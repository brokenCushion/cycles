/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/types.h"
#include "kernel/deep/volume_boundary.h"
#include "kernel/deep/volume_native.h"

CCL_NAMESPACE_BEGIN

#ifdef __VOLUME__
/* Capture one exact interval without changing beauty state. */
ccl_device KernelDeepResult deep_volume_interval(
    KernelGlobals kg, IntegratorState state, ShaderData *sd, const Ray &ray,
    const int object, const double start, const double end,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, int count)
{
  const VolumeStack entry = {sd->object, sd->shader};
  const float grid_scale = kernel_data_fetch(shaders, sd->shader & SHADER_MASK).deep_density_scale;
  /* A zero multiplier may eliminate the density attribute during native shader
   * compilation. It contributes no extinction and needs no voxel lookup. */
  if (grid_scale == 0)
    return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
  shader_setup_from_volume(sd, &ray, object);
  if (grid_scale >= 0) {
    sd->shader = entry.shader;
    sd->shader_flag = kernel_data_fetch(shaders, entry.shader & SHADER_MASK).flags;
    sd->object_flag = kernel_data_fetch(object_flag, object);
    const KernelDeepResult captured = deep_volume_native(
        kg, sd, &ray, start, end, grid_scale, events, density, stride, capacity, count);
    if (captured.status != DEEP_COMPLETE)
      return captured;
    count = int(captured.count);
  }
  else {
    sd->num_closure = 0;
    sd->num_closure_left = 0;
    sd->runtime_flag = SR_IS_VOLUME_SHADER_EVAL;
    volume_shader_eval_entry<false, KERNEL_FEATURE_NODE_MASK_VOLUME>(
        kg,
        state,
        sd,
        entry,
        PATH_RAY_VISIBILITY_CAMERA,
        INTEGRATOR_STATE(state, path, flag) | PATH_RAY_EXTINCTION);
    if (sd->runtime_flag & SR_CACHE_MISS)
      return {DEEP_FAILED, 0, DEEP_ERROR_CACHE_MISS};
    const float3 sigma = spectrum_to_rgb((sd->runtime_flag & SR_EXTINCTION) ?
                                             sd->closure_transparent_extinction :
                                             zero_spectrum());
    if (!isfinite(sigma.x) || sigma.x < 0 || sigma.x != sigma.y || sigma.x != sigma.z)
      return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};
    if (sigma.x > 0) {
      if (count == capacity)
        return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
      const float4 camera_z = kernel_data.cam.worldtocamera.z;
      const double depth_origin = double(camera_z.x) * ray.P.x + double(camera_z.y) * ray.P.y +
                                  double(camera_z.z) * ray.P.z + camera_z.w;
      const double depth_per_t = double(camera_z.x) * ray.D.x + double(camera_z.y) * ray.D.y +
                                 double(camera_z.z) * ray.D.z;
      const float front = float(depth_origin + start * depth_per_t);
      const float rear = float(depth_origin + end * depth_per_t);
      if (!(rear > front) || !(front > 0))
        return {DEEP_FAILED, 0, DEEP_ERROR_DEPTH};
      events[count * stride] = {
          DEEP_VOLUME, front, rear, 0, float(double(sigma.x) * (end - start) * len(ray.D))};
      ++count;
    }
  }
  return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
}

/* The BVH discovers objects. Pair their actual triangle crossings in double:
 * FLOAT all-hit queries may omit a grazing exit or include a false edge hit.
 * Repeated nearest-hit scans use constant scratch, including for nonconvex VDB
 * boundary meshes; no per-thread allocation or fixed face-count truncation. */
ccl_device KernelDeepResult deep_volume_object(
    KernelGlobals kg, IntegratorState state, ShaderData *sd, const Ray &ray,
    const int object, const double clip_start, const double clip_end,
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, int count)
{
  double origin[3] = {ray.P.x, ray.P.y, ray.P.z};
  double direction[3] = {ray.D.x, ray.D.y, ray.D.z};
  if (!(kernel_data_fetch(object_flag, object) & SD_OBJECT_TRANSFORM_APPLIED)) {
    const Transform tfm = object_fetch_transform(kg, object, OBJECT_INVERSE_TRANSFORM);
    const float4 rows[3] = {tfm.x, tfm.y, tfm.z};
    double p[3], d[3];
    for (int axis = 0; axis < 3; ++axis) {
      const float4 r = rows[axis];
      p[axis] = r.x * origin[0] + r.y * origin[1] + r.z * origin[2] + r.w;
      d[axis] = r.x * direction[0] + r.y * direction[1] + r.z * direction[2];
    }
    for (int axis = 0; axis < 3; ++axis) {
      origin[axis] = p[axis];
      direction[axis] = d[axis];
    }
  }
  const int first = kernel_data_fetch(object_prim_offset, object);
  const int size = kernel_data_fetch(objects, object).numprims;
  const int shader = sd->shader;
  double cursor = clip_start, start = -1;
  int cursor_prim = -1;
  Intersection previous;
  bool previous_back = false, has_previous = false;
  for (int step = 0; step < (density ? 16384 : 128); ++step) {
    double nearest = double(FLT_MAX), near_u = 0, near_v = 0;
    int near_prim = -1;
    bool back = false;
    for (int i = 0; i < size; ++i) {
      const int prim = first + i;
      float3 vertices[3];
      triangle_vertices(kg, object, prim, vertices);
      const double points[3][3] = {{vertices[0].x, vertices[0].y, vertices[0].z},
                                   {vertices[1].x, vertices[1].y, vertices[1].z},
                                   {vertices[2].x, vertices[2].y, vertices[2].z}};
      double t, u, v;
      bool candidate_back;
      if (!deep_volume_triangle(origin, direction, points, t, u, v, candidate_back) ||
          t < cursor || (t == cursor && prim <= cursor_prim) ||
          t > nearest || (t == nearest && near_prim >= 0 && prim > near_prim))
        continue;
      nearest = t; near_u = u; near_v = v; near_prim = prim; back = candidate_back;
    }
    if (near_prim < 0)
      return {start < 0 ? DEEP_COMPLETE : DEEP_FAILED, unsigned(count),
              start < 0 ? DEEP_ERROR_NONE : DEEP_ERROR_MEDIUM};
    cursor = nearest;
    cursor_prim = near_prim;
    Intersection hit;
    hit.object = object; hit.prim = near_prim; hit.type = PRIMITIVE_TRIANGLE;
    hit.t = float(nearest); hit.u = float(near_u); hit.v = float(near_v);
    if (has_previous && deep_same_surface_boundary(kg, previous, hit, previous_back, back))
      continue;
    if (!has_previous && back)
      start = clip_start;
    previous = hit; previous_back = back; has_previous = true;
    if (!back) {
      if (start >= 0)
        return {DEEP_FAILED, 0, DEEP_ERROR_MEDIUM};
      start = nearest;
    }
    else {
      if (start < 0)
        return {DEEP_FAILED, 0, DEEP_ERROR_MEDIUM};
      const double end = nearest < clip_end ? nearest : clip_end;
      if (end > start) {
        sd->shader = shader;
        const KernelDeepResult result = deep_volume_interval(
            kg, state, sd, ray, object, start, end, events, density, stride, capacity, count);
        if (result.status != DEEP_COMPLETE)
          return result;
        count = int(result.count);
      }
      start = -1;
    }
  }
  return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
}

