/* SPDX-License-Identifier: Apache-2.0 */

#include "deep/reconstruction.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

using namespace ccl::deep;

namespace {
constexpr double tolerance = 1e-12;
double max_error = 0.0;

void require(const bool condition, const std::string &message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void close(const double actual, const double expected)
{
  const double error = std::abs(actual - expected);
  require(std::isfinite(actual) && std::isfinite(expected) && error <= tolerance,
          "Numeric mismatch: " + std::to_string(actual) + " != " + std::to_string(expected));
  max_error = std::max(max_error, error);
}

double output_at(const std::vector<SurfaceSample> &output, const double z)
{
  double t = 1.0;
  for (const auto &sample : output) {
    if (sample.depth <= z) {
      t *= 1.0 - sample.alpha;
    }
  }
  return t;
}

/* Independent brute-force oracle: revisit every raw event for every query.
 * No boundary sorting, sweep state, or production summation helper is shared.
 * Used on bounded fixtures whose weight sum cannot overflow. */
double oracle_at(const PixelLedger &pixel, const double z)
{
  double numerator = 0.0, denominator = 0.0;
  for (const auto &sample : pixel.samples) {
    double t = 1.0;
    for (const auto &event : sample.events) {
      if (event.depth <= z) {
        t *= 1.0 - event.alpha;
      }
    }
    numerator += sample.weight * t;
    denominator += sample.weight;
  }
  return numerator / denominator;
}

void compare_oracle(const PixelLedger &pixel)
{
  const auto output = reconstruct(pixel);
  double previous_depth = 0.0;
  for (const auto &sample : output) {
    require(sample.depth > previous_depth && sample.alpha > 0.0 && sample.alpha <= 1.0,
            "Output must contain strictly sorted depths and positive valid alpha");
    previous_depth = sample.depth;
  }
  close(output_at(output, 0.0), 1.0);
  for (const auto &sample : pixel.samples) {
    for (const auto &event : sample.events) {
      for (const double z : {std::nextafter(event.depth, 0.0), event.depth}) {
        close(output_at(output, z), oracle_at(pixel, z));
      }
    }
  }
  close(output_at(output, std::numeric_limits<double>::infinity()),
        oracle_at(pixel, std::numeric_limits<double>::infinity()));
}

void expect(const PixelLedger &pixel,
            const std::vector<SurfaceSample> &expected,
            const double flattened_alpha)
{
  const auto output = reconstruct(pixel);
  require(output.size() == expected.size(), "Wrong output sample count");
  for (size_t i = 0; i < output.size(); ++i) {
    close(output[i].depth, expected[i].depth);
    close(output[i].alpha, expected[i].alpha);
  }
  close(1.0 - output_at(output, std::numeric_limits<double>::infinity()), flattened_alpha);
  compare_oracle(pixel);
}

void invalid(const PixelLedger &pixel)
{
  try {
    reconstruct(pixel);
  }
  catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("Invalid input was accepted");
}

void analytic()
{
  expect({0, 0, {{0, 1, true, {}}, {1, 1, true, {}}}}, {}, 0);
  expect({0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {{2, 1}}}}}, {{2, 1}}, 1);
  expect({0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {}}}}, {{2, 0.5}}, 0.5);
  expect({0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {{8, 1}}}}}, {{2, 0.5}, {8, 1}}, 1);
  expect({0, 0, {{0, 1, true, {{2, 0.25}, {8, 0.5}}}}}, {{2, 0.25}, {8, 0.5}}, 0.625);
  expect({0, 0, {{0, 1, true, {{2, 0.25}, {8, 0.5}, {9, 1}, {10, 1}}}}},
         {{2, 0.25}, {8, 0.5}, {9, 1}},
         1);
  expect({0, 0, {{0, 1, true, {{2, 1}}}, {1, 3, true, {}}}}, {{2, 0.25}}, 0.25);
  expect({0, 0, {{0, 1, true, {{2, 0.25}, {2, 0.5}}}}}, {{2, 0.625}}, 0.625);
  expect({0, 0, {{0, 1, true, {{2, 0}}}}}, {}, 0);
  expect({0, 0, {{0, 0, true, {{2, 1}}}, {1, 1, true, {}}}}, {}, 0);
  /* Opacity after full occlusion has no output contribution. */
  expect({-2, 3, {{0, 1, true, {{2, 1}, {8, 0.5}}}}}, {{2, 1}}, 1);
}

