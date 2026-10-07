/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/types.h"
CCL_NAMESPACE_BEGIN
/* Logical payload stores, including partial failed attempts. A count pass emits
 * no payload. The assigned flat range is checked before either buffer write. */
ccl_device_inline void deep_write_event(
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int index, const KernelDeepEvent event,
    const ccl_private KernelDeepDensity *companion,
    ccl_private KernelDeepWriteState *write)
{
  if (!events)
    return;
  if (write && unsigned(index) >= write->limit) {
    write->failed = true;
    return;
  }
  events[index] = event;
  if (companion)
    density[index] = *companion;
  if (write) {
    ++write->events;
    write->companions += companion ? 1 : 0;
    write->written_bytes += sizeof(KernelDeepEvent) + (companion ? sizeof(KernelDeepDensity) : 0);
  }
}
CCL_NAMESPACE_END
