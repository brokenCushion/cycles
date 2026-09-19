/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/types.h"
#include "kernel/util/nanovdb.h"

CCL_NAMESPACE_BEGIN

/* Initial numerical qualification is CPU/CUDA double evaluation followed by
 * FLOAT coefficients. The stored layout does not require device double support.
 * Other backends must qualify their arithmetic before enabling this path. */
#if !defined(__KERNEL_GPU__) || defined(__KERNEL_CUDA__)
template<typename BuildT>
ccl_device KernelDeepError deep_volume_grid_cell(
    const ccl_private nanovdb::CachedReadAccessor<BuildT> &accessor,
    const ccl_private double *origin,
    const ccl_private double *direction,
    const DeepGridSegment<double> segment,
    const double extinction_scale,
    const double physical_length_per_t,
    ccl_private KernelDeepDensity *output)
{
  double corners[8], start[3], end[3];
  for (int axis = 0; axis < 3; ++axis) {
    const double a = origin[axis] + segment.front * direction[axis] - segment.cell[axis];
    const double b = origin[axis] + segment.back * direction[axis] - segment.cell[axis];
    /* Only roundoff at an already traversed boundary may be clamped. Do not
     * hide a wrong cell or a ray that lost sub-voxel precision. */
    if (!(a >= -1e-9 && a <= 1 + 1e-9 && b >= -1e-9 && b <= 1 + 1e-9))
      return DEEP_ERROR_PROGRESS;
    start[axis] = a < 0 ? 0 : (a > 1 ? 1 : a);
    end[axis] = b < 0 ? 0 : (b > 1 ? 1 : b);
  }
  for (int corner = 0; corner < 8; ++corner) {
    const int3 coordinate = make_int3(segment.cell[0] + (corner & 1),
                                      segment.cell[1] + ((corner >> 1) & 1),
                                      segment.cell[2] + ((corner >> 2) & 1));
    const float value = accessor.getValue(coordinate);
    /* Nonnegative scalar density is part of the allowlisted contract. A
     * negative corner cannot be replaced by a clamped cubic without changing
     * the native shader's density function inside this cell. */
    if (!(value >= 0 && value <= 3.4028234663852886e38))
      return DEEP_ERROR_EXTINCTION;
    corners[corner] = value;
  }
  const double length = (segment.back - segment.front) * physical_length_per_t;
  if (!(segment.back > segment.front && physical_length_per_t > 0 && length > 0 &&
        extinction_scale >= 0))
    return DEEP_ERROR_EXTINCTION;
  const auto density = deep_trilinear_density(corners, start, end);
  for (int i = 0; i < 4; ++i) {
    const double coefficient = density.b[i] * extinction_scale * length;
    if (!(coefficient >= 0 && coefficient <= 3.4028234663852886e38))
      return DEEP_ERROR_EXTINCTION;
    output->optical_depth[i] = float(coefficient);
  }
  return DEEP_ERROR_NONE;
}

/* Append cell records to host-allocated lane buffers. The grid-space ray keeps
 * the world ray parameter; depth_origin + t * depth_per_t converts it to axial
 * camera depth. Physical extinction uses world length, never grid length.
 * Failure invalidates the whole camera sample, including any earlier writes. */
template<typename BuildT>
ccl_device KernelDeepResult deep_volume_grid_capture(
    const ccl_private nanovdb::CachedReadAccessor<BuildT> &accessor,
    const ccl_private double *origin,
    const ccl_private double *direction,
    const double start,
    const double end,
    const double extinction_scale,
    const double physical_length_per_t,
    const double depth_origin,
    const double depth_per_t,
    ccl_global KernelDeepEvent *events,
    ccl_global KernelDeepDensity *density,
    const int stride,
    const int capacity,
    const int first,
    const int traversal_limit)
{
  if (!events || !density || stride <= 0 || capacity <= 0 || first < 0 || first > capacity ||
      !(depth_per_t > 0))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  DeepGridCursor<double> cursor{};
  if (!deep_grid_begin(&cursor, origin, direction, start, end, traversal_limit))
    return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
  int count = first;
  DeepGridSegment<double> segment{};
  DeepGridStep step;
  while ((step = deep_grid_next(&cursor, &segment)) == DEEP_GRID_SEGMENT) {
    KernelDeepDensity coefficients{};
    const KernelDeepError error = deep_volume_grid_cell(
        accessor, origin, direction, segment, extinction_scale, physical_length_per_t, &coefficients);
    if (error != DEEP_ERROR_NONE)
      return {DEEP_FAILED, 0, error};
    bool occupied = false;
    for (int i = 0; i < 4; ++i)
      occupied |= coefficients.optical_depth[i] > 0;
    if (!occupied)
      continue;
    if (count == capacity)
      return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
    const double a = depth_origin + segment.front * depth_per_t;
    const double b = depth_origin + segment.back * depth_per_t;
    if (!(a > 0 && b <= 3.4028234663852886e38 && b > a))
      return {DEEP_FAILED, 0, DEEP_ERROR_DEPTH};
    coefficients.front = a;
    coefficients.back = b;
    events[count * stride] = {DEEP_VOLUME_CUBIC, float(a), float(b), 0, 0};
    density[count * stride] = coefficients;
    ++count;
  }
  if (step != DEEP_GRID_DONE)
    return {DEEP_FAILED, 0, step == DEEP_GRID_LIMIT ? DEEP_ERROR_CAPACITY : DEEP_ERROR_PROGRESS};
  return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
}
#endif

CCL_NAMESPACE_END
