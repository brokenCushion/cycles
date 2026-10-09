/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/optix.h"

extern "C" __global__ void __anyhit__kernel_optix_deep_all_hit()
{
  if (!optixIsTriangleHit())
    return optixIgnoreIntersection();
  const Intersection hit = get_intersection();
  const Ray *ray = get_payload_ptr_6<Ray>();
  const bool volume_only = optixGetPayload_3();
  if (bvh_volume_anyhit_triangle_filter(nullptr, hit.object, hit.prim,
                                      ray->self, optixGetPayload_4(), volume_only))
    return optixIgnoreIntersection();
  Intersection *hits = get_payload_ptr_0<Intersection>();
  const uint capacity = optixGetPayload_2();
  uint count = optixGetPayload_5();
  if (volume_only) {
    if (count < capacity)
      hits[count++] = hit;
    optixSetPayload_5(count);
    if (count == capacity)
      return optixTerminateRay(); /* Caller explicitly fails saturated initial media. */
  }
  else {
    bvh_deep_record_intersection(hits, count, capacity, hit);
    optixSetPayload_5(count);
  }
  optixIgnoreIntersection();
}

ccl_device uint scene_intersect_volume(KernelGlobals kg,
                                       const Ray *ray, Intersection *hits,
                                       const uint capacity, const uint visibility,
                                       const bool volume_only = true)
{
  uint p0 = pointer_pack_to_uint_0(hits), p1 = pointer_pack_to_uint_1(hits);
  uint p2 = capacity, p3 = volume_only, p4 = visibility, p5 = 0;
  uint p6 = pointer_pack_to_uint_0(ray), p7 = pointer_pack_to_uint_1(ray);
  uint mask = visibility & 0xFF;
  if (!mask && (visibility & ~0xFF))
    mask = 0xFF;
  optixTrace(intersection_ray_valid(ray) ? kernel_data.device_bvh : 0,
             ray->P, ray->D, ray->tmin, ray->tmax, ray->time, mask,
             OPTIX_RAY_FLAG_ENFORCE_ANYHIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
             DEEP_OPTIX_ALL_HIT_OFFSET, 0, 0, p0, p1, p2, p3, p4, p5, p6, p7);
  return p5;
}
