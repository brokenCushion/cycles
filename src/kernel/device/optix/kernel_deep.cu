/* SPDX-License-Identifier: Apache-2.0 */
#ifdef WITH_CYCLES_DEEP_OPAQUE
#include "kernel/device/optix/compat.h"
#include "kernel/device/optix/globals.h"
#include "kernel/device/gpu/image.h"
#include "kernel/tables.h"
#include "kernel/integrator/init_from_camera.h"
#include "kernel/integrator/shade_surface.h"
#include "kernel/integrator/volume_shader.h"
#include "kernel/device/gpu/work_stealing.h"
#include "kernel/film/read.h"
#include "kernel/deep/surface_cuda.h"

/* Keep the all-hit count intact on miss. Ordinary deep closest-hit queries
 * already initialize their primitive payload to PRIMITIVE_NONE. */
extern "C" __global__ void __miss__kernel_optix_deep() {}

extern "C" __global__ void __raygen__kernel_optix_deep_surface()
{
  const KernelDeepParamsOptiX &p =
      *reinterpret_cast<const KernelDeepParamsOptiX *>(kernel_params.path_index_array);
  deep_surface_kernel(optixGetLaunchIndex().x, p.tiles, p.tile_index, p.offset,
      p.count, p.max_events, p.event_stride, p.render_buffer, p.records,
      p.events, p.media, p.density, p.eps_ray, p.ranges, p.tile_count,
      p.media_count, p.sample_limit);
}
#endif
