/* SPDX-License-Identifier: Apache-2.0 */
/* Separate renderer-services module, linked only into the deep pipeline.
 * Beauty links the original module and retains its stochastic lookups. */
#define osl_get_attribute deep_native_osl_get_attribute
#include "kernel/osl/services_optix.cu"
#undef osl_get_attribute
#include "kernel/deep/osl_attribute.h"

CCL_NAMESPACE_BEGIN
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
