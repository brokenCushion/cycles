/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/mip.h"

CCL_NAMESPACE_BEGIN
/* Deep-only OptiX service: preserve native UDIM, wrapping, cache handling and
 * linear/cubic interpolation, averaging the native stochastic mip selector.
 * Zero derivatives and untiled images retain the original point lookup. */
ccl_device float4 deep_image_interp(KernelGlobals kg, ShaderData *sd, const int id, dual2 uv)
{
  const int image = kernel_image_udim_map(kg, id, uv.val);
  if (image == KERNEL_IMAGE_NONE)
    return IMAGE_MISSING_RGBA;
  const auto &tex = kernel_data_fetch(image_textures, image);
  if (tex.tile_descriptor_offset == KERNEL_TILE_LOAD_NONE)
    return kernel_image_interp(kg, sd, image, uv);
  const float du = len_squared(make_float2(uv.dx.x, uv.dy.x)) * float(tex.width * tex.width);
  const float dv = len_squared(make_float2(uv.dx.y, uv.dy.y)) * float(tex.height * tex.height);
  const float flevel = 0.5f * log2(max(min(du, dv), max(du, dv) / 256.0f)) +
                       kernel_data.image.mip_bias;
  const auto mip = deep_mip_filter(flevel, tex.tile_levels);
  const float4 low = kernel_image_interp<true>(kg, sd, image, uv, mip.low);
  if (mip.low == mip.high || mip.high_weight == 0.0f)
    return low;
  const float4 high = kernel_image_interp<true>(kg, sd, image, uv, mip.high);
  return low * (1.0f - mip.high_weight) + high * mip.high_weight;
}
CCL_NAMESPACE_END
