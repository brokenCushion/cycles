/* SPDX-License-Identifier: Apache-2.0 */
/* Native grid-access qualification. Reads the supplied OpenVDB, converts via
 * Cycles' loader utility, and checks the kernel accessor against OpenVDB. */
#include "util/nanovdb.h"
#include <openvdb/io/File.h>
#include <openvdb/tools/Interpolation.h>
#include "kernel/deep/volume_grid.h"
#include "deep/volume_compression_test.h"
#include "deep/volume.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

template<typename BuildT> static void test_empty_tiles(const int precision)
{
  auto source = openvdb::FloatGrid::create(0);
  auto writer = source->getAccessor();
  writer.setValue(openvdb::Coord(-8, 0, 0), 2);
  writer.setValue(openvdb::Coord(8, 0, 0), 1);
  /* Inactive nonzero tiles still affect native interpolation. */
  source->fill(openvdb::CoordBBox({128, 0, 0}, {255, 127, 127}), .5f, false);
  auto handle = ccl::openvdb_to_nanovdb(source, precision, 0.0f);
  const auto *native = reinterpret_cast<const ccl::nanovdb::NanoGrid<BuildT> *>(handle.data());
  const ccl::nanovdb::CachedReadAccessor<BuildT> accessor(native->tree().root());
  std::vector<KernelDeepEvent> events(512);
  std::vector<KernelDeepDensity> density(512);
  for (const int sign : {-1, 1}) {
    const double origin[3] = {-500.0 * sign, .25, .25};
    const double direction[3] = {1000.0 * sign, 0, 0};
    const auto result = ccl::deep_volume_grid_capture(accessor, origin, direction, 0.0, 1.0,
        .02, 1000, 1, 1000, events.data(), density.data(), 1, 512, 0, 512);
    if (result.status != DEEP_COMPLETE || !result.count)
      throw std::runtime_error("Empty-tile skip failed in bounded traversal");
    double tau = 0;
    for (unsigned i = 0; i < result.count; ++i)
      for (const float value : density[i].optical_depth)
        tau += double(value) / 4;
    const double expected = .02 * (128 * .5 + 3 * .75 * .75);
    if (std::abs(std::exp(-tau) - std::exp(-expected)) > 1e-7)
      throw std::runtime_error("Empty-tile skip lost interpolation halo or nonzero tile");
  }
}

static void test_half_precision()
{
  auto source = openvdb::FloatGrid::create(0);
  auto writer = source->getAccessor();
  for (int z = 0; z < 9; ++z)
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x)
        writer.setValue(openvdb::Coord(x, y, z), .01f + float(x + 3*y + 7*z) / 113);
  auto handle = ccl::openvdb_to_nanovdb(source, 16, 0.0f);
  const auto *reference = handle.grid<::nanovdb::Fp16>();
  if (!reference)
    throw std::runtime_error("Missing Fp16 test grid");
  const auto *native = reinterpret_cast<const ccl::nanovdb::NanoGrid<ccl::nanovdb::Fp16> *>(handle.data());
  const ccl::nanovdb::CachedReadAccessor<ccl::nanovdb::Fp16> accessor(native->tree().root());
  double max_error = 0;
  for (int cell = 0; cell < 64; ++cell) {
    const int x = cell % 8, y = (cell / 8) % 8, z = (cell * 3) % 8;
    const double origin[3] = {x + .1, y + .2, z + .3};
    const double direction[3] = {.7, .6, .5};
    const double length = std::sqrt(1.1);
    const ccl::DeepGridSegment<double> segment{{x, y, z}, 0, 1};
    KernelDeepDensity result{};
    if (ccl::deep_volume_grid_cell(accessor, origin, direction, segment, .02,
                                  length, &result) != DEEP_ERROR_NONE)
      throw std::runtime_error("Fp16 cell integration failed");
    double expected = 0, actual = 0;
    /* Independent NanoVDB SDK decoding and two-point Gaussian quadrature of
     * the quantized grid's trilinear density, not the original FLOAT grid. */
    for (const double sign : {-1.0, 1.0}) {
      const double t = .5 + sign / (2 * std::sqrt(3.0));
      const double p[3] = {.1 + t*.7, .2 + t*.6, .3 + t*.5};
      for (int corner = 0; corner < 8; ++corner) {
        const int a = corner & 1, b = (corner >> 1) & 1, c = (corner >> 2) & 1;
        const float value = reference->tree().getValue(::nanovdb::Coord(x+a, y+b, z+c));
        expected += value * (a ? p[0] : 1-p[0]) * (b ? p[1] : 1-p[1]) *
                    (c ? p[2] : 1-p[2]) * .5 * .02 * length;
      }
    }
    for (const float value : result.optical_depth)
      actual += double(value) / 4;
    max_error = std::max(max_error, std::abs(std::exp(-actual) - std::exp(-expected)));
  }
  if (max_error > 1e-7)
    throw std::runtime_error("Fp16 integration differs from independent decoded-grid oracle");
  std::cout << "PASS: Fp16, 64 cells; maximum transmittance error " << max_error << '\n';
}

