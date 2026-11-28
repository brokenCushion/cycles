/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CCL_NAMESPACE_BEGIN
#  define CCL_NAMESPACE_BEGIN namespace ccl {
#  define CCL_NAMESPACE_END }
#endif
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/volume_boundary.h"
#include "kernel/deep/mip.h"
#include "kernel/deep/write.h"
#include "deep/volume.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include <limits>
#include <cstring>

namespace {
void check(const bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void mip_tests()
{
  /* Independent native selector quadrature, including transitions and endpoint
   * clamping. Distinct mip values expose an incorrect floor-only lookup. */
  for (int levels : {1, 2, 8, 20}) {
    for (int position = -400; position <= levels * 400; ++position) {
      const float f = position / 400.0f;
      const auto mip = ccl::deep_mip_filter(f, levels);
      check(mip.low >= 0 && mip.high < levels && mip.high_weight >= 0 &&
            mip.high_weight <= 1, "Invalid deterministic mip mixture");
      double expected = 0;
      constexpr int probes = 1000;
      for (int i = 0; i < probes; ++i) {
        const double native_level = double(f) + .5 + ((i + .5) / probes - .5) * .5;
        const int level = std::clamp(int(native_level), 0, levels - 1);
        expected += double(level * level + 1) / probes;
      }
      const double actual = (1 - double(mip.high_weight)) * (mip.low * mip.low + 1) +
                            double(mip.high_weight) * (mip.high * mip.high + 1);
      check(std::abs(expected - actual) <= (2 * levels + 1) / double(probes),
            "Deep mip filter differs from native stochastic expectation");
      const auto repeated = ccl::deep_mip_filter(f, levels);
      check(mip.low == repeated.low && mip.high == repeated.high &&
            mip.high_weight == repeated.high_weight, "Mip filter is not deterministic");
    }
  }
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
/* Event capacity fallback (kernel/deep/merge.h, deep_event_reserve): the
 * same kernel code CPU, CUDA and OptiX run, exercised on the CPU. */
struct MergeLane {
  int stride;
  bool compact, companions;
  std::vector<KernelDeepEvent> events;
  std::vector<KernelDeepDensity> density;
  KernelDeepWriteState write;
  int count = 0;
  MergeLane(const int capacity, const int stride, const bool compact, const bool companions)
      : stride(stride), compact(compact), companions(companions),
        events(size_t(capacity) * stride), density(size_t(capacity) * stride)
  {
    write.fallback = true;
    write.compact_density = compact;
    write.volume_companions = companions;
  }
  /* What deep_volume_constant stores, without its roundoff allowance check. */
  KernelDeepError volume(const double front, const double back, const double tau,
                         const int object, const int capacity)
  {
    const auto error = ccl::deep_event_reserve(events.data(), density.data(), stride, capacity,
                                               &count, &write);
    if (error != DEEP_ERROR_NONE)
      return error;
    float high = float(tau);
    if (double(high) > tau)
      high = std::nextafter(high, 0.0f);
    const KernelDeepDensity companion{{high, float(tau - double(high)), 0, 0}, front, back};
    ccl::deep_write_event(events.data(), density.data(), count * stride,
        {deep_event_pack(DEEP_VOLUME, object), float(front), float(back), 0, float(tau)},
        companions ? &companion : nullptr, &write);
    ++count;
    return DEEP_ERROR_NONE;
  }
  KernelDeepError surface(const float depth, const float alpha, const int object,
                          const int capacity, const float facing = 1)
  {
    const auto error = ccl::deep_event_reserve(events.data(), density.data(), stride, capacity,
                                               &count, &write);
    if (error != DEEP_ERROR_NONE)
      return error;
    ccl::deep_write_event(events.data(), density.data(), count * stride,
        {deep_event_pack(DEEP_SURFACE, object), depth, depth, alpha, facing}, nullptr, &write);
    ++count;
    return DEEP_ERROR_NONE;
  }
  /* Exact span and optical depth of event i, as the host decodes them. */
  void span(const int i, double &front, double &back, double &tau) const
  {
    const KernelDeepEvent &e = events[size_t(i) * stride];
    if (deep_event_type(e) == DEEP_SURFACE || !companions) {
      front = e.front, back = e.back, tau = e.optical_depth;
      return;
    }
    int ordinal = 0;
    for (int j = 0; j < i; ++j)
      ordinal += deep_event_type(events[size_t(j) * stride]) != DEEP_SURFACE;
    const KernelDeepDensity &c = density[compact ? size_t(ordinal) : size_t(i) * stride];
    check(e.front == float(c.front) && e.back == float(c.back), "Merged companion bounds differ");
    check(c.optical_depth[0] >= 0 && c.optical_depth[1] >= 0, "Merged companion is negative");
    front = c.front, back = c.back;
    tau = double(c.optical_depth[0]) + double(c.optical_depth[1]);
  }
};

/* A smoke-like ray: 2000 contiguous steps of two objects, with gaps and
 * surfaces, written through the reserve into `capacity` slots. */
void merge_smoke(MergeLane &lane, const int capacity, std::vector<double> &boundaries,
                 std::vector<double> &cumulative, double &tau)
{
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> uniform(0, 1);
  double depth = 1;
  tau = 0;
  for (int i = 0; i < 2000; ++i) {
    if (i % 500 == 499) {
      check(lane.surface(float(depth), .25f, 3, capacity) == DEEP_ERROR_NONE,
            "Surface write failed");
      depth += .5; /* gap */
    }
    const double length = .01 * (1 + uniform(rng));
    const double sigma = 2 + std::sin(i * .01) + (i / 700) * 5;
    check(lane.volume(depth, depth + length, sigma * length, i < 1200 ? 5 : 6, capacity) ==
              DEEP_ERROR_NONE,
          "Overflowing write did not merge");
    /* Both ends: a gap leaves the back of the previous span unshared. */
    boundaries.push_back(depth);
    cumulative.push_back(tau);
    depth += length;
    tau += sigma * length;
    boundaries.push_back(depth);
    cumulative.push_back(tau);
  }
}

void merge_tests()
{
  for (const int stride : {1, 3}) {
    for (const bool compact : {false, true}) {
      if (compact && stride != 1)
        continue;
      for (const bool companions : {true, false}) {
        if (compact && !companions)
          continue;
        /* Under capacity nothing changes: bytes equal a lane without fallback. */
        MergeLane fits(64, stride, compact, companions), plain(64, stride, compact, companions);
        plain.write.fallback = false;
        for (int i = 0; i < 64; ++i) {
          check(fits.volume(1 + i, 2 + i, .01 * (i % 5), 7, 64) == DEEP_ERROR_NONE &&
                    plain.volume(1 + i, 2 + i, .01 * (i % 5), 7, 64) == DEEP_ERROR_NONE,
                "Under-capacity write failed");
        }
        check(!fits.write.fallbacks && fits.count == 64 &&
                  std::memcmp(fits.events.data(), plain.events.data(),
                              fits.events.size() * sizeof(KernelDeepEvent)) == 0 &&
                  std::memcmp(fits.density.data(), plain.density.data(),
                              fits.density.size() * sizeof(KernelDeepDensity)) == 0,
              "Under-capacity lane changed");
        /* Without fallback a full lane still fails explicitly, untouched. */
        check(plain.volume(65, 66, .01, 7, 64) == DEEP_ERROR_EVENT_CAPACITY && plain.count == 64,
              "Full lane without fallback did not fail");
        for (const int capacity : {64, 256}) {
          MergeLane lane(capacity, stride, compact, companions);
          std::vector<double> boundaries, cumulative; /* exact prefix optical depth */
          double tau = 0;
          merge_smoke(lane, capacity, boundaries, cumulative, tau);
          check(lane.count <= capacity && lane.count > capacity / 2 &&
                    (lane.write.fallbacks & DEEP_FALLBACK_MERGED) && !lane.write.failed &&
                    int(lane.write.events) == lane.count,
                "Merged lane exceeds capacity or lost its flag");
          int with_companion = 0, surfaces = 0;
          double total = 0;
          for (int i = 0; i < lane.count; ++i) {
            double front, back, t;
            lane.span(i, front, back, t);
            const KernelDeepEvent &e = lane.events[size_t(i) * stride];
            if (deep_event_type(e) == DEEP_SURFACE) {
              ++surfaces;
              check(e.surface_alpha == .25f && deep_event_object(e) == 3, "Surface was merged");
              continue;
            }
            with_companion += companions;
            check(deep_event_type(e) == DEEP_VOLUME, "Merged event kind");
            /* Every kept boundary is an original one; its prefix optical
             * depth (hence transmittance) equals the exact sum. */
            auto find = [&](const double z) {
              for (size_t k = 0; k < boundaries.size(); ++k)
                if (companions ? boundaries[k] == z : float(boundaries[k]) == float(z))
                  return int(k);
              return -1;
            };
            const int first = find(front), last = find(back);
            check(first >= 0 && last > first, "Merged span does not end on original boundaries");
            const double expected = cumulative[last] - cumulative[first];
            check(std::abs(t - expected) <= (companions ? 1e-12 : 1e-6) * (1 + expected),
                  "Merged optical depth differs from the sum");
            total += t;
          }
          check(surfaces == 4, "Surface count changed");
          check(int(lane.write.companions) == with_companion, "Companion cursor drifted");
          check(std::abs(total - tau) <= (companions ? 1e-11 : 1e-4) * tau,
                "Total optical depth changed");
          /* Deterministic: an identical second lane produces identical bytes. */
          MergeLane again(capacity, stride, compact, companions);
          std::vector<double> b2, c2;
          double t2;
          merge_smoke(again, capacity, b2, c2, t2);
          check(again.count == lane.count &&
                    std::memcmp(again.events.data(), lane.events.data(),
                                lane.events.size() * sizeof(KernelDeepEvent)) == 0 &&
                    std::memcmp(again.density.data(), lane.density.data(),
                                lane.density.size() * sizeof(KernelDeepDensity)) == 0,
                "Merge is not deterministic");
        }
        /* Count pass: no payload, one slot assumed freed, live counters. */
        KernelDeepWriteState counted;
        counted.count_only = true;
        counted.fallback = true;
        int count = 0;
        for (int i = 0; i < 100; ++i) {
          check(ccl::deep_event_reserve(nullptr, nullptr, 1, 8, &count, &counted) ==
                    DEEP_ERROR_NONE,
                "Count pass reserve failed");
          ccl::deep_write_event(nullptr, nullptr, 0,
              {deep_event_pack(DEEP_VOLUME, 1), 1, 2, 0, 1}, nullptr, &counted);
          ++count;
        }
        check(count == 8 && counted.events == 8 && (counted.fallbacks & DEEP_FALLBACK_MERGED),
              "Count pass prediction");
      }
    }
  }
  /* Numeric GPU count/output passes may deliver the source events unmerged. */
  {
    MergeLane raw(16, 1, true, true);
    raw.write.defer_merge = true;
    raw.write.limit = 16;
    for (int i = 0; i < 12; ++i)
      check(raw.volume(1 + i, 2 + i, .25, 2, 8) == DEEP_ERROR_NONE, "Write deferred lane");
    check(raw.count == 12 && raw.write.events == 12 && raw.write.companions == 12 &&
              (raw.write.fallbacks & DEEP_FALLBACK_MERGED),
          "Deferred lane did not retain the exact source records");
    KernelDeepWriteState counted;
    counted.count_only = counted.fallback = counted.defer_merge = true;
    int count = 0;
    for (unsigned i = 0; i < DEEP_FLAT_COUNT_LIMIT + 7; ++i) {
      check(ccl::deep_event_reserve(nullptr, nullptr, 1, 8, &count, &counted) == DEEP_ERROR_NONE,
            "Deferred count failed");
      ccl::deep_write_event(nullptr, nullptr, 0, {}, nullptr, &counted);
      ++count;
    }
    check(count == int(DEEP_FLAT_COUNT_LIMIT) && counted.events == DEEP_FLAT_COUNT_LIMIT,
          "Deferred count did not saturate at the staging limit");
  }
  /* Match CUDA's flush-to-zero behavior without changing stored optical depth. */
  check(ccl::deep_merge_rate(double(std::numeric_limits<float>::min()), 2) == 0 &&
            ccl::deep_merge_bin(std::numeric_limits<float>::denorm_min()) == 0,
        "Subnormal merge costs must use the zero bin");
  /* Most similar first: lower exponent bins merge before earlier pairs of a
   * higher bin, regardless of buffer order; at most `target` merge. */
  {
    MergeLane lane(6, 1, false, true);
    const double sigma[] = {9, 1, 4, 4, 1.5, 1.5};
    for (int i = 0; i < 6; ++i)
      check(lane.volume(1 + i, 2 + i, sigma[i], 2, 6) == DEEP_ERROR_NONE, "Write");
    int companions = 0;
    check(ccl::deep_merge_events(lane.events.data(), lane.density.data(), 1, 6, false, true, 2,
                                 &companions) == 2 && companions == 2,
          "Two most similar pairs");
    lane.count = 4;
    double front, back, tau;
    const double expected[4][3] = {{1, 2, 9}, {2, 3, 1}, {3, 5, 8}, {5, 7, 3}};
    for (int i = 0; i < 4; ++i) {
      lane.span(i, front, back, tau);
      check(front == expected[i][0] && back == expected[i][1] && tau == expected[i][2],
            "Most similar pairs did not merge first");
    }
  }
  /* A run can free several slots in one pass, keeping the optical depth at
   * the remaining boundary. The original first rate anchors the comparisons. */
  {
    MergeLane lane(8, 1, true, true);
    for (int i = 0; i < 8; ++i)
      check(lane.volume(1 + i, 2 + i, .25, 2, 8) == DEEP_ERROR_NONE, "Write equal-rate run");
    int companions = 0;
    check(ccl::deep_merge_events(lane.events.data(), lane.density.data(), 1, 8, true, true, 4,
                                 &companions) == 4 &&
              companions == 4,
          "Equal-rate run did not free its quota");
    lane.count = 4;
    double front, back, tau;
    lane.span(0, front, back, tau);
    check(front == 1 && back == 6 && tau == 1.25, "Merged run changed optical depth");
    lane.span(1, front, back, tau);
    check(front == 6 && back == 7 && tau == .25, "Compaction changed the next span");
  }
  /* Appending to a full lane can merge the same smooth tail repeatedly. */
  {
    MergeLane lane(8, 1, true, true);
    for (int i = 0; i < 12; ++i)
      check(lane.volume(1 + i, 2 + i, .25, 2, 8) == DEEP_ERROR_NONE, "Write streaming tail");
    check(lane.count == 8 && lane.write.companions == 8 &&
              (lane.write.fallbacks & DEEP_FALLBACK_MERGED),
          "Streaming tail exceeded capacity or changed the companion count");
    double front, back, tau;
    lane.span(6, front, back, tau);
    check(front == 7 && back == 12 && tau == 1.25, "Streaming tail changed optical depth");
    lane.span(7, front, back, tau);
    check(front == 12 && back == 13 && tau == .25, "Streaming tail changed the newest span");
  }
  /* Strict (no DEEP_VOLUME companions): cubic cells merge into a FLOAT span. */
  {
    std::vector<KernelDeepEvent> events(3);
    std::vector<KernelDeepDensity> density(3);
    for (int i = 0; i < 3; ++i) {
      events[i] = {deep_event_pack(DEEP_VOLUME_CUBIC, 4), float(1 + i), float(2 + i), 0, 0};
      density[i] = {{1, 2, 3, float(i)}, double(1 + i), double(2 + i)};
    }
    int companions = 0;
    check(ccl::deep_merge_events(events.data(), density.data(), 1, 3, false, false, 1,
                                 &companions) == 1 &&
              companions == 2 && deep_event_type(events[0]) == DEEP_VOLUME &&
              deep_event_object(events[0]) == 4 && events[0].front == 1 &&
              events[0].back == 3 && events[0].optical_depth == 1.5f + 1.75f &&
              deep_event_type(events[1]) == DEEP_VOLUME_CUBIC && density[1].optical_depth[3] == 2,
          "Strict cubic merge");
  }
  /* Surfaces only: the nearest-depth pair of one object; planes shift density. */
  {
    MergeLane lane(3, 2, false, true);
    check(lane.surface(1, .5f, 1, 3) == DEEP_ERROR_NONE &&
              lane.surface(2, .5f, 2, 3) == DEEP_ERROR_NONE &&
              lane.surface(2.1f, .5f, 2, 3, -1) == DEEP_ERROR_NONE &&
              lane.volume(4, 5, .3, 2, 3) == DEEP_ERROR_NONE && lane.count == 3,
          "Surface merge write");
    const KernelDeepEvent &merged = lane.events[2];
    check(merged.front == 2 && merged.back == 2 && merged.surface_alpha == .75f &&
              merged.optical_depth == 0 && deep_event_object(merged) == 2 &&
              lane.events[0].front == 1 && lane.events[4].front == 4 &&
              lane.density[4].front == 4 && lane.density[4].back == 5,
          "Nearest same-object surfaces did not merge");
    /* Nothing mergeable (different objects): fails as before. */
    MergeLane stuck(2, 1, false, true);
    check(stuck.surface(1, .5f, 1, 2) == DEEP_ERROR_NONE &&
              stuck.surface(2, .5f, 2, 2) == DEEP_ERROR_NONE &&
              stuck.surface(3, .5f, 3, 2) == DEEP_ERROR_EVENT_CAPACITY,
          "Unmergeable lane did not fail");
    /* Invalid values never merge. */
    std::vector<KernelDeepEvent> bad = {
        {deep_event_pack(DEEP_VOLUME, 1), 1, 2, 0, std::numeric_limits<float>::quiet_NaN()},
        {deep_event_pack(DEEP_VOLUME, 1), 2, 3, 0, 1},
        {deep_event_pack(DEEP_SURFACE, 1), 4, 4, 1.5f, 0},
        {deep_event_pack(DEEP_SURFACE, 1), 5, 5, .5f, 0}};
    int companions = 0;
    check(ccl::deep_merge_events(bad.data(), nullptr, 1, 4, false, false, 4, &companions) == 0,
          "Invalid events merged");
  }
  std::cout << "PASS: event capacity merge (planes, compact, strict, surfaces, count pass)\n";
}
}  // namespace

int main()
{
  try {
    {
      const double lo[3] = {0, 0, 0}, hi[3] = {1, 1, 1};
      const double origin[3] = {0, .5, -1}, direction[3] = {0, 0, 1};
      const double miss[3] = {2, .5, -1};
      check(ccl::deep_volume_bounds(origin, direction, lo, hi, 1, 1),
            "Parallel edge or equal-distance BVH candidate lost");
      check(!ccl::deep_volume_bounds(miss, direction, lo, hi, 0, 10),
            "Parallel ray outside BVH bound admitted");
    }
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
              check(ccl::deep_volume_bounds(origins[ray], directions[ray], lo, hi, t, t),
                    "Conservative BVH bounds lost an exact grazing crossing");
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
    mip_tests();
    grid_tests();
    merge_tests();
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
