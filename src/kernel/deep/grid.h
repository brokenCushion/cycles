/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "util/defines.h"
#ifndef __KERNEL_GPU__
#  include <cmath>
#endif

CCL_NAMESPACE_BEGIN

enum DeepGridStep { DEEP_GRID_SEGMENT, DEEP_GRID_DONE, DEEP_GRID_INVALID, DEEP_GRID_LIMIT };

/* Grid-space ray, retaining the original ray parameter and physical length.
 * Fixed state only; neither cells nor per-ray samples are allocated here. */
template<typename T> struct DeepGridCursor {
  T origin[3], direction[3], next[3];
  T current, end;
  int cell[3], step[3], remaining;
  bool valid;
};
template<typename T> struct DeepGridSegment {
  int cell[3];
  T front, back;
};

template<typename T>
ccl_device bool deep_grid_begin(ccl_private DeepGridCursor<T> *cursor,
                                const ccl_private T *origin,
                                const ccl_private T *direction,
                                const T start,
                                const T end,
                                const int limit)
{
  cursor->valid = false;
  if (!(start >= T(0) && end > start) || limit <= 0)
    return false;
  cursor->current = start;
  cursor->end = end;
  cursor->remaining = limit;
  bool moving = false;
  for (int axis = 0; axis < 3; ++axis) {
    const T p = origin[axis] + start * direction[axis];
    const T q = origin[axis] + end * direction[axis];
    /* Leave headroom for integer boundary increments. Also rejects NaN/Inf. */
    if (!(p > T(-1073741824) && p < T(1073741824) &&
          q > T(-1073741824) && q < T(1073741824)))
      return false;
    const T base = ::floor(p);
    int cell = int(base);
    const int step = direction[axis] > T(0) ? 1 : (direction[axis] < T(0) ? -1 : 0);
    /* On an integer boundary, a negative ray immediately enters the lower cell. */
    if (step < 0 && p == base)
      --cell;
    cursor->origin[axis] = origin[axis];
    cursor->direction[axis] = direction[axis];
    cursor->cell[axis] = cell;
    cursor->step[axis] = step;
    /* A rounded position can appear to have entered a cell whose
     * incoming plane is still ahead of start. Retain that short real interval. */
    if (step) {
      const T incoming = (T(cell + (step < 0 ? 1 : 0)) - origin[axis]) / direction[axis];
      if (incoming > start)
        cursor->cell[axis] -= step;
    }
    cursor->next[axis] = step ?
                            (T(cursor->cell[axis] + (step > 0 ? 1 : 0)) - origin[axis]) /
                                direction[axis] :
                            end;
    /* Position evaluation (possibly fused) and division can round differently
     * at the clipped start. If this cell's outgoing plane rounds at or before
     * start, it has no represented interval in [start, end]. Enter the adjacent
     * cell rather than rejecting the ray or emitting a zero-length segment.
     * Never skip a plane whose represented time is greater than start. */
    if (step && cursor->next[axis] <= start) {
      cursor->cell[axis] += step;
      cursor->next[axis] =
          (T(cursor->cell[axis] + (step > 0 ? 1 : 0)) - origin[axis]) / direction[axis];
    }
    moving |= step != 0;
    if (!(cursor->next[axis] > start))
      return false;
  }
  cursor->valid = moving;
  return cursor->valid;
}

template<typename T>
ccl_device DeepGridStep deep_grid_next(ccl_private DeepGridCursor<T> *cursor,
                                      ccl_private DeepGridSegment<T> *segment)
{
  if (!cursor->valid)
    return DEEP_GRID_INVALID;
  if (cursor->current >= cursor->end)
    return DEEP_GRID_DONE;
  if (cursor->remaining == 0)
    return DEEP_GRID_LIMIT;
  T end = cursor->end;
  for (int axis = 0; axis < 3; ++axis)
    end = end < cursor->next[axis] ? end : cursor->next[axis];
  if (!(end > cursor->current)) {
    cursor->valid = false;
    return DEEP_GRID_INVALID;
  }
  segment->front = cursor->current;
  segment->back = end;
  for (int axis = 0; axis < 3; ++axis) {
    segment->cell[axis] = cursor->cell[axis];
    /* Advance every tied axis: edge/corner crossings never emit zero intervals.
     * Recompute from the integer boundary rather than accumulating a delta. */
    if (cursor->next[axis] == end && cursor->step[axis]) {
      cursor->cell[axis] += cursor->step[axis];
      cursor->next[axis] =
          (T(cursor->cell[axis] + (cursor->step[axis] > 0 ? 1 : 0)) - cursor->origin[axis]) /
          cursor->direction[axis];
    }
  }
  cursor->current = end;
  --cursor->remaining;
  return DEEP_GRID_SEGMENT;
}

CCL_NAMESPACE_END
