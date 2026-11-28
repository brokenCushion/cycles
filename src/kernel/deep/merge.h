/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "util/defines.h"
#include "kernel/deep/types.h"
#ifndef __KERNEL_GPU__
#  include <cmath>
#  include <cstring>
#endif

CCL_NAMESPACE_BEGIN

/* Camera sample event capacity fallback, as MoonRay does for deep volume
 * segments (VolumePixelBuffer::mergeSegments): a sample that records more
 * events than its capacity does not fail. Adjacent contiguous volume events of
 * one object with the most similar extinction rate are merged; a merge keeps
 * the pair's summed optical depth, so transmittance at every kept boundary is
 * unchanged and only the shape inside the merged span becomes a single
 * exponential. Only when no volume pair can merge, the nearest-depth adjacent
 * surface events of one object merge (alpha 1-(1-a)(1-b), nearer depth), like
 * MoonRay's same-ID hard-surface merging but without a depth tolerance.
 *
 * Bounded and deterministic for every backend: no heap, RNG or atomics. An
 * exact repeated most-similar search costs O(capacity) per freed slot, which a
 * GPU lane cannot afford for thousands of overflowing smoke steps (lanes of a
 * warp overflow at different times and serialize). Instead each call frees up
 * to `target` slots in two linear passes: histogram the extinction difference
 * of every mergeable adjacent pair by binary exponent, then compact in
 * runs: all below the exponent bin that reaches `target`, and pairs in that
 * bin, in buffer order, up to `target` in total. The most similar pairs therefore
 * merge first, within a factor of two in the unclamped bins. Like MoonRay,
 * compare each new member against the first extinction rate in the run.
 * Compute rates in FLOAT for selection only; depth and optical depth still
 * use the original doubles.
 *
 * Events are rewritten in place: [0, count) at `stride`, companions either at
 * the event index (planes) or packed in companion order (`compact`). Inputs
 * that capture validation would reject never merge. */
struct DeepMergeSpan {
  KernelDeepEvent event;
  KernelDeepDensity companion;
  double front, back, tau;
  float sigma; /* tau per unit depth, for merge selection only */
  bool has_companion;
};

ccl_device_inline bool deep_merge_has_companion(const KernelDeepEvent event,
                                                const bool density,
                                                const bool volume_companions)
{
  const KernelDeepEventKind kind = deep_event_type(event);
  return density && (kind == DEEP_VOLUME_CUBIC || (kind == DEEP_VOLUME && volume_companions));
}

ccl_device_inline float deep_merge_rate(const double tau, const double width)
{
  const float a = float(tau);
  const float b = float(width);
  /* Avoid subnormal operands and a potential intermediate overflow. The
   * rare extreme inputs use the wide calculation before converting to FLOAT. */
  if (tau == 0)
    return 0;
  float rate;
  if (!(a >= FLT_MIN && a <= FLT_MAX && b >= FLT_MIN && b <= FLT_MAX))
    rate = float(tau / width);
  else {
#ifdef __KERNEL_GPU__
    rate = __fdiv_rn(a, b);
#else
    rate = a / b;
#endif
  }
  /* CUDA uses flush-to-zero. Apply the same rule to merge costs on CPU
   * and OptiX; stored optical depths are independent of these rates. */
  return rate >= FLT_MIN ? rate : 0.0f;
}

