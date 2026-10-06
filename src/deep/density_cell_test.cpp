/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CCL_NAMESPACE_BEGIN
#  define CCL_NAMESPACE_BEGIN namespace ccl {
#  define CCL_NAMESPACE_END }
#endif
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/volume_boundary.h"
#include "deep/volume.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include <limits>

namespace {
void check(const bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

/* Independent corner evaluation and two-point Gaussian quadrature. A
 * trilinear cell restricted to a line is cubic, so this quadrature is exact
 * in real arithmetic, including clipped intervals. */
double density(const double *corners, const double *start, const double *end, const double t)
{
  double p[3];
  for (int axis = 0; axis < 3; ++axis)
    p[axis] = start[axis] + t * (end[axis] - start[axis]);
  double result = 0;
  for (int i = 0; i < 8; ++i) {
    double weight = corners[i];
    for (int axis = 0; axis < 3; ++axis)
      weight *= (i & (1 << axis)) ? p[axis] : 1 - p[axis];
    result += weight;
  }
  return result;
}
double integral(const double *corners,
                const double *start,
                const double *end,
                const double length,
                const double a,
                const double b)
{
  const double middle = (a + b) / 2;
  const double half = (b - a) / 2;
  const double delta = half / std::sqrt(3.0);
  return length * half * (density(corners, start, end, middle - delta) +
                          density(corners, start, end, middle + delta));
}

void grid_tests()
{
  using namespace ccl;
  /* A production overlap ray starts on a grid plane. Fused position arithmetic
   * can choose the preceding cell while division rounds its exit to start.
   * Also include cancellation cases that reproduce with unfused arithmetic. */
  const double clipped_rays[][2] = {
      {-304.76766072190367, 0.91253751516342163},
      {-193.31958277894515, 0.707061631312155},
      {-228.2156701619392, 0.6650244287666001},
      {-252.95390239087573, 0.4812357726306221}};
  for (const auto &ray : clipped_rays) {
    const double o = ray[0], d = ray[1];
    for (const double sign : {-1.0, 1.0}) {
      const double origin[] = {sign * o, .25, .25};
      const double direction[] = {sign * d, 0, 0};
      const double crossing = (1-o)/d, end = crossing + .5/d;
      for (const double start : {std::nextafter(crossing, 0.0), crossing,
                                 std::nextafter(crossing, end)}) {
        DeepGridCursor<double> cursor{};
        DeepGridSegment<double> segment{};
        check(deep_grid_begin(&cursor, origin, direction, start, end, 3),
              "Rounded start boundary rejected");
        if (start < crossing) {
          check(deep_grid_next(&cursor, &segment) == DEEP_GRID_SEGMENT &&
                    segment.front == start && segment.back == crossing &&
                    segment.cell[0] == (sign > 0 ? 0 : -1),
                "Start correction skipped a positive-length interval");
        }
        check(deep_grid_next(&cursor, &segment) == DEEP_GRID_SEGMENT &&
                  segment.front == std::max(start, crossing) && segment.back == end &&
                  segment.cell[0] == (sign > 0 ? 1 : -2),
              "Rounded start selected a zero-length interpolation cell");
        check(deep_grid_next(&cursor, &segment) == DEEP_GRID_DONE,
              "Rounded-start ray did not finish");
      }
    }
  }
  std::mt19937 rng(3181);
  std::uniform_real_distribution<double> uniform(-1, 1);
  for (int fixture = 0; fixture < 500; ++fixture) {
    double origin[3], direction[3];
    for (int axis = 0; axis < 3; ++axis) {
      origin[axis] = 4 * uniform(rng);
      direction[axis] = uniform(rng);
    }
    if (fixture < 6) {
      /* Exact face, edge and corner starts, in both directions. */
      for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = -2;
        direction[axis] = axis <= fixture / 2 ? (fixture % 2 ? -1 : 1) : 0;
      }
    }
    const double start = fixture < 6 ? 0 : .125, end = 3.125;
    std::vector<double> boundaries{start, end};
    for (int axis = 0; axis < 3; ++axis) {
      if (!direction[axis])
        continue;
      /* Independent oracle: enumerate integer planes rather than walking cells. */
      for (int plane = -10; plane <= 10; ++plane) {
        const double t = (plane - origin[axis]) / direction[axis];
        if (t > start && t < end)
          boundaries.push_back(t);
      }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    DeepGridCursor<double> cursor{};
    check(deep_grid_begin(&cursor, origin, direction, start, end, 100), "Grid init failed");
    for (size_t i = 1; i < boundaries.size(); ++i) {
      DeepGridSegment<double> segment{};
      check(deep_grid_next(&cursor, &segment) == DEEP_GRID_SEGMENT, "Missing grid segment");
      check(segment.front == boundaries[i - 1] && segment.back == boundaries[i],
            "Grid traversal skipped or duplicated a boundary");
      for (int axis = 0; axis < 3; ++axis) {
        const double midpoint = origin[axis] +
                                (segment.front + segment.back) / 2 * direction[axis];
        check(segment.cell[axis] == int(std::floor(midpoint)), "Incorrect interpolation cell");
      }
    }
    DeepGridSegment<double> unused{};
    check(deep_grid_next(&cursor, &unused) == DEEP_GRID_DONE, "Grid did not finish");
  }
  for (const bool reverse : {false, true}) {
    const double origin[] = {reverse ? 2.0 : -2.0, .25, .25};
    const double direction[] = {reverse ? -1.0 : 1.0, 0, 0};
    DeepGridCursor<double> cursor{};
    check(deep_grid_begin(&cursor, origin, direction, 0.0, 4.0, 16), "Tent init failed");
    DeepGridSegment<double> segment{};
    double tau = 0;
    DeepGridStep step;
    while ((step = deep_grid_next(&cursor, &segment)) == DEEP_GRID_SEGMENT) {
      double corners[8], a[3], b[3];
      for (int corner = 0; corner < 8; ++corner) {
        bool at_origin = true;
        for (int axis = 0; axis < 3; ++axis)
          at_origin &= segment.cell[axis] + ((corner >> axis) & 1) == 0;
        corners[corner] = at_origin ? 1 : 0;
      }
      for (int axis = 0; axis < 3; ++axis) {
        a[axis] = origin[axis] + segment.front * direction[axis] - segment.cell[axis];
        b[axis] = origin[axis] + segment.back * direction[axis] - segment.cell[axis];
      }
      tau += deep_density_integral(deep_trilinear_density(corners, a, b),
                                   segment.back - segment.front);
    }
    check(step == DEEP_GRID_DONE && std::abs(tau - .5625) < 1e-14,
          "Cell traversal missed one-voxel tent feature");
  }
  DeepGridCursor<double> cursor{};
  DeepGridSegment<double> segment{};
  const double origin[] = {0, 0, 0}, direction[] = {1, 0, 0}, zero[] = {0, 0, 0};
  check(!deep_grid_begin(&cursor, origin, zero, 0.0, 1.0, 2), "Zero ray accepted");
  check(deep_grid_next(&cursor, &segment) == DEEP_GRID_INVALID, "Invalid ray appeared complete");
  check(!deep_grid_begin(&cursor, origin, direction, 1.0, 1.0, 2), "Empty range accepted");
  check(!deep_grid_begin(&cursor, origin, direction, 0.0,
                        std::numeric_limits<double>::infinity(), 2), "Infinite range accepted");
  check(deep_grid_begin(&cursor, origin, direction, 0.0, 3.0, 2), "Limited init failed");
  check(deep_grid_next(&cursor, &segment) == DEEP_GRID_SEGMENT &&
            deep_grid_next(&cursor, &segment) == DEEP_GRID_SEGMENT &&
            deep_grid_next(&cursor, &segment) == DEEP_GRID_LIMIT,
        "Traversal exhaustion was not explicit");
}
}  // namespace

int main()
{
  try {
    {
      /* Captured production rays: a true interval narrower than one FLOAT ULP,
       * and a false grazing exit accepted by the native FLOAT triangle test.
       * Compare triangle crossings with an independent axis-aligned slab oracle. */
      const double lo[3] = {-112.499725f, -94.9990005f, -285.499847f};
      const double hi[3] = {90.5002747f, 577.000977f, 177.500168f};
      const double origins[2][3] = {{28.0226612f, -422.024323f, -103.740944f},
                                    {72.2773285f, -370.052856f, 168.97818f}};
      const double directions[2][3] = {{-.3749322f, .872546554f, -.31319043f},
                                       {-.325293064f, .945494235f, .0150026893f}};
      const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4},
                              {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
      double vertices[8][3];
      for (int i = 0; i < 8; ++i)
        for (int axis = 0; axis < 3; ++axis)
          vertices[i][axis] = (i & (1 << axis)) ? hi[axis] : lo[axis];
      for (int ray = 0; ray < 2; ++ray) {
        double enter = 0, exit = 1e20;
        for (int axis = 0; axis < 3; ++axis) {
          const double a = (lo[axis] - origins[ray][axis]) / directions[ray][axis];
          const double b = (hi[axis] - origins[ray][axis]) / directions[ray][axis];
          enter = std::max(enter, std::min(a, b));
          exit = std::min(exit, std::max(a, b));
        }
        int entries = 0, exits = 0;
        for (const auto &face : faces)
          for (int half = 0; half < 2; ++half) {
            const int ids[3] = {face[0], face[half + 1], face[half + 2]};
            double triangle[3][3];
            for (int i = 0; i < 3; ++i)
              for (int axis = 0; axis < 3; ++axis)
                triangle[i][axis] = vertices[ids[i]][axis];
            double t, u, v;
            bool back;
            if (ccl::deep_volume_triangle(origins[ray], directions[ray], triangle, t, u, v, back)) {
              check(std::abs(t - (back ? exit : enter)) < 1e-10,
                    "Boundary crossing differs from slab oracle");
              back ? ++exits : ++entries;
            }
          }
        check(entries == 1 && exits == 1, "Missing or spurious volume boundary crossing");
        check(exit > enter, "Grazing interval lost");
        if (ray == 0)
          check(float(enter) == float(exit), "Fixture no longer exercises sub-FLOAT interval");
      }
    }
    {
      using namespace ccl::deep;
      /* Thin interior density, overlapping medium and an adjacent cell. The
       * oracle integrates the independently expanded Bernstein polynomial. */
      std::vector<CubicDensityInterval> cells = {
          {1, 3, {0, .8, .2, 0}}, {2, 4, {.1, .2, .7, .3}},
          {3, 5, {.2, .2, .2, .2}}};
      for (int dense = 0; dense < 2; ++dense) {
        // Dense, overlapping cells exercise attenuation-aware fitting. Shuffle
        // by reversal so correctness cannot depend on input front-to-back order.
        if (dense) {
          for (int i = 0; i < 12; ++i)
            cells.push_back({1.5 + i * .15, 2.5 + i * .15, {20, 40, 5, 30}});
          std::reverse(cells.begin(), cells.end());
        }
        const auto fitted = integrate_cubic_density(cells, 1e-7);
        for (int probe = 0; probe <= 2000; ++probe) {
          const double z = 1 + 4 * probe / 2000.0;
          double exact = 0, approximate = 0;
          for (const auto &s : cells) {
            const double u = std::clamp((z - s.front) / (s.back - s.front), 0.0, 1.0);
            const auto &b = s.optical_depth;
            exact += b[0] * u + 1.5 * (b[1] - b[0]) * u * u +
                     (b[0] - 2 * b[1] + b[2]) * u * u * u +
                     (-b[0] + 3 * b[1] - 3 * b[2] + b[3]) * u * u * u * u / 4;
          }
          for (const auto &s : fitted)
            approximate += s.optical_depth *
                           std::clamp((z - s.front) / (s.back - s.front), 0.0, 1.0);
          check(std::abs(std::exp(-exact) - std::exp(-approximate)) <= 1e-7 + 1e-13,
                "Cubic interval fit exceeded transmittance budget");
          if (probe == 2000)
            check(std::abs(exact - approximate) < 1e-12, "Cubic fit changed total tau");
      }
      bool rejected = false;
      try { integrate_cubic_density(cells, 1e-7, 1); }
      catch (const std::invalid_argument &) { rejected = true; }
      check(rejected, "Cubic capacity failure was not reported");
      }
      bool rejected = false;
      try { integrate_cubic_density({{1, 2, {0, -1, 0, 0}}}); }
      catch (const std::invalid_argument &) { rejected = true; }
      check(rejected, "Negative cubic density was accepted");
      check(integrate_cubic_density({{1, 2, {0, 0, 0, 0}}}).empty(),
            "Empty cubic cell produced intervals");
    }
    using namespace ccl;
    grid_tests();
    const double start[] = {0, 0, 0}, end[] = {1, 1, 1};
    const double feature[] = {0, 6, 0, 0, 0, 0, 0, 0};
    const auto hidden = deep_trilinear_density(feature, start, end);
    check(hidden.b[0] == 0 && hidden.b[3] == 0, "Expected zero endpoint density");
    check(deep_density_integral(hidden, 1.0) == .5, "Endpoint-only integration misses feature");

    std::mt19937 rng(7123);
    std::uniform_real_distribution<double> uniform(0, 1);
    double maximum_integral_error = 0, maximum_density_error = 0;
    for (int fixture = 0; fixture < 500; ++fixture) {
      double corners[8], a[3], b[3];
      for (double &v : corners)
        v = fixture < 2 ? double(fixture) : 4 * uniform(rng);
      for (int axis = 0; axis < 3; ++axis) {
        a[axis] = uniform(rng);
        b[axis] = uniform(rng);
      }
      /* Include stationary axes, reverse directions and zero-length clips. */
      if (fixture % 3 == 0)
        b[(fixture / 3) % 3] = a[(fixture / 3) % 3];
      const double length = 10 * uniform(rng);
      const auto curve = deep_trilinear_density(corners, a, b);
      for (double coefficient : curve.b)
        check(coefficient >= 0, "Negative Bernstein density");
      const double total = deep_density_integral(curve, length);
      const double bound = deep_density_chord_error(curve, length);
      for (int probe = 0; probe <= 100; ++probe) {
        const double t = probe / 100.0;
        const double expected_density = density(corners, a, b, t);
        const double actual_density = deep_density_blossom(curve, t, t, t);
        const double density_error = std::abs(actual_density - expected_density);
        maximum_density_error = std::max(maximum_density_error, density_error);
        check(density_error < 2e-14, "Cell density differs from corner oracle");
        const auto clipped = deep_density_restrict(curve, 0.0, t);
        const double expected_tau = integral(corners, a, b, length, 0, t);
        const double actual_tau = deep_density_integral(clipped, length * t);
        const double tau_error = std::abs(actual_tau - expected_tau);
        maximum_integral_error = std::max(maximum_integral_error, tau_error);
        check(tau_error < 2e-13, "Cell integral differs from Gaussian oracle");
        check(std::abs(expected_tau - t * total) <= bound + 2e-13,
              "Optical-depth chord error exceeds Bernstein bound");
        check(std::abs(std::exp(-expected_tau) - std::exp(-t * total)) <= bound + 2e-13,
              "Transmittance error exceeds bound");
      }
      const double u = uniform(rng), v = u + (1 - u) * uniform(rng);
      const auto restricted = deep_density_restrict(curve, u, v);
      check(std::abs(deep_density_integral(restricted, length * (v - u)) -
                     integral(corners, a, b, length, u, v)) < 2e-13,
            "Nonzero-start clipping integral differs");
      const auto left = deep_density_restrict(curve, 0.0, .5);
      const auto right = deep_density_restrict(curve, .5, 1.0);
      check(std::abs(deep_density_integral(left, length / 2) +
                     deep_density_integral(right, length / 2) - total) < 2e-13,
            "Splitting changed total optical depth");
    }
    std::cout << "PASS: grid traversal, trilinear density/integral, clipping and bounds; max errors "
              << maximum_density_error << ", " << maximum_integral_error << '\n';
  }
  catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
