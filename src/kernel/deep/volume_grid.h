/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/density.h"
#include "kernel/deep/grid.h"
#include "kernel/deep/types.h"
#include "kernel/util/nanovdb.h"

CCL_NAMESPACE_BEGIN

/* Non-strict per-object compression: absolute transmittance error proof.
 *
 * For an anchor (za, taua), let Ta = exp(-taua). Nonnegative extinction gives
 * |Ta*exp(-u) - Ta*exp(-v)| <= Ta*|u-v| for u,v >= 0. A cell's exact optical
 * depth primitive is quartic: deep_density_chord_error bounds its deviation q
 * from the endpoint chord at EVERY depth by the Bernstein convex-hull property.
 * If a replacement line differs from that cell chord by at most delta at BOTH
 * endpoints, linear interpolation bounds their difference by delta everywhere.
 * Thus Ta*(q+delta) bounds the absolute T error, including cell interiors.
 * Boundary tests alone are insufficient for a curved cell.
 *
 * Allocate eps_object so sum(eps_object) <= eps_ray over all contributing
 * objects. The product telescoping inequality, |product(Tj)-product(Tj')| <=
 * sum(|Tj-Tj'|), proves the ray bound even for overlapping media. Never spend
 * eps_ray independently on each object. Convex camera-sample averaging retains
 * eps_ray. Device compression replaces the host per-ray approximation budget;
 * it must not be charged a second time by the host fitter.
 *
 * After reserving representation error, split the object's remaining allowance
 * equally between cell curvature and boundary displacement. Subdivide a cell
 * until q <= eps_object/(2*Ta); use delta = eps_object/(2*Ta) at its boundaries.
 * This is a conservative instance of delta_i = eps_object/Ta - q_i, avoiding
 * retrospective cone changes when the next cell has greater curvature.
 * Each boundary (zi,taui) intersects the feasible nonnegative slope cone with
 * [(taui-taua-delta)/(zi-za), (taui-taua+delta)/(zi-za)]. Extend only when the
 * exact newest endpoint slope is feasible; otherwise emit the last feasible
 * endpoint and re-anchor. Exact integrated endpoint depths prevent error from
 * accumulating across emitted segments; preserve vacuum gaps explicitly.
 *
 * The proof above is in exact arithmetic. FLOAT tau/depth conversion is not
 * free: bound accumulated endpoint rounding and interior depth displacement,
 * charge them to the reserved allowance, and fail explicitly if it is exceeded.
 * Thin intervals must retain double boundaries until publication; a collapsed
 * FLOAT interval cannot silently be dropped. Subdivision/progress and event
 * capacity failures invalidate the sample. All state is bounded; no allocation.
 * Strict (zero allowance) bypasses compression and retains the cubic path.
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
    const int stride, const int capacity, ccl_private int *count)
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
  events[*count * stride] = {DEEP_VOLUME, float(front), float(back), 0, float(tau)};
  density[*count * stride] = {{high, low, 0, 0}, front, back};
  ++*count;
  return DEEP_ERROR_NONE;
}

struct DeepVolumeCompression {
  double anchor, last, anchor_tau, tau, lower, upper, roundoff;
  bool active;
};

ccl_device KernelDeepError deep_volume_compression_flush(
    ccl_private DeepVolumeCompression *stream, const double rounding_allowance,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count)
{
  if (!stream->active || stream->last == stream->anchor)
    return DEEP_ERROR_NONE;
  const KernelDeepError error = deep_volume_constant(
      stream->anchor, stream->last, stream->tau - stream->anchor_tau,
      rounding_allowance - stream->roundoff, events, density, stride, capacity, count);
  stream->anchor = stream->last;
  stream->anchor_tau = stream->tau;
  stream->lower = 0;
  stream->upper = 1.7976931348623157e308;
  stream->roundoff = 0;
  return error;
}

/* Restrict/subdivide the original cubic, rather than sampling a fitted curve.
 * A rejected vertex is replayed after flushing, so no extinction is lost. */