int main(int argc, char **argv)
{
  try {
    if (argc != 3)
      throw std::runtime_error("Usage: cycles_deep_vdb_grid_test INPUT.vdb OUTPUT_DIR");
      openvdb::initialize();
      {
        std::vector<KernelDeepEvent> events(8192);
        std::vector<KernelDeepDensity> density(8192);
        const int failure = ccl::check_volume_compression(events.data(), density.data());
        if (failure)
          throw std::runtime_error("Compression cubic oracle failed: " + std::to_string(failure));
        std::cout << "PASS: compressed curves against independent exact cubic integration\n";
      }
      test_half_precision();
      test_empty_tiles<float>(32);
      test_empty_tiles<ccl::nanovdb::Fp16>(16);
    openvdb::io::File file(argv[1]);
    file.open();
    auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid("density"));
    file.close();
    if (!grid || grid->background() != 0)
      throw std::runtime_error("Expected FLOAT density with zero background");
    auto handle = ccl::openvdb_to_nanovdb(grid, 32, 0.0f);
    if (!handle)
      throw std::runtime_error("Native NanoVDB conversion failed");
    const auto *native = reinterpret_cast<const ccl::nanovdb::NanoGrid<float> *>(handle.data());
    ccl::nanovdb::CachedReadAccessor<float> accessor(native->tree().root());
    const auto box = grid->evalActiveVoxelBoundingBox();
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory);
    std::ofstream binary(directory / "density.float.nanovdb", std::ios::binary);
    binary.write(reinterpret_cast<const char *>(handle.data()), handle.bufferSize());
    binary.close();
    if (!binary)
      throw std::runtime_error("Could not save native grid buffer");
    std::ofstream rays(directory / "rays.csv");
    rays << std::setprecision(17) << "ox,oy,oz,dx,dy,dz,length,expected_tau\n";
    std::mt19937 rng(319);
    std::uniform_real_distribution<double> uniform(.1, .9);
    double maximum_error = 0;
    size_t total_cells = 0, nonempty_rays = 0;
    size_t maximum_records = 0, maximum_intervals = 0;
    std::vector<KernelDeepEvent> events(DEEP_MAX_VOLUME_EVENTS);
    std::vector<KernelDeepDensity> coefficients(DEEP_MAX_VOLUME_EVENTS);
    for (int ray = 0; ray < 128; ++ray) {
      double origin[3], direction[3];
      for (int axis = 0; axis < 3; ++axis) {
        const double extent = box.max()[axis] - box.min()[axis] + 2;
        const double a = axis == ray % 3 ? 0 : uniform(rng);
        const double b = axis == ray % 3 ? 1 : uniform(rng);
        origin[axis] = box.min()[axis] - 1 + extent * a;
        direction[axis] = extent * (b - a);
      }
      const double length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                      direction[2] * direction[2]);
      ccl::DeepGridCursor<double> cursor{};
      if (!ccl::deep_grid_begin(&cursor, origin, direction, 0.0, 1.0, 16384))
        throw std::runtime_error("Grid cursor initialization failed");
      double actual_tau = 0;
      ccl::DeepGridSegment<double> segment{};
      ccl::DeepGridStep status;
      while ((status = ccl::deep_grid_next(&cursor, &segment)) == ccl::DEEP_GRID_SEGMENT) {
        KernelDeepDensity coefficients{};
        if (ccl::deep_volume_grid_cell(accessor, origin, direction, segment, .02, length,
                                      &coefficients) != DEEP_ERROR_NONE)
          throw std::runtime_error("Native density lookup failed");
        for (float value : coefficients.optical_depth)
          actual_tau += double(value) / 4;
        ++total_cells;
      }
      if (status != ccl::DEEP_GRID_DONE)
        throw std::runtime_error("Native traversal did not complete");
      const auto captured = ccl::deep_volume_grid_capture(
          accessor, origin, direction, 0.0, 1.0, .02, length, 1.0, 100.0,
          events.data(), coefficients.data(), 1, DEEP_MAX_VOLUME_EVENTS, 0, 16384);
      if (captured.status != DEEP_COMPLETE)
        throw std::runtime_error("Native cell capture failed");
      maximum_records = std::max(maximum_records, size_t(captured.count));
      std::vector<ccl::deep::CubicDensityInterval> cubic;
      double stored_tau = 0;
      for (unsigned i = 0; i < captured.count; ++i) {
        const auto &b = coefficients[i].optical_depth;
        cubic.push_back({coefficients[i].front, coefficients[i].back, {b[0], b[1], b[2], b[3]}});
        for (float value : b)
          stored_tau += double(value) / 4;
      }
      if (stored_tau != actual_tau)
        throw std::runtime_error("Native capture changed optical depth");
      for (const double eps : {4.95e-5, 4.995e-4}) {
        std::vector<KernelDeepEvent> compressed(8192);
        std::vector<KernelDeepDensity> spans(8192);
        const auto reduced = ccl::deep_volume_grid_capture(
            accessor, origin, direction, 0, 1, .02, length, 1, 100,
            compressed.data(), spans.data(), 1, 8192, 0, 16384, eps);
        if (reduced.status != DEEP_COMPLETE)
          throw std::runtime_error("Compressed asset capture failed: " +
                                   std::to_string(reduced.error));
        for (int probe = 0; probe <= 256; ++probe) {
          const double z = 1 + 100 * double(probe) / 256;
          double exact = 0, approximate = 0;
          for (const auto &span : cubic) {
            double u = (z - span.front) / (span.back - span.front);
            u = std::clamp(u, 0.0, 1.0);
            exact += ccl::compression_exact_tau(span.optical_depth, u);
          }
          for (unsigned i = 0; i < reduced.count; ++i) {
            const auto &span = spans[i];
            const double u = std::clamp((z - span.front) / (span.back - span.front), 0.0, 1.0);
            approximate += u * (double(span.optical_depth[0]) + double(span.optical_depth[1]));
          }
          if (std::abs(std::exp(-exact) - std::exp(-approximate)) > eps)
            throw std::runtime_error("Compressed asset exceeds exact cubic oracle allowance");
        }
      }
      const auto fitted = ccl::deep::integrate_cubic_density(cubic, 1e-7, 65536);
      maximum_intervals = std::max(maximum_intervals, fitted.size());
      /* Independent boundary enumeration + OpenVDB BoxSampler + exact cubic
       * Gaussian quadrature. Does not use the kernel cursor or coefficients. */
      std::vector<double> cuts{0, 1};
      for (int axis = 0; axis < 3; ++axis) {
        if (!direction[axis])
          continue;
        for (int plane = box.min()[axis] - 1; plane <= box.max()[axis] + 1; ++plane) {
          const double t = (plane - origin[axis]) / direction[axis];
          if (t > 0 && t < 1)
            cuts.push_back(t);
        }
      }
      std::sort(cuts.begin(), cuts.end());
      cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
      double expected_tau = 0;
      for (size_t cut = 1; cut < cuts.size(); ++cut) {
        const double middle = (cuts[cut - 1] + cuts[cut]) / 2;
        const double half = (cuts[cut] - cuts[cut - 1]) / 2;
        for (const double sign : {-1.0, 1.0}) {
          const double t = middle + sign * half / std::sqrt(3.0);
          const openvdb::Vec3d p(origin[0] + t * direction[0], origin[1] + t * direction[1],
                                origin[2] + t * direction[2]);
          const double density = openvdb::tools::BoxSampler::sample(grid->tree(), p);
          expected_tau += .02 * length * half * density;
        }
      }
      const double error = std::abs(std::exp(-expected_tau) - std::exp(-actual_tau));
      maximum_error = std::max(maximum_error, error);
      if (!(error <= 1e-7))
        throw std::runtime_error("Native grid transmittance differs from OpenVDB oracle");
      nonempty_rays += expected_tau > 0;
      for (double v : origin)
        rays << v << ',';
      for (double v : direction)
        rays << v << ',';
      rays << length << ',' << expected_tau << '\n';
    }
    rays.close();
    if (!rays || !nonempty_rays)
      throw std::runtime_error("Missing output or no rays intersected density");
    std::cout << std::setprecision(17) << "PASS: actual VDB, 128 rays, " << nonempty_rays
              << " nonempty, " << total_cells << " cells; maximum transmittance error "
              << maximum_error << '\n';
    std::cout << "Capture maximum records " << maximum_records
              << "; fitted intervals at 1e-7 " << maximum_intervals << '\n';
  }
  catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
