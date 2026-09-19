/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/types.h"

CCL_NAMESPACE_BEGIN

/* Independent visibility chain for the restricted homogeneous CPU/CUDA contract.
 * Trace past the far clip to find exits of media containing the near clip.
 * Only in-clip intervals/surfaces are recorded. No beauty state or RNG mutation.
 * Closed convex volume meshes are validated before rendering. */
ccl_device KernelDeepResult deep_volume(KernelGlobals kg,
                                        IntegratorState state,
                                        ccl_global KernelDeepEvent *events,
                                        ccl_global KernelDeepMedium *media,
                                        const int stride,
                                        const int capacity)
{
#ifdef __VOLUME__
  Ray ray;
  integrator_state_read_ray(state, &ray);
  const float clip_start = ray.tmin, clip_end = ray.tmax;
  ray.tmax = FLT_MAX;
  ray.self.object = ray.self.light_object = OBJECT_NONE;
  ray.self.prim = ray.self.light_prim = PRIM_NONE;
  int object_count = 0, count = 0;
  Intersection previous;
  bool previous_back = false, has_previous = false;
  for (int step = 0; step < 128; ++step) {
    Intersection hit;
    if (!scene_intersect(kg, &ray, PATH_RAY_VISIBILITY_CAMERA, &hit)) {
      for (int i = 0; i < object_count; ++i)
        if (media[i * stride].start >= 0)
          return {DEEP_FAILED, 0, DEEP_ERROR_MEDIUM};
      return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
    }
    if (hit.type != PRIMITIVE_TRIANGLE)
      return {DEEP_FAILED, 0, DEEP_ERROR_PRIMITIVE};
    ShaderData sd;
    shader_setup_from_ray(kg, &sd, &ray, &hit);
    const bool back = (sd.runtime_flag & SR_BACKFACING) != 0;
    const bool duplicate = has_previous &&
                           deep_same_surface_boundary(kg, previous, hit, previous_back, back);
    if (!duplicate) {
      previous = hit;
      previous_back = back;
      has_previous = true;
      if (sd.shader_flag & SD_HAS_VOLUME) {
        int index = 0;
        while (index < object_count && media[index * stride].object != hit.object)
          ++index;
        if (index == object_count) {
          if (object_count == DEEP_MAX_MEDIA)
            return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
          media[index * stride] = {hit.object, back ? clip_start : -1.0f};
          /* First exit means the near clip is inside. */
          ++object_count;
        }
        if (!back) {
          if (media[index * stride].start >= 0)
            return {DEEP_FAILED, 0, DEEP_ERROR_MEDIUM};
          media[index * stride].start = hit.t;
        }
        else {
          if (media[index * stride].start < 0)
            return {DEEP_FAILED, 0, DEEP_ERROR_MEDIUM};
          const float start = fmaxf(media[index * stride].start, clip_start);
          const float end = fminf(hit.t, clip_end);
          media[index * stride].start = -1;
          if (end > start) {
            const VolumeStack entry = {sd.object, sd.shader};
            shader_setup_from_volume(&sd, &ray, hit.object);
            sd.num_closure = 0;
            sd.num_closure_left = 0;
            sd.runtime_flag = SR_IS_VOLUME_SHADER_EVAL;
            volume_shader_eval_entry<false, KERNEL_FEATURE_NODE_MASK_VOLUME>(
                kg,
                state,
                &sd,
                entry,
                PATH_RAY_VISIBILITY_CAMERA,
                INTEGRATOR_STATE(state, path, flag) | PATH_RAY_EXTINCTION);
            if (sd.runtime_flag & SR_CACHE_MISS)
              return {DEEP_FAILED, 0, DEEP_ERROR_CACHE_MISS};
            const float3 sigma = spectrum_to_rgb((sd.runtime_flag & SR_EXTINCTION) ?
                                                     sd.closure_transparent_extinction :
                                                     zero_spectrum());
            if (!isfinite(sigma.x) || sigma.x < 0 || sigma.x != sigma.y || sigma.x != sigma.z)
              return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};
            if (sigma.x > 0) {
              if (count == capacity)
                return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
              const float front = deep_camera_depth(kernel_data.cam,
                                                    kernel_data_array(camera_motion),
                                                    ray.time,
                                                    ray.P + start * ray.D);
              const float rear = deep_camera_depth(kernel_data.cam,
                                                   kernel_data_array(camera_motion),
                                                   ray.time,
                                                   ray.P + end * ray.D);
              if (!(rear > front) || !(front > 0))
                return {DEEP_FAILED, 0, DEEP_ERROR_DEPTH};
              events[count * stride] = {
                  DEEP_VOLUME, front, rear, 0, sigma.x * (end - start) * len(ray.D)};
              ++count;
            }
          }
        }
      }
      else if (hit.t <= clip_end) {
        /* Allowlisted shaders use ShaderData and accepted path identity.
         * Keep live beauty ray/intersection/volume-stack state untouched. */
        surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE & ~KERNEL_FEATURE_NODE_RAYTRACE>(
            kg,
            state,
            &sd,
            nullptr,
            INTEGRATOR_STATE(state, path, visibility),
            INTEGRATOR_STATE(state, path, flag));
        const float3 t = spectrum_to_rgb(surface_shader_transparency(&sd));
        if (sd.runtime_flag & SR_CACHE_MISS)
          return {DEEP_FAILED, 0, DEEP_ERROR_CACHE_MISS};
        if (!isfinite(t.x) || t.x < 0 || t.x > 1 || t.x != t.y || t.x != t.z)
          return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};
        if (t.x < 1) {
          if (count == capacity)
            return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
          const float z = deep_camera_depth(
              kernel_data.cam, kernel_data_array(camera_motion), ray.time, ray.P + hit.t * ray.D);
          events[count * stride] = {DEEP_SURFACE, z, z, 1 - t.x, 0};
          ++count;
        }
      }
    }
    const float next = intersection_t_offset(hit.t);
    if (!(next > ray.tmin))
      return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
    ray.tmin = next;
    ray.self.object = hit.object;
    ray.self.prim = hit.prim;
  }
#endif
  return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
}
CCL_NAMESPACE_END
