/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "util/defines.h"

CCL_NAMESPACE_BEGIN
struct DeepMipFilter {
  int low, high;
  float high_weight;
};

/* Native mip selection floors flevel + .25 + .5*U, U uniform in [0,1).
 * This interval crosses at most one integer. Integrate its two probabilities,
 * then apply native endpoint clamping. The caller supplies nonnegative flevel;
 * negative footprints clamp to level zero and need no float-to-int conversion. */
ccl_device DeepMipFilter deep_mip_filter(const float flevel, const int levels)
{
  if (!(flevel > 0.0f))
    return {0, 0, 0.0f};
  if (flevel >= float(levels - 1))
    return {levels - 1, levels - 1, 0.0f};
  const int low = int(flevel + 0.25f);
  const int high = low + 1 < levels ? low + 1 : levels - 1;
  const float weight = (flevel + 0.75f - float(low + 1)) * 2.0f;
  return {low, high, weight > 0.0f ? weight : 0.0f};
}
CCL_NAMESPACE_END
