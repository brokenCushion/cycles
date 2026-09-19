/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/exr_writer.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
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

int main(int argc, char **argv)
{
  try {
    check(argc == 2, "Expected output directory");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    std::vector<Fixture> fixtures = {
        {"homogeneous", {{{0, 1, true, {}}, {{2, 8, 1.8}}}}},
        {"off_axis", {{{0, 1, true, {}}, {{2, 8, 1.8 * std::sqrt(2.0)}}}}},
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
    const double correct = oracle(fixtures[4].samples, 5);
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
    rejects([&] { reconstruct_volume(fixtures[4].samples, 2e-7, 1); });
    rejects([&] { reconstruct_volume(fixtures[0].samples, 0); });
    rejects([&] { interval_curve_error({{2, 8, 1}}, {}); });
    std::mt19937 rng(918);
    std::uniform_real_distribution<double> value(0, 1);
    for (int trial = 0; trial < 25; ++trial) {
      std::vector<VolumeCameraSample> samples;
      for (int s = 0; s < 4; ++s)
        samples.push_back({{uint64_t(s), .1 + value(rng), true, {{5, value(rng)}}},
                           {{2, 8, 4 * value(rng)}, {3, 7, value(rng)}}});
      const auto output = reconstruct_volume(samples);
      for (int j = 0; j < 300; ++j) {
        const double z = 10 * value(rng);
        check(std::abs(oracle(samples, z) - interval_transmittance(output, z)) < 2.0001e-7,
              "Random volume curve mismatch");
      }
    }
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
