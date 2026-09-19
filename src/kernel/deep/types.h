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
};
enum KernelDeepEventKind : unsigned int { DEEP_SURFACE = 0, DEEP_VOLUME, DEEP_VOLUME_CUBIC };
constexpr unsigned int DEEP_MAX_EVENTS = 64;
constexpr unsigned int DEEP_MAX_VOLUME_EVENTS = 4096;
constexpr unsigned int DEEP_MAX_MEDIA = 64;

/* A visited convex medium has one entry/exit pair. Negative start means exited.
 * GPU storage is allocated per batch by the host, never per kernel thread. */
struct KernelDeepMedium {
  int object;
  float start;
};

struct KernelDeepEvent {
  KernelDeepEventKind kind;
  float front, back;
  /* Only the field selected by kind may be nonzero. Alpha is local surface
   * opacity; optical_depth is integrated scalar extinction along a ray segment. */
  float surface_alpha;
  float optical_depth;
};
/* Optional companion buffer for native grid cells, indexed like events.
 * Cubic Bernstein coefficients include physical ray-segment length, so their
 * average is the cell's optical depth. Surface records keep their 20-byte layout.
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
};

static_assert(sizeof(unsigned int) == 4 && sizeof(float) == 4, "Deep record scalar layout");
static_assert(sizeof(KernelDeepEvent) == 20, "Deep event layout");
static_assert(sizeof(KernelDeepDensity) == 32, "Deep density layout");
static_assert(sizeof(KernelDeepResult) == 12, "Deep result layout");
static_assert(sizeof(KernelDeepRecord) == 28, "Deep metadata layout");
static_assert(sizeof(KernelDeepMedium) == 8, "Deep medium layout");
