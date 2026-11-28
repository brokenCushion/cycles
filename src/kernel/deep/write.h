/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/deep/merge.h"
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
  if (!events) {
    if (write && write->count_only) {
      ++write->events;
      write->companions += companion ? 1 : 0;
    }
    return;
  }
  if (write && (unsigned(index) >= write->limit ||
                (companion && write->compact_density && write->companions >= write->density_limit))) {
    write->failed = true;
    return;
  }
  events[index] = event;
  if (companion)
    density[write && write->compact_density ? write->companions : unsigned(index)] = *companion;
  if (write) {
    ++write->events;
    write->companions += companion ? 1 : 0;
    write->written_bytes += sizeof(KernelDeepEvent) + (companion ? sizeof(KernelDeepDensity) : 0);
  }
}
/* Make room for one more event of this camera sample. Below capacity nothing
 * changes. At capacity, capture entry points (write->fallback) merge similar
 * neighbouring events (kernel/deep/merge.h) instead of failing, and flag the
 * sample. A count pass has no payload to merge: it assumes one freed slot, and
 * the host can allocate a flat range for the exact records when their count
 * fits the staging limit. Otherwise the output pass merges in the kernel. */
ccl_device_inline KernelDeepError deep_event_reserve(
    ccl_global KernelDeepEvent *events, ccl_global KernelDeepDensity *density,
    const int stride, const int capacity, ccl_private int *count,
    ccl_private KernelDeepWriteState *write)
{
  if (*count < capacity)
    return DEEP_ERROR_NONE;
  if (!write || !write->fallback || capacity < 1 || (!events && !write->count_only))
    return DEEP_ERROR_EVENT_CAPACITY;
  write->fallbacks |= DEEP_FALLBACK_MERGED;
  if (write->defer_merge) {
    if (events)
      return unsigned(*count) < write->limit ? DEEP_ERROR_NONE : DEEP_ERROR_EVENT_CAPACITY;
    if (unsigned(*count) < DEEP_FLAT_COUNT_LIMIT)
      return DEEP_ERROR_NONE;
  }
  if (!events) {
    /* Keep the predicted counters live (at most capacity) like the output pass. */
    --*count;
    if (write->events)
      --write->events;
    if (write->companions)
      --write->companions;
    return DEEP_ERROR_NONE;
  }
  int companions = 0;
  int freed = 1;
  if (!deep_merge_tail(events, density, stride, *count, write, &companions)) {
    write->tail_merge_rate = -1.0f;
    freed = deep_merge_events(events, density, stride, *count, write->compact_density,
                              write->volume_companions, capacity / 2, &companions);
  }
  if (freed < 1)
    return DEEP_ERROR_EVENT_CAPACITY;
  *count -= freed;
  write->events -= unsigned(freed) < write->events ? unsigned(freed) : write->events;
  write->companions -= unsigned(companions) < write->companions ? unsigned(companions) :
                                                                    write->companions;
  return DEEP_ERROR_NONE;
}
CCL_NAMESPACE_END
