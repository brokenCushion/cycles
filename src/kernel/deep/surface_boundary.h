/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/globals.h"

CCL_NAMESPACE_BEGIN

/* Robust triangle tests can accept both sides of a shared edge. A straight
 * visibility traversal must count that boundary once. This is not depth-only
 * merging: disconnected layers and opposite-facing entry/exit hits survive. */
ccl_device_inline bool deep_same_surface_boundary(KernelGlobals kg,
                                                   const Intersection &previous,
                                                   const Intersection &current,
                                                   const bool previous_backfacing,
                                                   const bool current_backfacing)
{
  if (previous.object != current.object || previous.prim == current.prim ||
      previous_backfacing != current_backfacing ||
      fabsf(current.t - previous.t) > 8.0f * FLT_EPSILON * max(current.t, previous.t))
  {
    return false;
  }
  const uint3 a = kernel_data_fetch(tri_vindex, previous.prim);
  const uint3 b = kernel_data_fetch(tri_vindex, current.prim);
  const uint av[3] = {a.x, a.y, a.z};
  const uint bv[3] = {b.x, b.y, b.z};
  const float aw[3] = {1.0f - previous.u - previous.v, previous.u, previous.v};
  const float bw[3] = {1.0f - current.u - current.v, current.u, current.v};
  int shared = 0;
  for (int i = 0; i < 3; ++i) {
    bool match = false;
    for (int j = 0; j < 3; ++j) {
      match |= av[i] == bv[j];
    }
    shared += match;
    if (!match && fabsf(aw[i]) > 8.0f * FLT_EPSILON) {
      return false;
    }
  }
  if (shared != 2) {
    return false;
  }
  for (int j = 0; j < 3; ++j) {
    if (bv[j] != av[0] && bv[j] != av[1] && bv[j] != av[2] &&
        fabsf(bw[j]) > 8.0f * FLT_EPSILON)
    {
      return false;
    }
  }
  return true;
}

CCL_NAMESPACE_END
