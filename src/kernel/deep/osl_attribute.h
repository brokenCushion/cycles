/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/osl/services_shared.h"

CCL_NAMESPACE_BEGIN
/* Deep-only point evaluation: use the native deterministic filter whose
 * weighted sum is estimated by stochastic volume interpolation. Derivatives
 * are zero, as for native volume point evaluation; no RNG is read or advanced. */
template<typename T>
ccl_device_inline bool deep_osl_volume_attribute_impl(
    KernelGlobals kg, ShaderData *sd, const AttributeDescriptor &desc,
    const TypeDesc type, bool derivatives, void *val)
{
  const dual<T> data(primitive_volume_attribute<T>(kg, sd, desc, false));
  return set_attribute(data, type, derivatives, val);
}

ccl_device_inline bool deep_osl_volume_attribute(
    KernelGlobals kg, ShaderData *sd, const AttributeDescriptor &desc,
    const TypeDesc type, bool derivatives, void *val)
{
  switch (desc.type) {
    case NODE_ATTR_FLOAT:
      return deep_osl_volume_attribute_impl<float>(kg, sd, desc, type, derivatives, val);
    case NODE_ATTR_FLOAT2:
      return deep_osl_volume_attribute_impl<float2>(kg, sd, desc, type, derivatives, val);
    case NODE_ATTR_FLOAT3:
      return deep_osl_volume_attribute_impl<float3>(kg, sd, desc, type, derivatives, val);
    case NODE_ATTR_FLOAT4:
    case NODE_ATTR_RGBA:
      return deep_osl_volume_attribute_impl<float4>(kg, sd, desc, type, derivatives, val);
    default:
      return osl_shared_get_object_attribute(kg, sd, desc, type, derivatives, val);
  }
}
CCL_NAMESPACE_END
