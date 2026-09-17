/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/types.h"

/* Called only immediately after camera initialization, before beauty work.
 * The allowlist excludes AOVs, ray tracing and other side-effecting shaders. */
ccl_device int deep_surface_cuda(KernelGlobals kg,
                                 IntegratorState state,
                                 ccl_global float *events,
                                 const int max_events)
{
  if (INTEGRATOR_STATE(state, path, queued_kernel) != DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST ||
      INTEGRATOR_STATE(state, path, bounce) != 0 ||
      !(INTEGRATOR_STATE(state, path, visibility) & PATH_RAY_VISIBILITY_CAMERA))
    return -1;
  Ray ray;
  integrator_state_read_ray(state, &ray);
  ray.self.object = OBJECT_NONE;
  ray.self.prim = PRIM_NONE;
  ray.self.light_object = OBJECT_NONE;
  ray.self.light_prim = PRIM_NONE;
  int count = 0;
  for (;;) {
    Intersection isect;
    isect.object = OBJECT_NONE;
    isect.prim = PRIM_NONE;
    if (!scene_intersect(kg, &ray, PATH_RAY_VISIBILITY_CAMERA, &isect))
      return count;
    if (isect.type != PRIMITIVE_TRIANGLE || (max_events && count == max_events))
      return -1;
    const float depth = transform_point(&kernel_data.cam.worldtocamera, ray.P + ray.D * isect.t).z;
    if (!isfinite(depth) || depth <= 0)
      return -1;
    if (!max_events) {
      events[0] = depth;
      events[1] = 1;
      return 1;
    }
    ShaderData sd;
    shader_setup_from_ray(kg, &sd, &ray, &isect);
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE & ~KERNEL_FEATURE_NODE_RAYTRACE>(
        kg, state, &sd, nullptr,
        INTEGRATOR_STATE(state, path, visibility), INTEGRATOR_STATE(state, path, flag));
    if (sd.runtime_flag & SR_CACHE_MISS)
      return -1;
    const float3 transparency = spectrum_to_rgb(surface_shader_transparency(&sd));
    if (!isfinite(transparency.x) || !isfinite(transparency.y) || !isfinite(transparency.z) ||
        transparency.x < 0 || transparency.x > 1 || transparency.y != transparency.x ||
        transparency.z != transparency.x)
      return -1;
    events[2 * count] = depth;
    events[2 * count + 1] = 1 - transparency.x;
    ++count;
    if (transparency.x == 0)
      return count;
    const float next = intersection_t_offset(isect.t);
    if (!(next > ray.tmin))
      return -1;
    ray.tmin = next;
    ray.self.object = isect.object;
    ray.self.prim = isect.prim;
    INTEGRATOR_STATE_WRITE(state, path, transparent_bounce)++;
    INTEGRATOR_STATE_WRITE(state, path, flag) |= PATH_RAY_TRANSPARENT;
    INTEGRATOR_STATE_WRITE(state, path, rng_offset) += PRNG_BOUNCE_NUM;
  }
}

ccl_gpu_kernel(GPU_KERNEL_BLOCK_NUM_THREADS, GPU_KERNEL_MAX_REGISTERS)
    ccl_gpu_kernel_signature(deep_surface,
                             const ccl_global KernelWorkTile *tiles,
                             const int tile_index,
                             const int offset,
                             const int count,
                             const int max_events,
                             ccl_global float *render_buffer,
                             ccl_global KernelDeepRecord *records)
{
  const int index = ccl_gpu_global_id_x();
  if (index >= count)
    return;
  const ccl_global KernelWorkTile *tile = &tiles[tile_index];
  const int work = offset + index;
  const int state = tile->path_index_offset + work;
  ccl_global KernelDeepRecord *record = &records[index];
  uint x, y, scheduled;
  get_work_pixel(tile, work, &x, &y, &scheduled);
  record->x = x;
  record->y = y;
  record->population = 0;
  if (kernel_data.film.pass_adaptive_aux_buffer != PASS_UNUSED) {
    if (kernel_data.film.pass_sample_count == PASS_UNUSED) {
      record->count = -1;
      return;
    }
    const ccl_global float *pixel = film_pass_pixel_render_buffer(nullptr, state, render_buffer);
    record->population = __float_as_uint(pixel[kernel_data.film.pass_sample_count]);
    /* Camera initialization has finished for this entire batch. The convergence
     * flag has not changed since initialization, so it identifies rejected lanes
     * without inspecting their uninitialized sample/ray fields. A skipped lane
     * is not a completed camera miss. */
    if (!film_need_sample_pixel(nullptr, state, render_buffer)) {
      record->sample = 0;
      record->count = -2;
      return;
    }
  }
  record->sample = INTEGRATOR_STATE(state, path, sample);
  const auto flag = INTEGRATOR_STATE(state, path, flag);
  const auto rng_offset = INTEGRATOR_STATE(state, path, rng_offset);
  const auto transparent_bounce = INTEGRATOR_STATE(state, path, transparent_bounce);
  record->count = deep_surface_cuda(nullptr, state, record->events, max_events);
  /* Restore every field modified by visibility traversal, including failure.
   * Ray, intersection, throughput, queue counters and beauty buffers are untouched. */
  INTEGRATOR_STATE_WRITE(state, path, flag) = flag;
  INTEGRATOR_STATE_WRITE(state, path, rng_offset) = rng_offset;
  INTEGRATOR_STATE_WRITE(state, path, transparent_bounce) = transparent_bounce;
}
ccl_gpu_kernel_postfix
