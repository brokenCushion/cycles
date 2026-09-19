/* SPDX-License-Identifier: Apache-2.0 */
/* Standalone CUDA runtime qualification for the shared cell math. This does
 * not claim native VDB traversal or full-renderer CUDA qualification. */
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include "kernel/device/cuda/compat.h"
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/volume_grid.h"

/* Compile the real native accessor instantiations as well as the runtime math
 * tests below. This entry is not launched without a valid NanoVDB grid. */
template<typename T>
__global__ void read_native_cell(const nanovdb::NanoGrid<T> *grid,
                                KernelDeepDensity *output,
                                KernelDeepError *error)
{
  nanovdb::CachedReadAccessor<T> accessor(grid->tree().root());
  const double origin[] = {0, 0, 0}, direction[] = {1, 0, 0};
  const DeepGridSegment<double> segment{{0, 0, 0}, 0, 1};
  *error = deep_volume_grid_cell(accessor, origin, direction, segment, 1.0, 1.0, output);
}
template __global__ void read_native_cell<float>(const nanovdb::NanoGrid<float> *,
                                                 KernelDeepDensity *, KernelDeepError *);
template __global__ void read_native_cell<nanovdb::Fp16>(const nanovdb::NanoGrid<nanovdb::Fp16> *,
                                                        KernelDeepDensity *, KernelDeepError *);
template __global__ void read_native_cell<nanovdb::FpN>(const nanovdb::NanoGrid<nanovdb::FpN> *,
                                                       KernelDeepDensity *, KernelDeepError *);

struct CellCase {
  double corners[8], start[3], end[3], length;
};
struct CellResult {
  double integral, partial, value, bound;
  int grid_count, grid_status;
  unsigned long long grid_hash;
};
__global__ void evaluate_cells(const CellCase *cases, CellResult *results, const int count)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count)
    return;
  const CellCase input = cases[i];
  const auto curve = deep_trilinear_density(input.corners, input.start, input.end);
  const auto partial = deep_density_restrict(curve, 0.0, .37);
  results[i] = {deep_density_integral(curve, input.length),
                deep_density_integral(partial, input.length * .37),
                deep_density_blossom(curve, .37, .37, .37),
                deep_density_chord_error(curve, input.length)};
  double origin[3], direction[3];
  for (int axis = 0; axis < 3; ++axis) {
    origin[axis] = 8 * input.start[axis] - 4;
    direction[axis] = 5 * (input.end[axis] - input.start[axis]);
  }
  DeepGridCursor<double> cursor{};
  if (!deep_grid_begin(&cursor, origin, direction, .125, 2.5, 64)) {
    results[i].grid_status = DEEP_GRID_INVALID;
    return;
  }
  DeepGridSegment<double> segment{};
  unsigned long long hash = 14695981039346656037ull;
  int segments = 0;
  DeepGridStep step;
  while ((step = deep_grid_next(&cursor, &segment)) == DEEP_GRID_SEGMENT) {
    for (int axis = 0; axis < 3; ++axis)
      hash = (hash ^ static_cast<unsigned int>(segment.cell[axis])) * 1099511628211ull;
    ++segments;
  }
  results[i].grid_count = segments;
  results[i].grid_status = step;
  results[i].grid_hash = hash;
}

