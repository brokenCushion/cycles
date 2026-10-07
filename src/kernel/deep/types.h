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
};
enum KernelDeepEventKind : unsigned int { DEEP_SURFACE = 0, DEEP_VOLUME, DEEP_VOLUME_CUBIC };
constexpr unsigned int DEEP_MAX_EVENTS = 64;
constexpr unsigned int DEEP_DEFAULT_VOLUME_EVENTS = 4096;
constexpr unsigned int DEEP_MAX_VOLUME_EVENTS = 8192;
constexpr unsigned int DEEP_MAX_MEDIA = 64;

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
  unsigned int x, y, sample, population;
  KernelDeepResult result;
  unsigned int payload_counts; // low/high 16 bits: event/companion stores (predicted in count pass)
};

static_assert(sizeof(unsigned int) == 4 && sizeof(float) == 4, "Deep record scalar layout");
static_assert(sizeof(KernelDeepEvent) == 20, "Deep event layout");
static_assert(sizeof(KernelDeepDensity) == 32, "Deep density layout");
static_assert(sizeof(KernelDeepResult) == 12, "Deep result layout");
static_assert(sizeof(KernelDeepRecord) == 32, "Deep metadata layout");
static_assert(sizeof(KernelDeepMedium) == 8, "Deep medium layout");
static_assert(DEEP_MAX_VOLUME_EVENTS < (1u << 16), "Deep write counters fit sixteen bits");
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
  unsigned int written_bytes = 0;
  unsigned int events = 0, companions = 0;
  unsigned int limit = ~0u;
  unsigned int density_limit = ~0u;
  bool count_only = false, compact_density = false, failed = false;
};