ccl_device KernelDeepError deep_volume_compression_cell(
    ccl_private DeepVolumeCompression *stream, const KernelDeepDensity cell,
    const double allowance, const double rounding_allowance,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count)
{
  if (stream->active && cell.front != stream->last) {
    const KernelDeepError error = deep_volume_compression_flush(
        stream, rounding_allowance, events, density, stride, capacity, count);
    if (error != DEEP_ERROR_NONE)
      return error;
    stream->active = false;
  }
  if (!stream->active) {
    stream->anchor = stream->last = cell.front;
    /* Vacuum changes depth, never the optical-depth prefix. Keeping this prefix
     * also avoids over-refining cells behind already absorbed density. */
    stream->anchor_tau = stream->tau;
    stream->lower = 0;
    stream->upper = 1.7976931348623157e308;
    stream->active = true;
  }
  const DeepCubicDensity<double> original = {{cell.optical_depth[0], cell.optical_depth[1],
                                             cell.optical_depth[2], cell.optical_depth[3]}};
  double u = 0, width = 1;
  for (int steps = 0; u < 1; ++steps) {
    if (steps == 16384)
      return DEEP_ERROR_GRID_STEPS;
    const double v = u + width < 1 ? u + width : 1;
    const double end = v == 1 ? cell.back : cell.front + v * (cell.back - cell.front);
    if (!(v > u && end > stream->last))
      return DEEP_ERROR_PROGRESS;
    const auto piece = deep_density_restrict(original, u, v);
    const double delta = allowance / (2 * ::exp(-stream->anchor_tau));
    if (deep_density_chord_error(piece, v - u) > delta) {
      width /= 2;
      continue;
    }
    const double increment = deep_density_integral(piece, v - u);
    const double tau = stream->tau + increment;
    const double distance = end - stream->anchor;
    double lower = (tau - stream->anchor_tau - delta) / distance;
    double upper = (tau - stream->anchor_tau + delta) / distance;
    lower = lower > stream->lower ? lower : stream->lower;
    upper = upper < stream->upper ? upper : stream->upper;
    const double slope = (tau - stream->anchor_tau) / distance;
    if (slope < lower || slope > upper) {
      const KernelDeepError error = deep_volume_compression_flush(
          stream, rounding_allowance, events, density, stride, capacity, count);
      if (error != DEEP_ERROR_NONE)
        return error;
      continue;
    }
    stream->lower = lower;
    stream->upper = upper;
    stream->last = end;
    stream->tau = tau;
    stream->roundoff += 64 * 2.2204460492503131e-16 *
                       (1 + tau + original.b[0] + original.b[1] +
                        original.b[2] + original.b[3]);
    u = v;
    /* ponytail: keep the accepted subdivision width for this cell; grow it
     * only in a later optimization if subdivision work is measured significant. */
  }
  return DEEP_ERROR_NONE;
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
    const double eps_ray = 0)
{
  if (!events || !density || stride <= 0 || capacity <= 0 || first < 0 || first > capacity ||
      !(depth_per_t > 0 && physical_length_per_t > 0 && extinction_scale >= 0))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  DeepGridCursor<double> cursor{};
  if (!deep_grid_begin(&cursor, origin, direction, start, end, traversal_limit))
    return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
  int count = first;
  DeepVolumeCompression compressed{};
  /* Reserve 1/4 for arithmetic/record rounding. The ray can visit at most
   * DEEP_MAX_MEDIA objects; allocating equally is conservative for sparse rays.
   * Exact endpoint integration means disconnected spans do not compound the
   * curvature error; rounding is charged per emitted event across the ray. */
  const double eps_object = .75 * eps_ray / DEEP_MAX_MEDIA;
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
      continue;
    }
    events[count * stride] = {DEEP_VOLUME_CUBIC, float(a), float(b), 0, 0};
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