static void cuda_check(const cudaError_t error)
{
  if (error != cudaSuccess)
    throw std::runtime_error(cudaGetErrorString(error));
}
static double density(const CellCase &cell, const double t)
{
  double result = 0;
  for (int corner = 0; corner < 8; ++corner) {
    double value = cell.corners[corner];
    for (int axis = 0; axis < 3; ++axis) {
      const double p = cell.start[axis] + t * (cell.end[axis] - cell.start[axis]);
      value *= (corner & (1 << axis)) ? p : 1 - p;
    }
    result += value;
  }
  return result;
}
static double integral(const CellCase &cell, const double t)
{
  const double middle = t / 2, delta = middle / std::sqrt(3.0);
  return cell.length * middle * (density(cell, middle - delta) + density(cell, middle + delta));
}
int main()
{
  CellCase *device_cases = nullptr;
  CellResult *device_results = nullptr;
  try {
    constexpr int count = 512;
    std::vector<CellCase> cases(count);
    std::vector<CellResult> results(count);
    std::mt19937 rng(7123);
    std::uniform_real_distribution<double> uniform(0, 1);
    for (int i = 0; i < count; ++i) {
      for (double &corner : cases[i].corners)
        corner = i < 2 ? double(i) : 4 * uniform(rng);
      for (int axis = 0; axis < 3; ++axis) {
        cases[i].start[axis] = uniform(rng);
        cases[i].end[axis] = uniform(rng);
      }
      cases[i].length = 10 * uniform(rng);
    }
    for (int axis = 0; axis < 3; ++axis) {
      cases[0].start[axis] = 0;
      cases[0].end[axis] = 1;
      cases[1].start[axis] = 1;
      cases[1].end[axis] = axis == 0 ? 0 : 1;
    }
    cuda_check(cudaMalloc(&device_cases, sizeof(CellCase) * count));
    cuda_check(cudaMalloc(&device_results, sizeof(CellResult) * count));
    cuda_check(cudaMemcpy(device_cases, cases.data(), sizeof(CellCase) * count, cudaMemcpyHostToDevice));
    evaluate_cells<<<4, 128>>>(device_cases, device_results, count);
    cuda_check(cudaGetLastError());
    cuda_check(cudaMemcpy(results.data(), device_results, sizeof(CellResult) * count,
                          cudaMemcpyDeviceToHost));
    double maximum = 0;
    for (int i = 0; i < count; ++i) {
      double origin[3], direction[3];
      std::vector<double> boundaries{.125, 2.5};
      for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = 8 * cases[i].start[axis] - 4;
        direction[axis] = 5 * (cases[i].end[axis] - cases[i].start[axis]);
        if (!direction[axis])
          continue;
        for (int plane = -20; plane <= 20; ++plane) {
          const double t = (plane - origin[axis]) / direction[axis];
          if (t > .125 && t < 2.5)
            boundaries.push_back(t);
        }
      }
      std::sort(boundaries.begin(), boundaries.end());
      boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
      unsigned long long expected_hash = 14695981039346656037ull;
      for (size_t j = 1; j < boundaries.size(); ++j) {
        const double t = (boundaries[j - 1] + boundaries[j]) / 2;
        for (int axis = 0; axis < 3; ++axis) {
          const int cell = int(std::floor(origin[axis] + t * direction[axis]));
          expected_hash = (expected_hash ^ static_cast<unsigned int>(cell)) * 1099511628211ull;
        }
      }
      if (results[i].grid_status != DEEP_GRID_DONE ||
          results[i].grid_count != int(boundaries.size() - 1) ||
          results[i].grid_hash != expected_hash)
        throw std::runtime_error("CUDA grid traversal differs from integer-plane oracle");
      for (double error : {std::abs(results[i].integral - integral(cases[i], 1)),
                           std::abs(results[i].partial - integral(cases[i], .37)),
                           std::abs(results[i].value - density(cases[i], .37))})
      {
        maximum = std::max(maximum, error);
        if (!(error < 2e-13))
          throw std::runtime_error("CUDA cell differs from corner/quadrature oracle");
      }
      for (int probe = 0; probe <= 100; ++probe) {
        const double t = probe / 100.0;
        if (!(std::abs(integral(cases[i], t) - t * results[i].integral) <=
              results[i].bound + 2e-13))
          throw std::runtime_error("CUDA cell chord bound failed");
      }
    }
    cuda_check(cudaFree(device_cases));
    device_cases = nullptr;
    cuda_check(cudaFree(device_results));
    device_results = nullptr;
    std::cout << "PASS: 512 CUDA rays/cells, 101 probes each; maximum oracle error " << maximum << '\n';
  }
  catch (const std::exception &error) {
    if (device_cases)
      cudaFree(device_cases);
    if (device_results)
      cudaFree(device_results);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
