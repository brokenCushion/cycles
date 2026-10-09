/* SPDX-License-Identifier: Apache-2.0 */
/* Separate renderer-services module, linked only into the deep pipeline.
 * Beauty links the original module and retains its stochastic lookups. */
#define osl_get_attribute deep_native_osl_get_attribute
#define rs_texture deep_native_rs_texture
#include "kernel/osl/services_optix.cu"
#undef osl_get_attribute
#undef rs_texture
#include "kernel/deep/osl_attribute.h"
#include "kernel/deep/image_2d.h"

CCL_NAMESPACE_BEGIN
ccl_device_extern bool rs_texture(
    ShaderGlobals *sg, RSDeviceString filename, void *handle, void *thread_info,
    OSLTextureOptions *opt, float s, float t, float dsdx, float dtdx,
    float dsdy, float dtdy, int channels, float *result, float *ds,
    float *dt, void *error)
{
  if ((sg->sd->runtime_flag & (1 << 15)) &&
      OSL_TEXTURE_HANDLE_TYPE(handle) == OSLTextureHandleType::IMAGE) {
    const float4 rgba = deep_image_interp(nullptr, sg->sd, OSL_TEXTURE_HANDLE_ID(handle),
                                         dual2({s, t}, {dsdx, dtdx}, {dsdy, dtdy}));
    rgba_to_nchannels(rgba, channels, result);
    return true;
  }
  return deep_native_rs_texture(sg, filename, handle, thread_info, opt, s, t,
                               dsdx, dtdx, dsdy, dtdy, channels, result, ds, dt, error);
}

ccl_device_extern bool osl_get_attribute(
    ShaderGlobals *sg, const int derivatives, DeviceString object_name,
    DeviceString name, const int array_lookup, const int index,
    const RSTypeDesc type_abi, void *res)
{
  ShaderData *sd = sg->sd;
  if ((sd->runtime_flag & (1 << 15)) && object_name == DeviceStrings::u_empty) {
    const AttributeDescriptor desc = find_attribute(nullptr, sd->object, sd->prim, name);
    if (is_attribute_found(desc))
      return deep_osl_volume_attribute(nullptr, sd, desc,
          *reinterpret_cast<const TypeDesc *>(&type_abi), derivatives, res);
  }
  return deep_native_osl_get_attribute(sg, derivatives, object_name, name,
                                       array_lookup, index, type_abi, res);
}
CCL_NAMESPACE_END
