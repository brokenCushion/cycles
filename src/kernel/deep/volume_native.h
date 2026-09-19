/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#if defined(WITH_NANOVDB) && (!defined(__KERNEL_GPU__) || defined(__KERNEL_CUDA__))
#  include "kernel/deep/volume_grid.h"
#endif

CCL_NAMESPACE_BEGIN

/* Resolve the same density attribute and texture transforms as native volume
 * shading. Host preflight supplies a validated scalar absorption multiplier.
 * The static camera/object contract is checked again before using transforms. */
ccl_device KernelDeepResult deep_volume_native(KernelGlobals kg,
                                               ccl_private ShaderData *sd,
                                               const ccl_private Ray *ray,
                                               const float start,
                                               const float end,
                                               const float scale,
                                               ccl_global KernelDeepEvent *events,
                                               ccl_global KernelDeepDensity *density,
                                               const int stride,
                                               const int capacity,
                                               const int first)
{
#if defined(WITH_NANOVDB) && (!defined(__KERNEL_GPU__) || defined(__KERNEL_CUDA__))
  if ((sd->object_flag & SD_OBJECT_MOTION) || kernel_data.cam.num_motion_steps ||
      (sd->shader_flag & SD_VOLUME_CUBIC))
    return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
  const AttributeDescriptor desc = find_attribute(kg, sd, ATTR_STD_VOLUME_DENSITY);
  if (!is_attribute_found(desc) || desc.element != ATTR_ELEMENT_VOXEL ||
      desc.type != NODE_ATTR_FLOAT)
    return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};
  const ccl_global KernelImageTexture &texture = kernel_data_fetch(image_textures, desc.offset);
  const ccl_global KernelImageInfo &info = kernel_data_fetch(image_info, texture.image_info_id);
  if (info.interpolation != INTERPOLATION_LINEAR)
    return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};
  if (info.data_type == IMAGE_DATA_TYPE_NANOVDB_EMPTY)
    return {DEEP_COMPLETE, unsigned(first), DEEP_ERROR_NONE};
  if (info.data_type != IMAGE_DATA_TYPE_NANOVDB_FLOAT || !info.data)
    return {DEEP_FAILED, 0, DEEP_ERROR_EXTINCTION};

  double origin[3] = {ray->P.x, ray->P.y, ray->P.z};
  double direction[3] = {ray->D.x, ray->D.y, ray->D.z};
  const double physical_length = sqrt(direction[0] * direction[0] +
                                      direction[1] * direction[1] +
                                      direction[2] * direction[2]);
  const Transform object_inverse = object_fetch_transform(kg, sd->object, OBJECT_INVERSE_TRANSFORM);
  /* Evaluate the native FLOAT transform coefficients in double, avoiding an
   * intermediate FLOAT position subtraction for long/translated grid rays. */
  for (int transform = 0; transform < (texture.use_transform_3d ? 2 : 1); ++transform) {
    const Transform tfm = transform == 0 ? object_inverse : texture.transform_3d;
    const float4 rows[3] = {tfm.x, tfm.y, tfm.z};
    double p[3], d[3];
    for (int axis = 0; axis < 3; ++axis) {
      const float4 row = rows[axis];
      p[axis] = row.x * origin[0] + row.y * origin[1] + row.z * origin[2] + row.w;
      d[axis] = row.x * direction[0] + row.y * direction[1] + row.z * direction[2];
    }
    for (int axis = 0; axis < 3; ++axis) {
      origin[axis] = p[axis];
      direction[axis] = d[axis];
    }
  }
  const float4 camera_z = kernel_data.cam.worldtocamera.z;
  const double depth_origin = double(camera_z.x) * ray->P.x + double(camera_z.y) * ray->P.y +
                              double(camera_z.z) * ray->P.z + camera_z.w;
  const double depth_per_t = double(camera_z.x) * ray->D.x + double(camera_z.y) * ray->D.y +
                             double(camera_z.z) * ray->D.z;
  const auto *grid = (ccl_global nanovdb::NanoGrid<float> *)info.data;
  const nanovdb::CachedReadAccessor<float> accessor(grid->tree().root());
  return deep_volume_grid_capture(accessor, origin, direction, start, end, scale,
                                  physical_length, depth_origin, depth_per_t,
                                  events, density, stride, capacity, first, 16384);
#else
  return {DEEP_FAILED, 0, DEEP_ERROR_STATE};
#endif
}

CCL_NAMESPACE_END
