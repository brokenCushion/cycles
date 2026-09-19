/* SPDX-License-Identifier: Apache-2.0 */
/* Native grid-access qualification. Reads the supplied OpenVDB, converts via
 * Cycles' loader utility, and checks the kernel accessor against OpenVDB. */
#include "util/nanovdb.h"
#include <openvdb/io/File.h>
#include <openvdb/tools/Interpolation.h>
#include "kernel/deep/volume_grid.h"
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

int main(int argc, char **argv)
{
  try {
    if (argc != 3)
      throw std::runtime_error("Usage: cycles_deep_vdb_grid_test INPUT.vdb OUTPUT_DIR");
    openvdb::initialize();
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
