/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/exr_writer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace ccl::deep;
namespace {
void check(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
template<class F> void rejects(F f)
{
  bool failed = false;
  try {
    f();
  }
  catch (const std::invalid_argument &) {
    failed = true;
  }
  check(failed, "Expected density rejection");
}
double oracle(const std::vector<LinearDensityInterval> &profile, double z)
{
  double tau = 0;
  for (const auto &s : profile) {
    const double h = std::clamp(z - s.front, 0.0, s.back - s.front);
    tau += s.physical_length_per_depth *
           (s.sigma_front * h + (s.sigma_back - s.sigma_front) * h * h / (2 * (s.back - s.front)));
  }
  return std::exp(-tau);
}
double integrated(const std::vector<VolumeInterval> &intervals, double z)
{
  double tau = 0;
  for (const auto &s : intervals)
    tau += s.optical_depth * std::clamp((z - s.front) / (s.back - s.front), 0.0, 1.0);
  return std::exp(-tau);
}
}  // namespace

int main(int argc, char **argv)
{
  try {
    check(argc == 2, "Expected fixture directory");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    const std::vector<std::vector<LinearDensityInterval>> cases{
        {},
        {{2, 8, 0, 0}},
        {{2, 8, .2, .2}},
        {{2, 8, 0, .3}},
        {{2, 8, .3, 0, std::sqrt(2.0)}},
        {{2, 7, .1, .2}, {4, 9, .2, .1}},
        {{4.11, 4.12, 0, 4}, {4.12, 4.13, 4, 0}},
        // Exact clip of the rising [2,8] profile to [3,6].
        {{3, 6, .05, .2}}};
    double maximum = 0;
    auto verify = [&](const std::vector<LinearDensityInterval> &p) {
      const auto v = integrate_linear_density(p);
      auto probe = [&](double z) {
        const double error = std::abs(oracle(p, z) - integrated(v, z));
        maximum = std::max(maximum, error);
        check(error <= 1.00001e-7, "Density integration error");
      };
      for (int i = 0; i <= 1000; ++i)
        probe(i * .01);
      for (const auto &s : v) {
        probe(s.front);
        probe((s.front + s.back) * .5);
        probe(s.back);
      }
      check(std::abs(oracle(p, 10) - integrated(v, 10)) < 1e-12, "Total optical depth changed");
    };
    for (const auto &p : cases)
      verify(p);
    std::mt19937 rng(891);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < 20; ++i)
      verify({{1 + u(rng), 4 + u(rng), u(rng), u(rng), 1 + u(rng)}});
    check(integrate_linear_density(cases[2]).size() == 1, "Constant profile split");
    check(1 - oracle(cases[6], 5) > .03, "Thin feature fixture ineffective");
    rejects([&] { integrate_linear_density(cases[3], 1e-7, 1); });
    rejects([&] { integrate_linear_density({{2, 3, .1, .1}, {4, 5, .1, .1}}, 1e-7, 1); });
    rejects([&] { integrate_linear_density(cases[3], 0); });
    rejects([&] {
      integrate_linear_density(
          cases[3], std::numeric_limits<double>::denorm_min(), std::numeric_limits<size_t>::max());
    });
    rejects([] { integrate_linear_density({{2, 3, -1, .1}}); });
    rejects([] { integrate_linear_density({{2, 2, .1, .1}}); });
    rejects([] { integrate_linear_density({{2, 3, .1, .1, 0}}); });
    rejects([] { integrate_linear_density({{2, 3, INFINITY, .1}}); });
    rejects([] { integrate_linear_density({{1e16, std::nextafter(1e16, INFINITY), 0, 1}}); });

    // A partly covered ray verifies averaging transmittance after integration.
    const auto v = integrate_linear_density({{2, 6, .01, .013}, {3, 7, .02, .018}});
    const auto curve = reconstruct_volume({{{0, 1, true, {}}, v}, {{1, 1, true, {}}, {}}}, 1e-7);
    for (int i = 0; i <= 1000; ++i) {
      const double z = i * .01;
      check(std::abs(interval_transmittance(curve, z) -
                     (.5 + .5 * oracle({{2, 6, .01, .013}, {3, 7, .02, .018}}, z))) <= 2e-7,
            "Covered overlap reconstruction error");
    }

    // Orthographic analytic sphere, with known linear extinction along each ray.
    constexpr int width = 32, height = 24;
    std::vector<std::vector<IntervalSample>> pixels(width * height);
    std::ofstream expected(directory / "density_expected.csv");
    expected << std::setprecision(17) << "x,file_y,depth,alpha\n";
    size_t point_count = 0;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const double px = (x + .5 - width * .5) / 6, py = (y + .5 - height * .5) / 6;
        const double r2 = px * px + py * py;
        std::vector<LinearDensityInterval> profile;
        if (r2 < 3.24) {
          const double h = std::sqrt(3.24 - r2);
          // sigma(z)=.15+.001*(z-5); spatially smooth at each sphere chord.
          profile.push_back({5 - h, 5 + h, .15 - .001 * h, .15 + .001 * h});
          for (const auto &s : integrate_linear_density(profile))
            pixels[y * width + x].push_back({s.front, s.back, -std::expm1(-s.optical_depth)});
        }
        point_count += pixels[y * width + x].size();
        for (int cut = 0; cut <= 12; ++cut) {
          const double z = 3 + cut * .35;
          expected << x << ',' << y << ',' << z << ',' << 1 - oracle(profile, z) << '\n';
        }
      }
    SurfaceImage image{{0, 0, width - 1, height - 1}, {0, 0, width - 1, height - 1}};
    image.view = "default";
    image.compression = DeepCompression::Zips;
    write_volume_exr(directory / "density_sphere.exr", image, pixels);
    std::cout << "PASS: density integration max error " << std::setprecision(17) << maximum
              << "; sphere intervals " << point_count << '\n';
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
