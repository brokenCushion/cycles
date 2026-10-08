/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/types.h"
CCL_NAMESPACE_BEGIN

/* Queue-owned device arguments. Keep beauty's KernelParamsOptiX layout intact;
 * its otherwise unused path_index_array points here only in the deep pipeline. */
struct KernelDeepParamsOptiX {
  const KernelWorkTile *tiles;
  int tile_index, offset, count, max_events, event_stride;
  float *render_buffer;
  KernelDeepRecord *records;
  KernelDeepEvent *events;
  KernelDeepMedium *media;
  KernelDeepDensity *density;
  float eps_ray;
  const KernelDeepRange *ranges;
  int tile_count, media_count, sample_limit;
};

/* First 24 SBT records are the unmodified native hit groups. */
constexpr int DEEP_OPTIX_ALL_HIT_OFFSET = 24;
CCL_NAMESPACE_END
