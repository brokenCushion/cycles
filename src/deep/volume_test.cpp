/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/exr_writer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace ccl::deep;
namespace {
void check(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
template<class F> void rejects(F action)
{
  bool failed = false;
  try { action(); }
  catch (const std::exception &) { failed = true; }
  check(failed, "Expected invalid input/budget failure");
}

/* Independent Beer-Lambert oracle with physical lengths integrated in tau. */
double oracle(const std::vector<VolumeCameraSample> &samples, double z)
{
  double sum = 0, weight = 0;
  for (const auto &s : samples) {
    double t = 1;
    for (const auto &v : s.intervals) {
      if (z > v.front)
        t *= std::exp(-v.optical_depth * (std::min(z, v.back) - v.front) / (v.back - v.front));
    }
    for (const auto &e : s.camera.events)
      if (z >= e.depth)
        t *= 1 - e.alpha;
    sum += s.camera.weight * t;
    weight += s.camera.weight;
  }
  return sum / weight;
}

struct Fixture {
  const char *name;
  std::vector<VolumeCameraSample> samples;
};
}  // namespace

/* Replay one real diagnostic pixel to measure export changes without another
 * render. The CSV contains accepted camera extinction, never mesh samples. */
static int replay_camera_pixel(const char *source, const char *destination)
{
  std::ifstream input(source);
  std::string line;
  std::getline(input, line);
  check(line == "file_x,file_y,sample,front,back,value,kind,event", "Invalid camera CSV header");
  std::vector<VolumeCameraSample> rays;
  int first_x = -1, first_y = -1;
  while (std::getline(input, line)) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream row(line);
    int x, y, sample, event;
    double front, back, value;
    std::string kind;
    check(bool(row >> x >> y >> sample >> front >> back >> value >> kind >> event), "Invalid camera CSV row");
    if (first_x == -1) { first_x = x; first_y = y; }
    if (x != first_x || y != first_y)
      break;
    check(sample >= 0 && sample <= int(rays.size()), "Invalid replay camera identity");
    if (sample == int(rays.size()))
      rays.push_back({{uint64_t(sample), 1, true, {}}, {}});
    if (kind == "volume")
      rays.back().intervals.push_back({front, back, value});
    else if (kind == "surface")
      rays.back().camera.events.push_back({front, value});
    else
      check(kind == "miss" && event == -1 && front == 0 && back == 0 && value == 0, "Invalid replay miss");
  }
  check(!rays.empty(), "Missing replay camera data");
  size_t raw_count = 0, compact_count = 0;
  const auto start = std::chrono::steady_clock::now();
  for (auto &ray : rays) {
    raw_count += ray.intervals.size();
    const auto compact = reconstruct_volume({ray}, volume_reconstruction_error, 65536, volume_density_error / 2);
    ray.camera.events.clear();
    std::vector<VolumeInterval> intervals;
    for (const auto &span : compact)
      if (span.front == span.back)
        ray.camera.events.push_back({span.front, span.alpha});
      else
        intervals.push_back({span.front, span.back, -std::log1p(-span.alpha)});
    ray.intervals = std::move(intervals);
    compact_count += ray.intervals.size();
  }
  std::cout << "Pixel " << first_x << ',' << first_y << ": " << rays.size() << " cameras, "
            << raw_count << " raw / " << compact_count << " compact intervals\n" << std::flush;
  const auto curve = reconstruct_volume(rays, volume_reconstruction_error / 2, 65536,
                                        volume_reconstruction_error / 2, 512 * 1024 * 1024);
  std::ofstream output(destination);
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output << "front,back,alpha\n" << std::setprecision(17);
  for (const auto &span : curve)
    output << span.front << ',' << span.back << ',' << span.alpha << '\n';
  std::cout << "Reconstructed " << curve.size() << " intervals in "
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " seconds\n";
  return 0;
}

