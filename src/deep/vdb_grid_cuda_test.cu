/* SPDX-License-Identifier: Apache-2.0 */
/* Run on the raw FLOAT grid and independent OpenVDB ray oracle exported by
 * cycles_deep_vdb_grid_test. This is grid qualification, not a deep render. */
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "kernel/device/cuda/compat.h"
#include "kernel/deep/volume_grid.h"

struct AssetRay {
  double origin[3], direction[3], length, expected_tau;
};
struct AssetResult {
  double tau;
  int error, cells;
};
__global__ void integrate_grid(const nanovdb::NanoGrid<float> *grid,
                               const AssetRay *rays,
                               AssetResult *results,
                               KernelDeepEvent *events,
                               KernelDeepDensity *density,
                               const int count)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count)
    return;
  const AssetRay ray = rays[i];
  AssetResult result{};
  DeepGridCursor<double> cursor{};
  if (!deep_grid_begin(&cursor, ray.origin, ray.direction, 0.0, 1.0, 16384)) {
    results[i] = {0, DEEP_ERROR_PROGRESS, 0};
    return;
  }
  nanovdb::CachedReadAccessor<float> accessor(grid->tree().root());
  DeepGridSegment<double> segment{};
  DeepGridStep step;
  while ((step = deep_grid_next(&cursor, &segment)) == DEEP_GRID_SEGMENT) {
    KernelDeepDensity coefficients{};
    result.error = deep_volume_grid_cell(accessor, ray.origin, ray.direction, segment,
                                         .02, ray.length, &coefficients);
    if (result.error != DEEP_ERROR_NONE)
      break;
    for (int coefficient = 0; coefficient < 4; ++coefficient)
      result.tau += double(coefficients.optical_depth[coefficient]) / 4;
    ++result.cells;
  }
  if (result.error == DEEP_ERROR_NONE && step != DEEP_GRID_DONE)
    result.error = step == DEEP_GRID_LIMIT ? DEEP_ERROR_CAPACITY : DEEP_ERROR_PROGRESS;
  if (result.error == DEEP_ERROR_NONE) {
    const auto captured = deep_volume_grid_capture(
        accessor, ray.origin, ray.direction, 0.0, 1.0, .02, ray.length, 1.0, 100.0,
        events + i, density + i, count, DEEP_MAX_VOLUME_EVENTS, 0, 16384);
    if (captured.status != DEEP_COMPLETE)
      result.error = captured.error;
    double stored_tau = 0;
    float previous = 0;
    for (unsigned j = 0; j < captured.count; ++j) {
      const auto &event = events[j * count + i];
      if (event.kind != DEEP_VOLUME_CUBIC || event.front < previous ||
          !(density[j * count + i].back > density[j * count + i].front) ||
          event.back < event.front || event.surface_alpha != 0 || event.optical_depth != 0)
        result.error = DEEP_ERROR_DEPTH;
      previous = event.back;
      for (int k = 0; k < 4; ++k)
        stored_tau += double(density[j * count + i].optical_depth[k]) / 4;
    }
    if (stored_tau != result.tau)
      result.error = DEEP_ERROR_EXTINCTION;
    if (captured.count > 1) {
      const auto overflow = deep_volume_grid_capture(
          accessor, ray.origin, ray.direction, 0.0, 1.0, .02, ray.length, 1.0, 100.0,
          events + i, density + i, count, 1, 0, 16384);
      if (overflow.status != DEEP_FAILED || overflow.count != 0 ||
          overflow.error != DEEP_ERROR_CAPACITY)
        result.error = DEEP_ERROR_STATE;
    }
  }
  results[i] = result;
}
static void check_cuda(cudaError_t error)
{
  if (error != cudaSuccess)
    throw std::runtime_error(cudaGetErrorString(error));
}
int main(int argc, char **argv)
{
  void *device_grid = nullptr;
  AssetRay *device_rays = nullptr;
  AssetResult *device_results = nullptr;
  KernelDeepEvent *device_events = nullptr;
  KernelDeepDensity *device_density = nullptr;
  try {
    if (argc != 3)
      throw std::runtime_error("Usage: vdb_grid_cuda_test FLOAT.nanovdb rays.csv");
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0)
      throw std::runtime_error("Missing native grid buffer");
    std::vector<char> grid(static_cast<size_t>(size));
    input.seekg(0);
    input.read(grid.data(), grid.size());
    if (!input)
      throw std::runtime_error("Incomplete native grid buffer");
    std::ifstream csv(argv[2]);
    std::string line;
    std::getline(csv, line);
    std::vector<AssetRay> rays;
    while (std::getline(csv, line)) {
      std::replace(line.begin(), line.end(), ',', ' ');
      std::istringstream fields(line);
      AssetRay ray{};
      for (double &v : ray.origin)
        fields >> v;
      for (double &v : ray.direction)
        fields >> v;
      fields >> ray.length >> ray.expected_tau;
      if (!fields)
        throw std::runtime_error("Invalid ray oracle row");
      rays.push_back(ray);
    }
    if (rays.empty() || rays.size() > 4096)
      throw std::runtime_error("Invalid oracle ray count");
    std::vector<AssetResult> results(rays.size());
    check_cuda(cudaMalloc(&device_grid, grid.size()));
    check_cuda(cudaMalloc(&device_rays, rays.size() * sizeof(AssetRay)));
    check_cuda(cudaMalloc(&device_results, results.size() * sizeof(AssetResult)));
    check_cuda(cudaMalloc(&device_events, rays.size() * DEEP_MAX_VOLUME_EVENTS * sizeof(KernelDeepEvent)));
    check_cuda(cudaMalloc(&device_density, rays.size() * DEEP_MAX_VOLUME_EVENTS * sizeof(KernelDeepDensity)));
    check_cuda(cudaMemcpy(device_grid, grid.data(), grid.size(), cudaMemcpyHostToDevice));
    check_cuda(cudaMemcpy(device_rays, rays.data(), rays.size() * sizeof(AssetRay), cudaMemcpyHostToDevice));
    integrate_grid<<<(rays.size() + 63) / 64, 64>>>(
        static_cast<const nanovdb::NanoGrid<float> *>(device_grid), device_rays, device_results,
        device_events, device_density, int(rays.size()));
    check_cuda(cudaGetLastError());
    check_cuda(cudaMemcpy(results.data(), device_results, results.size() * sizeof(AssetResult),
                          cudaMemcpyDeviceToHost));
    double maximum = 0;
    size_t cells = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
      const double error = std::abs(std::exp(-results[i].tau) - std::exp(-rays[i].expected_tau));
      if (results[i].error || !(error <= 1e-7))
        throw std::runtime_error("Actual CUDA grid differs from OpenVDB oracle");
      maximum = std::max(maximum, error);
      cells += results[i].cells;
    }
    std::cout << std::setprecision(17) << "PASS: actual CUDA VDB capture and overflow, " << rays.size() << " rays, "
              << cells << " cells; maximum transmittance error " << maximum << '\n';
    check_cuda(cudaFree(device_grid)); device_grid = nullptr;
    check_cuda(cudaFree(device_rays)); device_rays = nullptr;
    check_cuda(cudaFree(device_results)); device_results = nullptr;
    check_cuda(cudaFree(device_events)); device_events = nullptr;
    check_cuda(cudaFree(device_density)); device_density = nullptr;
  }
  catch (const std::exception &error) {
    if (device_grid) cudaFree(device_grid);
    if (device_rays) cudaFree(device_rays);
    if (device_results) cudaFree(device_results);
    if (device_events) cudaFree(device_events);
    if (device_density) cudaFree(device_density);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
