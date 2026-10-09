/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/integrator/volume_shader.h"

CCL_NAMESPACE_BEGIN
#ifdef __OSL__
#  ifndef __KERNEL_GPU__
void deep_osl_eval_volume(const ThreadKernelGlobalsCPU *kg, const IntegratorStateCPU *state,
                         ShaderData *sd, PathRayVisibility visibility, uint32_t flag);
#  endif
#endif

/* ShaderDataTinyStorage deliberately omits closures on GPU. Initialize the
 * complete actual storage, not sizeof(ShaderData), and never request closures.
 * Reinitializing at each point also prevents shader scratch from leaking to the
 * next evaluation. The accepted beauty state is read-only. */
ccl_device_inline float3 deep_volume_sigma(
    KernelGlobals kg, IntegratorState state, const Ray &ray,
    const VolumeStack &entry, const double t, bool *cache_miss)
{
  ShaderDataTinyStorage storage{};
  ShaderData *sd = AS_SHADER_DATA(&storage);
  shader_setup_from_volume(sd, &ray, entry.object);
  sd->P = make_float3(float(double(ray.P.x) + double(ray.D.x) * t),
                     float(double(ray.P.y) + double(ray.D.y) * t),
                     float(double(ray.P.z) + double(ray.D.z) * t));
  sd->runtime_flag = SR_IS_VOLUME_SHADER_EVAL | (1 << 15);
  sd->shader = entry.shader;
  sd->shader_flag = kernel_data_fetch(shaders, entry.shader & SHADER_MASK).flags;
  sd->object_flag = kernel_data_fetch(object_flag, entry.object);
#ifdef __OBJECT_MOTION__
  shader_setup_object_transforms(kg, sd, sd->time);
  volume_shader_motion_blur(kg, sd);
#endif
  const uint32_t flag = INTEGRATOR_STATE(state, path, flag) | PATH_RAY_EXTINCTION;
#ifdef __OSL__
  if (kernel_data.kernel_features & KERNEL_FEATURE_OSL_SHADING) {
#  ifdef __KERNEL_OPTIX__
    ShaderGlobals globals{};
    shaderdata_to_shaderglobals(sd, PATH_RAY_VISIBILITY_CAMERA, flag, &globals);
    uint8_t closure_pool[1024]{};
    globals.closure_pool = closure_pool;
    globals.shade_index = state + 1;
    const unsigned int index = 2 + 1 +
        ((entry.shader & SHADER_MASK) + SHADER_TYPE_VOLUME * kernel_data.max_shaders);
    optixDirectCall<void>(index, &globals, (void *)nullptr, (void *)nullptr,
                          (void *)nullptr, 0, (void *)nullptr);
    if (globals.Ci)
      flatten_closure_tree(kg, sd, PATH_RAY_VISIBILITY_CAMERA, flag, globals.Ci);
#  else
    deep_osl_eval_volume(kg, state, sd, PATH_RAY_VISIBILITY_CAMERA, flag);
#  endif
  }
  else
#endif
  {
#ifdef __SVM__
    svm_eval_nodes<KERNEL_FEATURE_NODE_MASK_VOLUME | (1u << 31), SHADER_TYPE_VOLUME>(
        kg, state, sd, nullptr, PATH_RAY_VISIBILITY_CAMERA, flag);
#endif
  }
  *cache_miss = (sd->runtime_flag & SR_CACHE_MISS) != 0;
  return spectrum_to_rgb((sd->runtime_flag & SR_EXTINCTION) ?
                            sd->closure_transparent_extinction : zero_spectrum());
}
CCL_NAMESPACE_END
