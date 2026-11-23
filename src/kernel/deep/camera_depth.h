/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/types.h"
#include "util/transform.h"

CCL_NAMESPACE_BEGIN
/* Axial depth in the camera that generated this shutter-time sample. */
ccl_device_inline float deep_camera_depth(const KernelCamera &camera,
                                         const ccl_global DecomposedTransform *motion,
                                         const float time,
                                         const float3 point)
{
  if (camera.num_motion_steps) {
    Transform camera_to_world;
    transform_motion_array_interpolate(&camera_to_world, motion, camera.num_motion_steps, time);
    const Transform world_to_camera = transform_inverse(camera_to_world);
    return transform_point(&world_to_camera, point).z;
  }
  return transform_point(&camera.worldtocamera, point).z;
}

/* Camera Z row (world to camera) at this sample's shutter time: the matrix
 * deep_camera_depth inverts, so volume depth along a ray agrees with surface
 * depth. Depth stays affine in the ray parameter; a static camera returns the
 * unchanged worldtocamera row. */
ccl_device_inline float4 deep_camera_depth_row(const KernelCamera &camera,
                                              const ccl_global DecomposedTransform *motion,
                                              const float time)
{
  if (camera.num_motion_steps) {
    Transform camera_to_world;
    transform_motion_array_interpolate(&camera_to_world, motion, camera.num_motion_steps, time);
    return transform_inverse(camera_to_world).z;
  }
  return camera.worldtocamera.z;
}
CCL_NAMESPACE_END
