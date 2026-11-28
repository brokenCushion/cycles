/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Plain data shared by kernels, host capture and spill storage. No allocations,
 * pointers, STL or backend-specific types. One lane owns its metadata and events. */
enum KernelDeepStatus : unsigned int {
  DEEP_EMPTY = 0,
  DEEP_ACTIVE,
  DEEP_COMPLETE,
  DEEP_SKIPPED,
  DEEP_FAILED,
};
enum KernelDeepError : unsigned int {
  DEEP_ERROR_NONE = 0,
  DEEP_ERROR_STATE,
  DEEP_ERROR_PRIMITIVE,
  DEEP_ERROR_DEPTH,
  DEEP_ERROR_CAPACITY,
  DEEP_ERROR_CACHE_MISS,
  DEEP_ERROR_EXTINCTION,
  DEEP_ERROR_PROGRESS,
  DEEP_ERROR_MEDIUM,
  DEEP_ERROR_EVENT_CAPACITY,
  DEEP_ERROR_GRID_STEPS,
  DEEP_ERROR_BOUNDARY_CAPACITY,
  DEEP_ERROR_MEDIA_CAPACITY,
  DEEP_ERROR_DIAGNOSTIC_CAPACITY,
};
enum KernelDeepEventKind : unsigned int { DEEP_SURFACE = 0, DEEP_VOLUME, DEEP_VOLUME_CUBIC };
constexpr unsigned int DEEP_MAX_EVENTS = 64;
constexpr unsigned int DEEP_DEFAULT_VOLUME_EVENTS = 4096;
constexpr unsigned int DEEP_MAX_VOLUME_EVENTS = 8192;
constexpr unsigned int DEEP_MAX_MEDIA = 64;

/* Diagnostic side channel, never part of the event/spill or EXR layout.
 * Raw intersections and accepted events are separate to expose deduplication. */
struct KernelDeepHit {
  int stage, type, primitive, segment, object;
  float u, v, t, facing, depth, alpha;
};
static_assert(sizeof(KernelDeepHit) == 44, "Deep diagnostic hit layout");
/* One counter, raw/accepted pairs at capacity, and the extra overflow hit.
 * Additional duplicate-boundary diagnostics fail explicitly rather than truncate. */
constexpr unsigned int DEEP_DIAGNOSTIC_RECORDS = 2 * DEEP_MAX_EVENTS + 2;
#ifndef __KERNEL_GPU__
struct KernelDeepTrace {
  void *context;
  void (*write)(void *, const KernelDeepHit &);
};
#endif

/* A visited convex medium has one entry/exit pair. Negative start means exited.
 * GPU storage is allocated per batch by the host, never per kernel thread. */
struct KernelDeepMedium {
  int object;
  float start;
};

struct KernelDeepEvent {
  /* Low 2 bits: event kind. Upper 30 bits: native object index. */
  unsigned int kind;
  float front, back;
  /* Alpha is local surface opacity. For volumes optical_depth is integrated
   * extinction; for surfaces this otherwise unused field carries facing:
   * +1 front, -1 back, 0 synthetic. The event and spill layout stay 20 bytes. */
  float surface_alpha;
  float optical_depth;
};
constexpr unsigned int DEEP_EVENT_KIND_MASK = 3u;
constexpr unsigned int DEEP_OBJECT_MASK = (1u << 30) - 1u;
constexpr unsigned long long DEEP_MAX_OBJECT_COUNT = 1ull << 30;
/* No C++ bitfields: shifts specify identical bytes on every backend. */
#ifdef __KERNEL_GPU__
#  define DEEP_INLINE ccl_device_inline
#else
#  define DEEP_INLINE constexpr
#endif
DEEP_INLINE bool deep_object_count_valid(const unsigned long long count)
{
  return count <= DEEP_MAX_OBJECT_COUNT;
}
DEEP_INLINE unsigned int deep_event_pack(const KernelDeepEventKind type, const int object = -1)
{
  /* Reserved type 3 makes invalid input fail capture validation explicitly. */
  return (unsigned(type) > unsigned(DEEP_VOLUME_CUBIC) || object < -1 ||
          (object >= 0 && unsigned(object) > DEEP_OBJECT_MASK)) ?
             DEEP_EVENT_KIND_MASK : unsigned(type) | ((unsigned(object) & DEEP_OBJECT_MASK) << 2);
}
DEEP_INLINE KernelDeepEventKind deep_event_type(const KernelDeepEvent &event)
{
  return KernelDeepEventKind(event.kind & DEEP_EVENT_KIND_MASK);
}
DEEP_INLINE unsigned int deep_event_object(const KernelDeepEvent &event)
{
  return event.kind >> 2;
}
#undef DEEP_INLINE
static_assert(DEEP_VOLUME_CUBIC < 4 && DEEP_OBJECT_MASK == 0x3fffffffu,
              "Deep event uses two kind bits and thirty object bits");