#endif

/* Independent visibility chain for the restricted homogeneous CPU/CUDA contract.
 * Trace past the far clip to find exits of media containing the near clip.
 * Only in-clip intervals/surfaces are recorded. No beauty state or RNG mutation.
 * Closed convex volume meshes are validated before rendering. */
ccl_device KernelDeepResult deep_volume(KernelGlobals kg,
                                        IntegratorState state,
                                        ccl_global KernelDeepEvent *events,
                                        ccl_global KernelDeepMedium *media,
                                        const int stride,
                                        const int capacity,
                                        ccl_global KernelDeepDensity *density = nullptr)
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
  /* Collect a bounded nearest batch before advancing the ray. Closest-hit-only
   * traversal loses another object's crossing at an identical FLOAT distance.
   * Fixed scratch has no per-thread heap allocation; the extra element is the
   * existing BVH all-hit traversal's sentinel slot. */
  constexpr int boundary_capacity = 2 * DEEP_MAX_MEDIA;
  Intersection boundaries[boundary_capacity + 1];
  bool boundary_back[boundary_capacity];
  int steps = 0;
  while (steps < (density ? 16384 : 128)) {
    uint boundary_count = scene_intersect_volume(
        kg, &ray, boundaries, boundary_capacity, PATH_RAY_VISIBILITY_CAMERA, false);
    if (boundary_count == 0) {
      return {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE};
    }
    /* Insertion sort is cheap for the usual one/two crossings and bounded for
     * coincident layers. Sorting preserves genuine thin intervals. */
    for (uint i = 1; i < boundary_count; ++i) {
      const Intersection value = boundaries[i];
      uint j = i;
      while (j && (boundaries[j - 1].t > value.t ||
                   (boundaries[j - 1].t == value.t && boundaries[j - 1].object > value.object))) {
        boundaries[j] = boundaries[j - 1];
        --j;
      }
      boundaries[j] = value;
    }
    if (boundary_count == boundary_capacity) {
      /* An unrecorded crossing may tie the farthest retained hit. Leave that
       * entire depth for the next query instead of silently skipping a layer. */
      const float last = boundaries[boundary_count - 1].t;
      while (boundary_count && boundaries[boundary_count - 1].t == last)
        --boundary_count;
      if (!boundary_count)
        return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
    }
    for (uint boundary = 0; boundary < boundary_count; ++boundary) {
      if (++steps > (density ? 16384 : 128))
        return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
      const Intersection hit = boundaries[boundary];
      if (hit.type != PRIMITIVE_TRIANGLE)
        return {DEEP_FAILED, 0, DEEP_ERROR_PRIMITIVE};
      ShaderData sd;
      shader_setup_from_ray(kg, &sd, &ray, &hit);
      const bool back = (sd.runtime_flag & SR_BACKFACING) != 0;
      boundary_back[boundary] = back;
      bool duplicate = has_previous &&
                       deep_same_surface_boundary(kg, previous, hit, previous_back, back);
      for (uint i = 0; !duplicate && i < boundary; ++i) {
        if (boundaries[i].object == hit.object) {
          duplicate = deep_same_surface_boundary(
              kg, boundaries[i], hit, boundary_back[i], back);
        }
      }
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
            media[index * stride] = {hit.object, -1.0f};
            ++object_count;
            const KernelDeepResult result = deep_volume_object(
                kg, state, &sd, ray, hit.object, clip_start, clip_end,
                events, density, stride, capacity, count);
            if (result.status != DEEP_COMPLETE)
              return result;
            count = int(result.count);
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
    }
    const float next = intersection_t_offset(boundaries[boundary_count - 1].t);
    if (!(next > ray.tmin))
      return {DEEP_FAILED, 0, DEEP_ERROR_PROGRESS};
    ray.tmin = next;
    ray.self.object = OBJECT_NONE;
    ray.self.prim = PRIM_NONE;
  }
#endif
  return {DEEP_FAILED, 0, DEEP_ERROR_CAPACITY};
}
CCL_NAMESPACE_END