int main(int argc, char **argv)
{
  try {
    if (argc == 4 && std::string(argv[1]) == "--camera-csv")
      return replay_camera_pixel(argv[2], argv[3]);
    check(argc == 2 || argc == 3, "Expected output directory and optional captured-curve CSV");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    for (const int population : {9, 65, 1024}) {
      std::vector<VolumeCameraSample> rays;
      for (int i = 0; i < population; ++i) {
        VolumeCameraSample ray{{uint64_t(i), double(i % 7), true, {}}, {}};
        if (i % 5) {
          ray.intervals.push_back({2 + (i % 3) * .02, 8, .2 + (i % 11) * .1});
          ray.camera.events.push_back({4 + (i % 4) * .01, i % 2 ? .3 : 1});
        }
        rays.push_back(std::move(ray));
      }
      const auto curve = reconstruct_volume(rays, 2.5e-8, 16384, 2.5e-8);
      rejects([&] { reconstruct_volume(rays, 2.5e-8, 16384, 2.5e-8, 1); });
      for (int i = 0; i <= 2000; ++i) {
        const double z = 1 + i * .005;
        check(std::abs(interval_transmittance(curve, z) - oracle(rays, z)) < 5.01e-8,
              "Balanced camera mixture exceeded the shared error budget");
      }
    }
    std::vector<Fixture> fixtures = {
        {"homogeneous", {{{0, 1, true, {}}, {{2, 8, 1.8}}}}},
        {"off_axis", {{{0, 1, true, {}}, {{2, 8, 1.8 * std::sqrt(2.0)}}}}},
        {"long_open_interval", {{{0, 1, true, {}}, {{2, 20, 6.74601793}}}}},
        {"camera_inside", {{{0, 1, true, {}}, {{0.125, 4, 1.1625}}}}},
        {"overlapping_media", {{{0, 1, true, {}}, {{2, 7, 1.5}, {4, 9, 2}}}}},
        {"partial_coverage", {{{0, 1, true, {}}, {{2, 8, 1.8}}}, {{1, 1, true, {}}, {}}}},
        {"crossing_surface", {{{0, 1, true, {{5, .6}}}, {{2, 8, 1.8}}}}},
        {"opaque_surface", {{{0, 1, true, {{5, 1}}}, {{2, 8, 1.8}}}}},
        {"mixed_extinction", {{{0, 1, true, {}}, {{2, 8, .6}}},
                              {{1, 3, true, {{5, .25}}}, {{2, 8, 3.6}}}}},
        {"piecewise_density", {{{0, 1, true, {}}, {{2, 4, .2}, {4, 6, 1}, {6, 8, .4}}}}},
        {"empty", {{{0, 1, true, {}}, {}}}},
    };
    std::ofstream manifest(directory / "expected.csv");
    manifest << std::setprecision(17) << "fixture,depth,alpha\n";
    double maximum = 0;
    for (const auto &f : fixtures) {
      const auto reconstructed = reconstruct_volume(f.samples);
      const auto reduced = reconstruct_volume(f.samples, volume_reconstruction_error, 65536, 2e-8);
      check(interval_curve_error(reconstructed, reduced) <= 2e-8 + 1e-11,
            "Volume reduction exceeded whole-curve budget");
      for (int j = 0; j <= 1000; ++j) {
        const double z = j / 100.0;
        const double t = oracle(f.samples, z);
        const double error = std::abs(t - interval_transmittance(reconstructed, z));
        maximum = std::max(maximum, error);
        check(error <= 2.0001e-7, "Analytic interval fitting error");
        if (j % 25 == 0)
          manifest << f.name << ',' << z << ',' << 1 - t << '\n';
      }
      SurfaceImage image{{0, 0, 7, 7}, {0, 0, 7, 7}};
      image.view = "default";
      image.compression = DeepCompression::Zips;
      write_volume_exr(directory / (std::string(f.name) + ".exr"), image,
                       std::vector<std::vector<IntervalSample>>(64, reconstructed));
      std::cout << f.name << ": " << reconstructed.size() << " intervals\n";
    }
    check(reconstruct_volume(fixtures[0].samples).size() == 1, "Homogeneous interval over-split");
    /* After the foreground ray becomes opaque, only the other exponential
     * contributes. A zero-transmission ray must not force variance fitting. */
    const std::vector<VolumeCameraSample> extinguished{
        {{0, 1, true, {{2, 1}}}, {}},
        {{1, 1, true, {{10001, 1}}}, {{1, 10001, 100}}}};
    const auto surviving = reconstruct_volume(extinguished, 5e-8, 16384);
    check(surviving.size() < 100, "Extinguished ray caused excessive subdivision");
    for (int i = 0; i <= 1000; ++i) {
      const double z = i * 10.01;
      check(std::abs(oracle(extinguished, z) - interval_transmittance(surviving, z)) < 5.01e-8,
            "Extinguished-ray optimization changed physical transmission");
    }
    /* Partial volume coverage retains a clear ray beside an opaque medium.
     * The rate range alone grossly overestimates curvature in its faint tail. */
    std::vector<VolumeCameraSample> tail{{{0, 1, true, {}}, {}},
                                        {{1, 1, true, {}}, {}}};
    for (int i = 0; i < 64; ++i)
      tail[1].intervals.push_back({1.0 + i, 2.0 + i, 16});
    const auto fitted_tail = reconstruct_volume(tail, 2.5e-9, 65536, 2.5e-9);
    check(fitted_tail.size() < 13500, "Faint volume tail caused excessive fitting");
    for (int i = 0; i <= 2000; ++i) {
      const double z = 65.0 * i / 2000;
      const double expected = .5 * (1 + std::exp(-16 * std::clamp(z - 1, 0.0, 64.0)));
      check(std::abs(expected - interval_transmittance(fitted_tail, z)) < 2.51e-9,
            "Weighted curvature bound changed partial-volume transmission");
    }
    const double correct = oracle(fixtures[5].samples, 5);
    check(std::abs(correct - std::exp(-.45)) > .05, "Partial coverage fixture is ineffective");
    const std::vector<IntervalSample> whole{{2, 8, -std::expm1(-1.8)}};
    const std::vector<IntervalSample> split{{2, 5, -std::expm1(-.9)}, {5, 8, -std::expm1(-.9)}};
    check(interval_curve_error(whole, split) < 1e-14, "Exponential splitting changed T");
    check(interval_curve_error({{2, 8, .4}}, {{2, 8, .8}}) >= .399999,
          "Curve error missed endpoint");
    /* Same endpoints but different interior curve: stationary-point check matters. */
    check(interval_curve_error({{2, 8, .75}}, {{2, 5, .2}, {5, 8, .6875}}) > .2,
          "Curve error missed interior difference");
    auto invalid = fixtures[0].samples;
    invalid[0].camera.complete = false;
    rejects([&] { reconstruct_volume(invalid); });
    invalid = fixtures[0].samples;
    invalid.push_back(invalid[0]);
    rejects([&] { reconstruct_volume(invalid); });
    invalid = fixtures[0].samples;
    invalid[0].intervals[0].optical_depth = -1;
    rejects([&] { reconstruct_volume(invalid); });
    rejects([&] { reconstruct_volume(fixtures[5].samples, 2e-7, 1); });
    rejects([&] { reconstruct_volume(fixtures[0].samples, 0); });
    rejects([&] { reconstruct_volume(fixtures[0].samples, 1e-7, 10, -1); });
    rejects([&] { interval_curve_error({{2, 8, 1}}, {}); });
    std::mt19937 rng(918);
    std::uniform_real_distribution<double> value(0, 1);
    for (int trial = 0; trial < 25; ++trial) {
      std::vector<VolumeCameraSample> samples;
      for (int s = 0; s < 4; ++s)
        samples.push_back({{uint64_t(s), .1 + value(rng), true, {{5, value(rng)}}},
                           {{2, 8, 4 * value(rng)}, {3, 7, value(rng)}}});
      const auto output = reconstruct_volume(samples);
      const auto reduced = reconstruct_volume(samples, volume_reconstruction_error, 65536, 2e-8);
      check(interval_curve_error(output, reduced) <= 2e-8 + 1e-11,
            "Random reduction exceeded whole-curve budget");
      for (int j = 0; j < 300; ++j) {
        const double z = 10 * value(rng);
        check(std::abs(oracle(samples, z) - interval_transmittance(output, z)) <
                  volume_reconstruction_error + 1e-11,
              "Random volume curve mismatch");
      }
    }
    /* Staggered, gapped VDB-like rays exercise indexed optical-depth queries
     * at cell boundaries, interior cuts and surface steps. Reversing one ray
     * also checks unordered runs against the same physical oracle. */
    std::vector<VolumeCameraSample> long_rays;
    for (int s = 0; s < 4; ++s) {
      VolumeCameraSample ray{{uint64_t(s), double(s + 1), true, {{3.5, .2}}}, {}};
      for (int cell = 0; cell < 512; ++cell) {
        const double front = 1 + cell * .01 + s * .001;
        ray.intervals.push_back({front, front + .008, .001 + .004 * value(rng)});
      }
      long_rays.push_back(std::move(ray));
    }
    const auto started = std::chrono::steady_clock::now();
    const auto indexed = reconstruct_volume(long_rays);
    std::cout << "Four 512-cell rays: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
              << " seconds, " << indexed.size() << " output intervals\n";
    const auto reduced_rays = reconstruct_volume(long_rays, volume_reconstruction_error,
                                                  indexed.size(), 2e-8);
    check(interval_curve_error(indexed, reduced_rays) <= 2e-8 + 1e-11,
          "Long-ray reduction exceeded whole-curve budget");
    std::cout << "Reduced four-ray output: " << reduced_rays.size() << " intervals\n";
    /* Redundant shared boundaries should not exhaust the output capacity. An
     * empty zero-weight sample exercises the general mixture path. */
    std::vector<VolumeCameraSample> uniform_rays{{{0, 1, true, {}}, {}},
                                                {{1, 0, true, {}}, {}}};
    for (int i = 0; i < 2000; ++i)
      uniform_rays[0].intervals.push_back({1 + i * .001, 1 + (i + 1) * .001, .0001});
    rejects([&] { reconstruct_volume(uniform_rays, 2e-8, 10); });
    const auto uniform_reduced = reconstruct_volume(uniform_rays, 2e-8, 10, 2e-8);
    check(uniform_reduced.size() == 1, "Constant-density subdivision was not reduced");
    check(interval_curve_error(uniform_reduced, {{1, 3, -std::expm1(-.2)}}) < 1e-11,
          "Long merge chain changed extinction");
    auto sparse_weights = uniform_rays;
    for (uint64_t id = 2; id < 9; ++id)
      sparse_weights.push_back({{id, 0, true, {}}, {}});
    check(interval_curve_error(uniform_reduced,
          reconstruct_volume(std::move(sparse_weights), 2e-8, 10, 2e-8,
                             uniform_rays[0].intervals.capacity() * 128)) < 1e-11,
          "Zero-weight camera filtering changed the bounded curve");
    uniform_rays.pop_back();
    check(reconstruct_volume(uniform_rays, 2e-8, 10, 2e-8).size() == 1,
          "Single-ray reduction exhausted capacity before merging");
    const std::vector<VolumeCameraSample> alternating_rate{
        {{0, 1, true, {}}, {{1, 2, .1}, {2, 3, .1001}, {3, 4, .0999}}}};
    /* The pairwise sum is 1.2711e-4, but the actual three-span error is
     * 8.1869e-5. Tightening may rescue that merge, without resetting its budget. */
    const auto tightened = reconstruct_volume(alternating_rate, 1e-8, 1, 1e-4);
    check(tightened.size() == 1, "Bounded source check did not rescue a valid merge");
    for (int i = 0; i <= 300; ++i) {
      const double z = 1 + i * .01;
      check(std::abs(oracle(alternating_rate, z) - interval_transmittance(tightened, z)) <= 1e-4,
            "Rescued merge exceeded physical curve budget");
    }
    rejects([&] { reconstruct_volume(alternating_rate, 1e-8, 1, 8e-5); });
    for (const auto &ray : long_rays)
      for (const auto &v : ray.intervals)
        for (double z : {v.front, (v.front + v.back) / 2, v.back})
          check(std::abs(oracle(long_rays, z) - interval_transmittance(indexed, z)) <
                    volume_reconstruction_error + 1e-11,
                "Indexed volume boundary/interior mismatch");
    for (double z : {std::nextafter(3.5, 0.0), 3.5, 7.0})
      check(std::abs(oracle(long_rays, z) - interval_transmittance(indexed, z)) <
                volume_reconstruction_error + 1e-11,
            "Indexed volume surface/tail mismatch");
    std::reverse(long_rays[0].intervals.begin(), long_rays[0].intervals.end());
    const auto unordered = reconstruct_volume(long_rays);
    check(interval_curve_error(indexed, unordered) < 1e-11,
          "Unordered volume evaluation differs from indexed evaluation");
    auto overlapping_rays = long_rays;
    for (auto &ray : overlapping_rays) {
      std::sort(ray.intervals.begin(), ray.intervals.end(),
                [](const auto &a, const auto &b) { return a.front < b.front; });
      auto second_grid = ray.intervals;
      for (auto &v : second_grid) {
        v.front += .003;
        v.back += .003;
        v.optical_depth *= .7;
      }
      ray.intervals.insert(ray.intervals.end(), second_grid.begin(), second_grid.end());
    }
    const auto overlapping = reconstruct_volume(overlapping_rays);
    for (const auto &ray : overlapping_rays)
      for (size_t i = 0; i < ray.intervals.size(); i += 7) {
        const auto &v = ray.intervals[i];
        for (double z : {v.front, (v.front + v.back) / 2, v.back})
          check(std::abs(oracle(overlapping_rays, z) - interval_transmittance(overlapping, z)) <
                    volume_reconstruction_error + 1e-11,
                "Overlapping indexed grid runs differ from physical oracle");
      }
    for (auto &ray : overlapping_rays)
      std::shuffle(ray.intervals.begin(), ray.intervals.end(), rng);
    check(interval_curve_error(overlapping, reconstruct_volume(overlapping_rays)) < 1e-11,
          "Overlapping grid result depends on interval order");
    /* Analytic perspective rays through two overlapping spheres. This is a
     * reference fixture, not a Cycles render. Physical chord length sets tau. */
    constexpr int width = 160, height = 120;
    SurfaceImage image{{0, 0, width - 1, height - 1}, {0, 0, width - 1, height - 1}};
    image.view = "default";
    image.compression = DeepCompression::Zips;
    std::vector<std::vector<IntervalSample>> pixels;
    std::ofstream expected(directory / "spheres_expected.csv");
    expected << std::setprecision(17) << "x,file_y,depth,alpha\n";
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const double dx = (2 * (x + .5) - width) / height * std::tan(.45);
        const double dy = (height - 2 * (y + .5)) / height * std::tan(.45);
        const double norm = std::sqrt(dx * dx + dy * dy + 1);
        VolumeCameraSample sample{{0, 1, true, {}}, {}};
        for (int sphere = 0; sphere < 2; ++sphere) {
          const double cx = sphere ? .65 : -.65, cz = sphere ? 6.5 : 5, radius = 1.5;
          const double projected = (dx * cx + cz) / norm;
          const double discriminant = projected * projected - (cx * cx + cz * cz - radius * radius);
          if (discriminant > 0) {
            const double half = std::sqrt(discriminant);
            sample.intervals.push_back({(projected - half) / norm, (projected + half) / norm,
                                         2 * half * (sphere ? .65 : .35)});
          }
        }
        pixels.push_back(reconstruct_volume({sample}));
        for (double cut : {4.0, 5.0, 6.0, 7.0, 9.0})
          expected << x << ',' << y << ',' << cut << ',' << 1 - oracle({sample}, cut) << '\n';
      }
    write_volume_exr(directory / "analytic_spheres.exr", image, pixels);
    /* A failed quantization must preserve a prior output. */
    const auto preserved = directory / "preserve.exr";
    { std::ofstream out(preserved); out << "sentinel"; }
    SurfaceImage one{{0, 0, 0, 0}, {0, 0, 0, 0}};
    if (argc == 3) {
      /* Replay a captured production pixel without re-rendering the scene. */
      std::ifstream input(argv[2]);
      std::string header;
      check(bool(std::getline(input, header)) && header == "front,back,alpha",
            "Invalid captured-curve CSV header");
      std::vector<IntervalSample> captured;
      IntervalSample value;
      char comma_a, comma_b;
      while (input >> value.front >> comma_a >> value.back >> comma_b >> value.alpha) {
        check(comma_a == ',' && comma_b == ',', "Invalid captured-curve CSV row");
        captured.push_back(value);
      }
      check(input.eof() && !captured.empty(), "Incomplete captured-curve CSV");
      write_volume_exr(directory / "captured_curve.exr", one, {captured});
    }
    std::vector<IntervalSample> ramp, rounded_ramp;
    for (int i = 0; i < 200; ++i) {
      const double front = 1200.00006 + .1 * i, back = 1200.00006 + .1 * (i + 1);
      const double alpha = -std::expm1(-.0001 * std::min(i + 1, 200 - i));
      ramp.push_back({front, back, alpha});
      rounded_ramp.push_back({double(float(front)), double(float(back)), double(float(alpha))});
    }
    check(interval_curve_error(ramp, rounded_ramp) > 1e-6,
          "Depth projection fixture does not exercise rounding drift");
    write_volume_exr(directory / "float_projected.exr", one, {ramp});
    std::vector<IntervalSample> dense;
    for (int i = 0; i < 10000; ++i)
      dense.push_back({2+i*.001, 2+(i+1)*.001, -std::expm1(-(.0001+i*1e-10))});
    dense.insert(dense.begin()+5000, {7, 7, .3});
    const auto reduced = reduce_interval_curve(dense, 2.5e-7);
    check(reduced.size() < dense.size(), "Dense export curve did not reduce");
    check(interval_curve_error(dense, reduced) <= 2.5e-7,
          "Export merges accumulated excess whole-curve error");
    check(std::abs(interval_transmittance(dense, 20)-interval_transmittance(reduced, 20)) < 1e-12,
          "Export reduction changed endpoint opacity");
    check(std::count_if(reduced.begin(), reduced.end(), [](const IntervalSample &s) {
            return s.front == s.back && s.front == 7 && s.alpha == .3;
          }) == 1, "Export reduction lost a surface step");
    const std::vector<IntervalSample> gap{{2, 3, .2}, {4, 5, .2}};
    check(reduce_interval_curve(gap, 1e-3).size() == 2, "Export reduction crossed an empty gap");
    rejects([&] { reduce_interval_curve(gap, -1); });
    const std::vector<IntervalSample> thin{{2, 2 + 1e-10, 1e-8}};
    check(interval_curve_error(thin, {{2, 2, double(float(1e-8))}}) < 1.1e-8,
          "Thin interval step error was not bounded");
    write_volume_exr(directory / "thin_bounded.exr", one, {thin});
    rejects([&] { write_volume_exr(preserved, one, {{{2, 2 + 1e-10, .5}}}); });
    std::ifstream read(preserved);
    std::string content;
    read >> content;
    check(content == "sentinel", "Failed volume publication replaced destination");
    read.close();
    std::filesystem::remove(preserved);
    std::cout << std::setprecision(17) << "Maximum analytic fitting error: " << maximum << '\n';
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