#ifndef __KERNEL_GPU__
static_assert(deep_event_pack(DEEP_VOLUME_CUBIC, int(DEEP_OBJECT_MASK)) == 0xfffffffeu,
              "Deep object packing preserves all thirty bits");
#endif
/* Optional companion buffer for native grid cells, indexed like events.
 * Cubic Bernstein coefficients include physical ray-segment length, so their
 * average is the cell's optical depth. All event kinds share the same object-index layout.
 * The host allocates this buffer; kernels never allocate it per thread. */
struct KernelDeepDensity {
  float optical_depth[4];
  /* Do not quantize thin cell boundaries before the export error check. */
  double front, back;
};
struct KernelDeepResult {
  KernelDeepStatus status;
  unsigned int count;
  KernelDeepError error;
};
struct KernelDeepRecord {
  /* Population in low 15 bits (host cap 4096), extended grid flag in bit 15,
   * shader point evaluations in the high 16 bits. */
  unsigned int x, y, sample, population;
  KernelDeepResult result;
  unsigned int payload_counts; // low/high 15 bits: event/companion stores (predicted in count pass)
};
/* Resource fallbacks one camera sample took instead of failing; the host counts
 * samples and warns once per render. Records carry them in payload_counts bit
 * 15 (merged) and bit 31 (coarsened); live counts stay below 1 << 15. */
enum KernelDeepFallback : unsigned int {
  DEEP_FALLBACK_MERGED = 1u,    /* events merged to fit the sample's event capacity */
  DEEP_FALLBACK_COARSENED = 2u, /* volume shader steps coarsened to fit step/evaluation limits */
  DEEP_FALLBACK_GRID_EXTENDED = 4u, /* continued exact native-grid traversal in chunks */
};
constexpr unsigned int DEEP_POPULATION_MASK = 0x7fffu;
constexpr unsigned int DEEP_POPULATION_GRID_EXTENDED = 1u << 15;
constexpr unsigned int DEEP_PAYLOAD_COUNT_MASK = 0x7fffu;
/* The maximum count is also the sentinel for a potentially longer ray.
 * Longer rays keep the compact in-kernel fallback. */
constexpr unsigned int DEEP_FLAT_COUNT_LIMIT = DEEP_PAYLOAD_COUNT_MASK;
constexpr unsigned int DEEP_PAYLOAD_MERGED = 1u << 15;
constexpr unsigned int DEEP_PAYLOAD_COARSENED = 1u << 31;

static_assert(sizeof(unsigned int) == 4 && sizeof(float) == 4, "Deep record scalar layout");
static_assert(sizeof(KernelDeepEvent) == 20, "Deep event layout");
static_assert(sizeof(KernelDeepDensity) == 32, "Deep density layout");
static_assert(sizeof(KernelDeepResult) == 12, "Deep result layout");
static_assert(sizeof(KernelDeepRecord) == 32, "Deep metadata layout");
static_assert(sizeof(KernelDeepMedium) == 8, "Deep medium layout");
static_assert(DEEP_MAX_VOLUME_EVENTS < (1u << 15), "Deep write counters fit fifteen bits");
struct DeepVolumeCompression {
  double anchor, last, anchor_tau, tau, lower, upper, roundoff, cutoff;
  double prefix_error, anchor_error;
  KernelDeepDensity singleton;
  int cells;
  int object = -1;
  bool active, terminated;
};

/* Host assigns flat ranges after the count pass; no device allocation. */
struct KernelDeepRange { unsigned int offset, count, work, density_offset, density_count; };
static_assert(sizeof(KernelDeepRange) == 20, "Deep flat range layout");
struct KernelDeepWriteState {
  unsigned int shader_evaluations = 0;
  unsigned int written_bytes = 0;
  unsigned int events = 0, companions = 0;
  unsigned int limit = ~0u;
  unsigned int density_limit = ~0u;
  unsigned int fallbacks = 0; /* KernelDeepFallback bits */
  float tail_merge_rate = -1.0f; /* first extinction rate of an active tail run */
  bool count_only = false, compact_density = false, failed = false;
  /* Merge/coarsen instead of failing at capacity (camera capture entry points
   * only); volume_companions: DEEP_VOLUME events carry a density companion. */
  bool fallback = false, volume_companions = false;
  bool defer_merge = false; /* exact flat GPU payload, merged during readback */
};