void invalid_inputs()
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  invalid({0, 0, {}});
  invalid({0, 0, {{0, 0, true, {}}}});
  invalid({0, 0, {{0, 1, false, {}}}});
  invalid({0, 0, {{0, 1, true, {}}, {0, 1, true, {}}}});
  for (const double weight : {-1.0, nan, inf}) {
    invalid({0, 0, {{0, weight, true, {}}}});
  }
  for (const double z : {-1.0, 0.0, nan, inf}) {
    invalid({0, 0, {{0, 1, true, {{z, 0.5}}}}});
  }
  for (const double alpha : {-0.01, 1.01, nan, inf}) {
    invalid({0, 0, {{0, 1, true, {{2, alpha}}}}});
  }
  /* Validate even ignored zero-weight samples and occluded events. */
  invalid({0, 0, {{0, 1, true, {}}, {1, 0, false, {}}}});
  invalid({0, 0, {{0, 1, true, {}}, {1, 0, true, {{nan, 0}}}}});
  invalid({0, 0, {{0, 1, true, {{1, 1}, {2, nan}}}}});
}

void long_stack()
{
  PixelLedger pixel{0, 0, {{0, 1, true, {}}}};
  for (int i = 1; i <= 2000; ++i) {
    pixel.samples[0].events.push_back({double(i), 1e-5});
  }
  const auto output = reconstruct(pixel);
  require(output.size() == 2000, "Low-opacity boundaries disappeared");
  close(output_at(output, 2000), std::exp(2000 * std::log1p(-1e-5)));
  compare_oracle(pixel);
}

void extreme_weights()
{
  for (const double w : {std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::min(),
                         std::numeric_limits<double>::denorm_min()})
  {
    const auto output = reconstruct({0, 0, {{0, w, true, {{2, 1}}}, {1, w, true, {}}}});
    require(output.size() == 1, "Extreme weight fixture count");
    close(output[0].alpha, 0.5);
  }
}

void randomized()
{
  std::mt19937 rng(20260916);
  for (int trial = 0; trial < 250; ++trial) {
    PixelLedger pixel{0, 0, {}};
    for (uint64_t i = 0; i < 12; ++i) {
      CameraSample sample{i, i == 0 ? 1.0 : double(rng() % 9), true, {}};
      const unsigned count = rng() % 10;
      for (unsigned j = 0; j < count; ++j) {
        sample.events.push_back({double(1 + rng() % 15), double(rng() % 11) / 10.0});
      }
      pixel.samples.push_back(sample);
    }
    compare_oracle(pixel);
    const auto original = reconstruct(pixel);
    std::shuffle(pixel.samples.begin(), pixel.samples.end(), rng);
    for (auto &sample : pixel.samples) {
      std::shuffle(sample.events.begin(), sample.events.end(), rng);
    }
    const auto permuted = reconstruct(pixel);
    require(original.size() == permuted.size(), "Permutation changed sample count");
    for (size_t i = 0; i < original.size(); ++i) {
      require(original[i].depth == permuted[i].depth && original[i].alpha == permuted[i].alpha,
              "Permutation changed canonical reconstruction");
    }
    compare_oracle(pixel);
  }
}

void coverage_limit()
{
  const PixelLedger left{0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {}}}};
  const PixelLedger right{0, 0, {{0, 1, true, {}}, {1, 1, true, {{2, 1}}}}};
  close(reconstruct(left)[0].alpha, reconstruct(right)[0].alpha);
  /* Identical scalar inputs can mean same-half coverage (0.5 union) or
   * opposite-half coverage (1.0 union). Scalar alpha-over yields 0.75. */
  close(1.0 - (1.0 - reconstruct(left)[0].alpha) * (1.0 - reconstruct(right)[0].alpha), 0.75);
  expect({0, 0, {{0, 1, true, {{2, 1}, {3, 1}}}, {1, 1, true, {}}}}, {{2, 0.5}}, 0.5);
  expect({0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {{3, 1}}}}}, {{2, 0.5}, {3, 1}}, 1);
}
}  // namespace

int main()
{
  int failures = 0;
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"analytic fixtures", analytic},
      {"invalid inputs and incomplete chains", invalid_inputs},
      {"long low-opacity stack", long_stack},
      {"extreme finite weights", extreme_weights},
      {"250 random ledgers, oracle and permutations", randomized},
      {"expected scalar coverage limitation", coverage_limit}};
  for (const auto &[name, test] : tests) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    }
    catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << std::setprecision(17) << "Maximum absolute checked error: " << max_error << '\n';
  const auto example = reconstruct({0, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {{8, 1}}}}});
  std::cout << "Disjoint coverage: Z ZBack A\n";
  for (const auto &s : example) {
    std::cout << s.depth << ' ' << s.depth << ' ' << s.alpha << '\n';
  }
  return failures == 0 ? 0 : 1;
}