ccl_device_inline DeepMergeSpan deep_merge_load(const ccl_global KernelDeepEvent *events,
                                                const ccl_global KernelDeepDensity *density,
                                                const int stride,
                                                const int index,
                                                const bool compact,
                                                const int companion,
                                                const bool volume_companions)
{
  DeepMergeSpan span;
  span.event = events[index * stride];
  span.has_companion = deep_merge_has_companion(span.event, density != nullptr, volume_companions);
  span.companion = {{0, 0, 0, 0}, 0, 0};
  if (span.has_companion) {
    span.companion = density[compact ? companion : index * stride];
    span.front = span.companion.front;
    span.back = span.companion.back;
    const float *b = span.companion.optical_depth;
    span.tau = deep_event_type(span.event) == DEEP_VOLUME ?
                   double(b[0]) + double(b[1]) :
                   ((double(b[0]) + double(b[1])) + (double(b[2]) + double(b[3]))) / 4;
  }
  else {
    span.front = double(span.event.front);
    span.back = double(span.event.back);
    span.tau = double(span.event.optical_depth);
  }
  span.sigma = deep_event_type(span.event) != DEEP_SURFACE && span.back > span.front ?
                   deep_merge_rate(span.tau, span.back - span.front) :
                   0.0f;
  return span;
}

/* Contiguous volume events of one object with valid depth and optical depth. */
ccl_device_inline bool deep_merge_volume_pair(const DeepMergeSpan a,
                                              const DeepMergeSpan b,
                                              const bool volume_companions,
                                              ccl_private float *cost)
{
  if (deep_event_type(a.event) == DEEP_SURFACE || deep_event_type(b.event) == DEEP_SURFACE ||
      deep_event_object(a.event) != deep_event_object(b.event) ||
      a.has_companion != b.has_companion || !(a.front > 0 && a.back > a.front) ||
      !(b.back > b.front) || a.back != b.front || !(a.tau >= 0 && b.tau >= 0))
    return false;
  const double tau = a.tau + b.tau;
  if (!(tau <= 3.4028234663852886e38 && b.back <= 3.4028234663852886e38))
    return false;
  /* A merged event without a companion keeps FLOAT bounds; they must stay ordered. */
  if (!(a.has_companion && volume_companions) && !(float(a.front) < float(b.back)))
    return false;
  /* Compare saturated rates in the widest bin, without subtracting inf-inf. */
  *cost = a.sigma <= FLT_MAX && b.sigma <= FLT_MAX ? fabsf(a.sigma - b.sigma) : FLT_MAX;
  return true;
}

ccl_device_inline DeepMergeSpan deep_merge_volume(const DeepMergeSpan a,
                                                  const DeepMergeSpan b,
                                                  const bool volume_companions)
{
  DeepMergeSpan span;
  span.front = a.front;
  span.back = b.back;
  span.tau = a.tau + b.tau;
  span.sigma = a.sigma;
  span.has_companion = a.has_companion && volume_companions;
  span.event = {(a.event.kind & ~DEEP_EVENT_KIND_MASK) | unsigned(DEEP_VOLUME),
                float(span.front), float(span.back), 0, float(span.tau)};
  span.companion = {{0, 0, 0, 0}, 0, 0};
  if (span.has_companion) {
    /* The same nonnegative two-FLOAT expansion as deep_volume_constant. */
    float high = float(span.tau);
    if (double(high) > span.tau)
      high = nextafterf(high, 0.0f);
    span.companion = {{high, float(span.tau - double(high)), 0, 0}, span.front, span.back};
  }
  return span;
}

/* Exponent bins: 0 holds zero and subnormal costs, then one binary exponent each
 * (outer ones clamped). */
ccl_device_inline int deep_merge_bin(const float cost)
{
#ifdef __KERNEL_GPU__
  const unsigned int bits = __float_as_uint(cost);
#else
  unsigned int bits;
  std::memcpy(&bits, &cost, sizeof(bits));
#endif
  if (bits < 0x00800000u)
    return 0;
  const int exponent = int(bits >> 23) - 94;
  return exponent < 1 ? 1 : (exponent > 63 ? 63 : exponent);
}

ccl_device_inline void deep_merge_store(ccl_global KernelDeepEvent *events,
                                        ccl_global KernelDeepDensity *density,
                                        const int stride,
                                        const int index,
                                        const bool compact,
                                        const int companion,
                                        const DeepMergeSpan span)
{
  events[index * stride] = span.event;
  if (span.has_companion)
    density[compact ? companion : index * stride] = span.companion;
}

