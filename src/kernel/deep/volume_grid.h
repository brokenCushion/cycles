/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/types.h"
#include "kernel/util/nanovdb.h"

CCL_NAMESPACE_BEGIN

/* Non-strict no-expansion bound (absolute transmittance).
 * Host density fitting and device capture each receive half the density budget.
 * Host preflight supplies N=min(scene volume objects, DEEP_MAX_MEDIA), N>=1;
 * eps_object=eps_ray/N. Product telescoping bounds overlapping media by the
 * sum of object errors; convex camera averaging preserves eps_ray.
 * Reserve 1/4 of eps_ray for record/integration roundoff, charged per record.
 * For an exact optical prefix taua, Ta=exp(-taua), nonnegative extinction gives
 * |Ta exp(-u)-Ta exp(-v)| <= Ta |u-v|. The Bernstein hull bounds each cell's
 * quartic primitive's chord error q at EVERY interior depth. Require
 * q<=delta=.75*eps_object/(2*Ta), and intersect the boundary slope cones with
 * +/-delta. Then Ta*(q+delta)<=.75*eps_object. Exact integrated endpoints
 * prevent accumulation across segments. Vacuum retains the object's prefix.
 * Carry a conservative prefix roundoff bound: use exp(-tau+prefix_error)
 * for both slope allowances and the termination test, never an optimistic T.
 * Curvature rejection emits the original cubic unchanged: no subdivision.
 * A singleton also stays cubic; linear spans replace at least two records.
 * Host fits retained cubics within its separate half of the density budget.
 * Once exact T_obj<=.75*eps_object, replacing the subsequent tail by opacity
 * has error <=.75*eps_object. This is disjoint from the earlier curve error,
 * so their maximum, not their sum, is charged. Use the next occupied cell's
 * record slot for opacity: no expansion, even without any successful merge.
 * Round its depth upward; attenuation is already bounded before that depth.
 * Clamp later objects to this cutoff. All failures invalidate the sample.
 * Strict bypasses this path, retaining every original byte and operation.
 */

/* Initial numerical qualification is CPU/CUDA double evaluation followed by
 * FLOAT coefficients. The stored layout does not require device double support.
 * Other backends must qualify their arithmetic before enabling this path. */
#if !defined(__KERNEL_GPU__) || defined(__KERNEL_CUDA__)
/* A compressed native event keeps double boundaries and a two-FLOAT,
 * nonnegative optical-depth expansion in the existing companion buffer.
 * This avoids spending the publication precision allowance during capture. */
ccl_device KernelDeepError deep_volume_constant(
    const double front, const double back, const double tau, const double rounding_allowance,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count, const int object = -1)
{
  if (*count == capacity)
    return DEEP_ERROR_EVENT_CAPACITY;
  if (!(front > 0 && back > front && back <= 3.4028234663852886e38 &&
        tau >= 0 && tau <= 3.4028234663852886e38))
    return DEEP_ERROR_DEPTH;
  float high = float(tau);
  if (double(high) > tau)
    high = nextafterf(high, 0.0f);
  const float low = float(tau - double(high));
  const double stored = double(high) + double(low);
  /* Include double addition/integration roundoff, not just FLOAT conversion.
   * Each event gets 1/capacity of the ray's representation reserve, so the
   * sum remains bounded even across objects and disconnected intervals. */
  if (!(::fabs(stored - tau) + 32 * 2.2204460492503131e-16 * (1 + tau) <=
        rounding_allowance))
    return DEEP_ERROR_EXTINCTION;
  events[*count * stride] = {DEEP_VOLUME, float(front), float(back), 0, float(tau), object};
  density[*count * stride] = {{high, low, 0, 0}, front, back};
  ++*count;
  return DEEP_ERROR_NONE;
}

ccl_device KernelDeepError deep_volume_exact(
    const KernelDeepDensity cell, ccl_global KernelDeepEvent *events,
    ccl_global KernelDeepDensity *density, const int stride, const int capacity,
    ccl_private int *count, const int object = -1)
{
  if (*count == capacity)
    return DEEP_ERROR_EVENT_CAPACITY;
  events[*count * stride] = {DEEP_VOLUME_CUBIC, float(cell.front), float(cell.back), 0, 0, object};
  density[*count * stride] = cell;
  ++*count;
  return DEEP_ERROR_NONE;
}

