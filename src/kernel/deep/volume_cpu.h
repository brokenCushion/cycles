/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/volume.h"

CCL_NAMESPACE_BEGIN

ccl_device KernelDeepResult deep_volume_cpu(const ThreadKernelGlobalsCPU *kg,
                                            const IntegratorStateCPU *camera,
                                            KernelDeepEvent *events,
                                            const int capacity,
                                            KernelDeepDensity *density,
                                            const double eps_ray, KernelDeepWriteState *write = nullptr)
{
  IntegratorStateCPU private_state = *camera;
  KernelDeepMedium media[DEEP_MAX_MEDIA];
  return deep_volume(kg, &private_state, events, media, 1, capacity, density, eps_ray, write);
}

CCL_NAMESPACE_END