/* When a full lane is still writing a smooth run, make room at its tail.
 * MoonRay compares subsequent segments with the first rate in the run and
 * accepts a difference of at most the largest rate divided by its compression
 * resolution (default 10). Use the larger of the two local rates to avoid a
 * distant dense interval setting the tolerance for this pair. */
ccl_device_inline bool deep_merge_tail(ccl_global KernelDeepEvent *events,
                                       ccl_global KernelDeepDensity *density,
                                       const int stride,
                                       const int count,
                                       ccl_private KernelDeepWriteState *write,
                                       ccl_private int *freed_companions)
{
  if (count < 2)
    return false;
  const int last = count - 1;
  const bool has_a = deep_merge_has_companion(
      events[(last - 1) * stride], density != nullptr, write->volume_companions);
  const bool has_b = deep_merge_has_companion(
      events[last * stride], density != nullptr, write->volume_companions);
  const int companion = int(write->companions) - int(has_a) - int(has_b);
  if (has_a != has_b || companion < 0)
    return false;
  DeepMergeSpan a = deep_merge_load(
      events, density, stride, last - 1, write->compact_density, companion, write->volume_companions);
  const DeepMergeSpan b = deep_merge_load(
      events, density, stride, last, write->compact_density, companion + int(has_a),
      write->volume_companions);
  if (write->tail_merge_rate >= 0)
    a.sigma = write->tail_merge_rate;
  float cost;
  if (!deep_merge_volume_pair(a, b, write->volume_companions, &cost))
    return false;
  const float scale = a.sigma > b.sigma ? a.sigma : b.sigma;
  if (!(scale <= FLT_MAX && cost <= scale * 0.1f))
    return false;
  const DeepMergeSpan span = deep_merge_volume(a, b, write->volume_companions);
  deep_merge_store(events, density, stride, last - 1, write->compact_density, companion, span);
  *freed_companions = int(has_a) + int(has_b) - int(span.has_companion);
  write->tail_merge_rate = a.sigma;
  return true;
}

/* Histogram of mergeable adjacent pairs by exponent bin; returns their number. */
ccl_device int deep_merge_histogram(const ccl_global KernelDeepEvent *events,
                                    const ccl_global KernelDeepDensity *density,
                                    const int stride,
                                    const int count,
                                    const bool compact,
                                    const bool volume_companions,
                                    ccl_private unsigned int *histogram)
{
  for (int i = 0; i < 64; ++i)
    histogram[i] = 0;
  int pairs = 0;
  DeepMergeSpan previous = deep_merge_load(events, density, stride, 0, compact, 0, volume_companions);
  int companion = previous.has_companion ? 1 : 0;
  for (int i = 1; i < count; ++i) {
    const DeepMergeSpan next = deep_merge_load(
        events, density, stride, i, compact, companion, volume_companions);
    companion += next.has_companion ? 1 : 0;
    float cost;
    if (deep_merge_volume_pair(previous, next, volume_companions, &cost)) {
      ++histogram[deep_merge_bin(cost)];
      ++pairs;
    }
    previous = next;
  }
  return pairs;
}

/* Smallest bin whose cumulative count reaches `wanted`; *before counts below it. */
ccl_device_inline int deep_merge_threshold(const ccl_private unsigned int *histogram,
                                           const unsigned int wanted,
                                           ccl_private unsigned int *before)
{
  unsigned int cumulative = 0;
  int bin = 0;
  for (; bin < 63; ++bin) {
    if (cumulative + histogram[bin] >= wanted)
      break;
    cumulative += histogram[bin];
  }
  *before = cumulative;
  return bin;
}

/* Frees at least one slot when any pair can merge, aiming for `target`.
 * Returns the freed event count; *freed_companions receives the change in
 * companion-bearing events (the compact companion cursor moves back by it). */