ccl_device KernelDeepError deep_volume_compression_flush(
    ccl_private DeepVolumeCompression *stream, const double rounding_allowance,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count)
{
  if (!stream->active)
    return DEEP_ERROR_NONE;
  const KernelDeepError error = stream->cells == 1 ?
      deep_volume_exact(stream->singleton, events, density, stride, capacity, count, stream->object) :
      deep_volume_constant(stream->anchor, stream->last, stream->tau - stream->anchor_tau,
          rounding_allowance - stream->roundoff, events, density, stride, capacity, count, stream->object);
  stream->active = false;
  stream->cells = 0;
  return error;
}

ccl_device KernelDeepError deep_volume_compression_cell(
    ccl_private DeepVolumeCompression *stream, const KernelDeepDensity cell,
    const double allowance, const double rounding_allowance,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count)
{
  if (stream->terminated)
    return DEEP_ERROR_STATE;
  if (stream->active && cell.front != stream->last) {
    const auto error = deep_volume_compression_flush(
        stream, rounding_allowance, events, density, stride, capacity, count);
    if (error != DEEP_ERROR_NONE)
      return error;
  }
  if (::exp(-stream->tau + stream->prefix_error) <= allowance) {
    const auto error = deep_volume_compression_flush(
        stream, rounding_allowance, events, density, stride, capacity, count);
    if (error != DEEP_ERROR_NONE)
      return error;
    if (*count == capacity)
      return DEEP_ERROR_EVENT_CAPACITY;
    float z = float(cell.front);
    if (double(z) < cell.front)
      z = nextafterf(z, FLT_MAX);
    events[*count * stride] = {DEEP_SURFACE, z, z, 1, 0, stream->object};
    ++*count;
    stream->cutoff = z;
    stream->terminated = true;
    return DEEP_ERROR_NONE;
  }
  const DeepCubicDensity<double> original = {{cell.optical_depth[0], cell.optical_depth[1],
                                             cell.optical_depth[2], cell.optical_depth[3]}};
  const double increment = deep_density_integral(original, 1.0);
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!stream->active) {
      stream->anchor = stream->last = cell.front;
      stream->anchor_tau = stream->tau;
      stream->anchor_error = stream->prefix_error;
      stream->lower = 0;
      stream->upper = 1.7976931348623157e308;
      stream->roundoff = 0;
      stream->cells = 0;
    }
    const double delta = allowance / (2 * ::exp(-stream->anchor_tau + stream->anchor_error));
    if (deep_density_chord_error(original, 1.0) > delta) {
      const auto error = deep_volume_compression_flush(
          stream, rounding_allowance, events, density, stride, capacity, count);
      if (error != DEEP_ERROR_NONE)
        return error;
      const auto exact = deep_volume_exact(cell, events, density, stride, capacity, count, stream->object);
      stream->tau += increment;
      stream->prefix_error += 64 * 2.2204460492503131e-16 * (1 + stream->tau + increment);
      return exact;
    }
    const double tau = stream->tau + increment;
    const double distance = cell.back - stream->anchor;
    double lower = (tau - stream->anchor_tau - delta) / distance;
    double upper = (tau - stream->anchor_tau + delta) / distance;
    lower = lower > stream->lower ? lower : stream->lower;
    upper = upper < stream->upper ? upper : stream->upper;
    const double slope = (tau - stream->anchor_tau) / distance;
    if (slope < lower || slope > upper) {
      const auto error = deep_volume_compression_flush(
          stream, rounding_allowance, events, density, stride, capacity, count);
      if (error != DEEP_ERROR_NONE)
        return error;
      continue;
    }
    stream->lower = lower;
    stream->upper = upper;
    stream->last = cell.back;
    stream->tau = tau;
    stream->prefix_error += 64 * 2.2204460492503131e-16 * (1 + tau + increment);
    stream->roundoff += 64 * 2.2204460492503131e-16 *
                       (1 + tau + original.b[0] + original.b[1] +
                        original.b[2] + original.b[3]);
    if (!stream->cells)
      stream->singleton = cell;
    ++stream->cells;
    stream->active = true;
    return DEEP_ERROR_NONE;
  }
  return DEEP_ERROR_PROGRESS;
}

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
    const int traversal_limit,
    const double eps_ray = 0,
    const int volume_objects = DEEP_MAX_MEDIA,
    ccl_private DeepVolumeCompression *object_stream = nullptr,
    const int object = -1)
{
  if (!events || !density || stride <= 0 || capacity <= 0 || first < 0 || first > capacity ||
      !(depth_per_t > 0 && physical_length_per_t > 0 && extinction_scale >= 0))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  DeepGridCursor<double> cursor{};
  if (!deep_grid_begin(&cursor, origin, direction, start, end, traversal_limit))
    return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
  int count = first;
  DeepVolumeCompression local_stream{};
  local_stream.object = object;
  if (object_stream && object_stream->object != object)
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  DeepVolumeCompression &compressed = object_stream ? *object_stream : local_stream;
  if (eps_ray > 0 && !(volume_objects >= 1 && volume_objects <= DEEP_MAX_MEDIA))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  const double eps_object = .75 * eps_ray / volume_objects;
  const double eps_record = .25 * eps_ray / capacity;
  DeepGridSegment<double> segment{};
  DeepGridStep step;
  while (true) {
    if (cursor.current < cursor.end && cursor.remaining > 0) {
      int dimension;
      const int3 cell = make_int3(cursor.cell[0], cursor.cell[1], cursor.cell[2]);
      if (accessor.getValue(cell, &dimension) == 0 && dimension > 1) {
        /* A constant zero tile proves empty interpolation only while all eight
         * corners remain inside it. Keep the one-cell halo at each tile edge. */
        double exit = cursor.end;
        bool interior = true;
        for (int axis = 0; axis < 3; ++axis) {
          const int lower = cursor.cell[axis] & ~(dimension - 1);
          interior &= cursor.cell[axis] + 1 < lower + dimension;
          if (cursor.step[axis]) {
            const int plane = cursor.step[axis] > 0 ? lower + dimension - 1 : lower;
            const double crossing = (double(plane) - origin[axis]) / direction[axis];
            exit = exit < crossing ? exit : crossing;
          }
        }
        if (interior && exit > cursor.current) {
          if (exit == cursor.end) {
            if (eps_ray > 0) {
              const auto error = deep_volume_compression_flush(
                  &compressed, eps_record, events, density, stride, capacity, &count);
              if (error != DEEP_ERROR_NONE)
                return {DEEP_FAILED, 0, error};
            }
            return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
          }
          const int remaining = cursor.remaining - 1;
          if (!remaining)
            return {DEEP_FAILED, 0, DEEP_ERROR_GRID_STEPS};
          if (!deep_grid_begin(&cursor, origin, direction, exit, end, remaining))
            return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
          continue;
        }
      }
    }
    step = deep_grid_next(&cursor, &segment);
    if (step != DEEP_GRID_SEGMENT)
      break;
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
    if (eps_ray == 0 && count == capacity)
      return {DEEP_FAILED, 0, DEEP_ERROR_EVENT_CAPACITY};
    const double a = depth_origin + segment.front * depth_per_t;
    const double b = depth_origin + segment.back * depth_per_t;
    if (!(a > 0 && b <= 3.4028234663852886e38 && b > a))
      return {DEEP_FAILED, 0, DEEP_ERROR_DEPTH};
    coefficients.front = a;
    coefficients.back = b;
    if (eps_ray > 0) {
      const auto error = deep_volume_compression_cell(
          &compressed, coefficients, eps_object, eps_record,
          events, density, stride, capacity, &count);
      if (error != DEEP_ERROR_NONE)
        return {DEEP_FAILED, 0, error};
      if (compressed.terminated)
        return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
      continue;
    }
    events[count * stride] = {DEEP_VOLUME_CUBIC, float(a), float(b), 0, 0, object};
    density[count * stride] = coefficients;
    ++count;
  }
  if (step != DEEP_GRID_DONE)
    return {DEEP_FAILED, 0, step == DEEP_GRID_LIMIT ? DEEP_ERROR_GRID_STEPS : DEEP_ERROR_PROGRESS};
  if (eps_ray > 0) {
    const auto error = deep_volume_compression_flush(
        &compressed, eps_record, events, density, stride, capacity, &count);
    if (error != DEEP_ERROR_NONE)
      return {DEEP_FAILED, 0, error};
  }
  return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
}
#endif

CCL_NAMESPACE_END
