/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/camera_depth.h"
#include "kernel/deep/surface_boundary.h"
#include "kernel/deep/types.h"
#include "kernel/deep/volume.h"

/* Called only immediately after camera initialization, before beauty work.
 * The allowlist excludes AOVs, ray tracing and other side-effecting shaders. */
ccl_device KernelDeepResult deep_surface_cuda(KernelGlobals kg,
                                              IntegratorState state,
                                              ccl_global KernelDeepEvent *events,
                                              const int event_stride,
                                              const int max_events,
                                              ccl_private KernelDeepWriteState *write = nullptr)
{
  if (INTEGRATOR_STATE(state, path, queued_kernel) != DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST ||
      INTEGRATOR_STATE(state, path, bounce) != 0 ||
      !(INTEGRATOR_STATE(state, path, visibility) & PATH_RAY_VISIBILITY_CAMERA))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  Ray ray;
  integrator_state_read_ray(state, &ray);
  ray.self.object = OBJECT_NONE;
  ray.self.prim = PRIM_NONE;
  ray.self.light_object = OBJECT_NONE;
  ray.self.light_prim = PRIM_NONE;
  int count = 0;
  Intersection previous;
  bool previous_backfacing = false;
  for (;;) {
    Intersection isect;
    isect.object = OBJECT_NONE;
    isect.prim = PRIM_NONE;
    if (!scene_intersect(kg, &ray, PATH_RAY_VISIBILITY_CAMERA, &isect))
      return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
    if (isect.type != PRIMITIVE_TRIANGLE)
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_PRIMITIVE};
    const float depth = deep_camera_depth(
        kernel_data.cam, kernel_data_array(camera_motion), ray.time, ray.P + ray.D * isect.t);
    if (!isfinite(depth) || depth <= 0)
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_DEPTH};
    if (!max_events) {
      deep_write_event(events, nullptr, 0,
          {deep_event_pack(DEEP_SURFACE, isect.object), depth, depth, 1, 0}, nullptr, write);
      return {DEEP_COMPLETE, 1, DEEP_ERROR_NONE};
    }
    ShaderDataTinyStorage storage;
    ShaderData &sd = *AS_SHADER_DATA(&storage);
    shader_setup_from_ray(kg, &sd, &ray, &isect);
    const bool backfacing = (sd.runtime_flag & SR_BACKFACING) != 0;
    if (count && deep_same_surface_boundary(kg, previous, isect, previous_backfacing, backfacing))
    {
      ray.tmin = intersection_t_offset(isect.t);
      ray.self.object = isect.object;
      ray.self.prim = isect.prim;
      continue;
    }
    if (count == max_events)
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_CAPACITY};
    previous = isect;
    previous_backfacing = backfacing;
    /* Transparency is accumulated independently of closures. Keep camera
     * visibility and flags unchanged while omitting unused BSDF storage. */
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE & ~KERNEL_FEATURE_NODE_RAYTRACE, false>(
        kg,
        state,
        &sd,
        nullptr,
        INTEGRATOR_STATE(state, path, visibility),
        INTEGRATOR_STATE(state, path, flag));
    if (sd.runtime_flag & SR_CACHE_MISS)
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_CACHE_MISS};
    const float3 transparency = spectrum_to_rgb(surface_shader_transparency(&sd));
    if (!isfinite(transparency.x) || !isfinite(transparency.y) || !isfinite(transparency.z) ||
        transparency.x < 0 || transparency.x > 1 || transparency.y != transparency.x ||
        transparency.z != transparency.x)
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_EXTINCTION};
    deep_write_event(events, nullptr, count * event_stride,
        {deep_event_pack(DEEP_SURFACE, isect.object), depth, depth, 1 - transparency.x, 0}, nullptr, write);
    ++count;
    if (transparency.x == 0)
      return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
    const float next = intersection_t_offset(isect.t);
    if (!(next > ray.tmin))
      return {DEEP_FAILED, unsigned(count), DEEP_ERROR_PROGRESS};
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
                             const int event_stride,
                             ccl_global float *render_buffer,
                             ccl_global KernelDeepRecord *records,
                             ccl_global KernelDeepEvent *events,
                             ccl_global KernelDeepMedium *media,
                             ccl_global KernelDeepDensity *density,
                             const float eps_ray,
                             const ccl_global KernelDeepRange *ranges,
                             const int tile_count,
                             const int media_count,
                             const int sample_limit)
{
  const int index = ccl_gpu_global_id_x();
  if (index >= count)
    return;
  int selected_tile = tile_index;
  int work = ranges ? int(ranges[index].work) : offset + index;
  if (tile_index < 0) {
    selected_tile = 0;
    while (selected_tile < tile_count && work >= tiles[selected_tile].work_size) {
      work -= tiles[selected_tile++].work_size;
    }
    if (selected_tile == tile_count)
      return;
  }
  const ccl_global KernelWorkTile *tile = &tiles[selected_tile];
  const int state = tile->path_index_offset + work;
  ccl_global KernelDeepRecord *record = &records[index];
  uint x, y, scheduled;
  get_work_pixel(tile, work, &x, &y, &scheduled);
  record->x = x;
  record->y = y;
  record->payload_counts = 0;
  record->population = 0;
  record->sample = 0;
  record->result = {DEEP_ACTIVE, 0, DEEP_ERROR_NONE};
  if (kernel_data.film.pass_adaptive_aux_buffer != PASS_UNUSED) {
    if (kernel_data.film.pass_sample_count == PASS_UNUSED) {
      record->result = {DEEP_FAILED, 0, DEEP_ERROR_STATE};
      return;
    }
    const ccl_global float *pixel = film_pass_pixel_render_buffer(nullptr, state, render_buffer);
    record->population = min(__float_as_uint(pixel[kernel_data.film.pass_sample_count]),
                             uint(sample_limit));
    /* Camera initialization has finished for this entire batch. The convergence
     * flag has not changed since initialization, so it identifies rejected lanes
     * without inspecting their uninitialized sample/ray fields. A skipped lane
     * is not a completed camera miss. */
    if (!film_need_sample_pixel(nullptr, state, render_buffer)) {
      record->sample = 0;
      record->result = {DEEP_SKIPPED, 0, DEEP_ERROR_NONE};
      return;
    }
  }
  record->sample = INTEGRATOR_STATE(state, path, sample);
  if (record->sample >= uint(sample_limit)) {
    record->result = {DEEP_SKIPPED, 0, DEEP_ERROR_NONE};
    return;
  }
  const auto flag = INTEGRATOR_STATE(state, path, flag);
  const auto rng_offset = INTEGRATOR_STATE(state, path, rng_offset);
  const auto transparent_bounce = INTEGRATOR_STATE(state, path, transparent_bounce);
  KernelDeepWriteState write;
  write.count_only = !events;
  write.limit = ranges ? ranges[index].count : unsigned(max(1, max_events) * event_stride);
  write.compact_density = ranges != nullptr;
  write.density_limit = ranges ? ranges[index].density_count : ~0u;
  const unsigned event_offset = ranges ? ranges[index].offset : unsigned(index);
  ccl_global KernelDeepEvent *lane_events = events ? events + event_offset : nullptr;
  ccl_global KernelDeepDensity *lane_density = density ? density + (ranges ? ranges[index].density_offset : event_offset) : nullptr;
  /* Event planes: neighboring lanes write neighboring events at each crossing.
   * The host allocates only the configured number of planes. */
  if (media) {
    const int next = INTEGRATOR_STATE(state, path, queued_kernel);
    if ((next != DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST &&
         next != DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK) ||
        INTEGRATOR_STATE(state, path, bounce) != 0 ||
        !(INTEGRATOR_STATE(state, path, visibility) & PATH_RAY_VISIBILITY_CAMERA))
      record->result = {DEEP_FAILED, 0, DEEP_ERROR_STATE};
    else
      record->result = deep_volume(
          nullptr, state, lane_events,
          media + (ranges || !events ? index * media_count : index), event_stride, max_events,
          lane_density, eps_ray, &write, ranges || !events ? 1 : event_stride, media_count);
  }
  else {
    record->result = deep_surface_cuda(nullptr, state, lane_events, event_stride, max_events, &write);
  }
  record->payload_counts = write.events | (write.companions << 16);
  if (write.failed)
    record->result = {DEEP_FAILED, record->result.count, DEEP_ERROR_EVENT_CAPACITY};
  /* Restore every field modified by visibility traversal, including failure.
   * Ray, intersection, throughput, queue counters and beauty buffers are untouched. */
  INTEGRATOR_STATE_WRITE(state, path, flag) = flag;
  INTEGRATOR_STATE_WRITE(state, path, rng_offset) = rng_offset;
  INTEGRATOR_STATE_WRITE(state, path, transparent_bounce) = transparent_bounce;
}
ccl_gpu_kernel_postfix