ccl_device_noinline int deep_merge_events(ccl_global KernelDeepEvent *events,
                                 ccl_global KernelDeepDensity *density,
                                 const int stride,
                                 const int count,
                                 const bool compact,
                                 const bool volume_companions,
                                 const int target,
                                 ccl_private int *freed_companions)
{
  *freed_companions = 0;
  if (count < 2)
    return 0;
  unsigned int histogram[64];
  const int pairs = deep_merge_histogram(
      events, density, stride, count, compact, volume_companions, histogram);
  if (pairs) {
    const int wanted = target < 1 ? 1 : (target < pairs ? target : pairs);
    unsigned int before;
    const int threshold = deep_merge_threshold(histogram, unsigned(wanted), &before);
    /* Reserve room for more expensive pairs only after the cheaper bins.
     * A run can free multiple slots without recalculating its extinction. */
    int quota = wanted - int(before);
    /* In-place compaction: outputs never pass the next unread input. The
     * first qualifying pair always merges (or follows an earlier merge). */
    int freed = 0, output = 0, output_companion = 0;
    DeepMergeSpan run = deep_merge_load(events, density, stride, 0, compact, 0, volume_companions);
    int companion = run.has_companion ? 1 : 0;
    for (int i = 1; i < count; ++i) {
      const DeepMergeSpan next = deep_merge_load(
          events, density, stride, i, compact, companion, volume_companions);
      companion += next.has_companion ? 1 : 0;
      float cost;
      if (freed < wanted && deep_merge_volume_pair(run, next, volume_companions, &cost)) {
        const int bin = deep_merge_bin(cost);
        if (bin < threshold || (bin == threshold && quota > 0)) {
          quota -= bin == threshold ? 1 : 0;
          const int companions = int(run.has_companion) + int(next.has_companion);
          run = deep_merge_volume(run, next, volume_companions);
          *freed_companions += companions - int(run.has_companion);
          ++freed;
          continue;
        }
      }
      deep_merge_store(events, density, stride, output, compact, output_companion, run);
      output_companion += run.has_companion ? 1 : 0;
      ++output;
      run = next;
    }
    deep_merge_store(events, density, stride, output, compact, output_companion, run);
    return freed;
  }
  /* Surfaces only: merge the single nearest-depth adjacent pair of one object. */
  int best = -1;
  float best_cost = 0;
  for (int i = 0; i + 1 < count; ++i) {
    const KernelDeepEvent a = events[i * stride], b = events[(i + 1) * stride];
    if (deep_event_type(a) != DEEP_SURFACE || deep_event_type(b) != DEEP_SURFACE ||
        deep_event_object(a) != deep_event_object(b) || !(a.front > 0 && b.front > 0) ||
        !(a.surface_alpha >= 0 && a.surface_alpha <= 1 && b.surface_alpha >= 0 &&
          b.surface_alpha <= 1))
      continue;
    const float cost = a.front < b.front ? b.front - a.front : a.front - b.front;
    if (best < 0 || cost < best_cost) {
      best = i;
      best_cost = cost;
    }
  }
  if (best < 0)
    return 0;
  const KernelDeepEvent a = events[best * stride], b = events[(best + 1) * stride];
  const float depth = a.front < b.front ? a.front : b.front;
  float alpha = float(1.0 - (1.0 - double(a.surface_alpha)) * (1.0 - double(b.surface_alpha)));
  alpha = alpha < 0 ? 0 : (alpha > 1 ? 1 : alpha);
  events[best * stride] = {a.kind, depth, depth, alpha,
                           a.optical_depth == b.optical_depth ? a.optical_depth : 0.0f};
  for (int i = best + 2; i < count; ++i) {
    events[(i - 1) * stride] = events[i * stride];
    if (density && !compact)
      density[(i - 1) * stride] = density[i * stride];
  }
  return 1;
}

CCL_NAMESPACE_END
