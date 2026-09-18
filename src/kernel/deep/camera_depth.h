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
CCL_NAMESPACE_END
