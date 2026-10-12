/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CYCLES_C_H
#define CYCLES_C_H

#include <stdint.h>

#if defined(_WIN32)
#  define CCL_CALL __cdecl
#  if defined(CCL_C_API_BUILD)
#    define CCL_API __declspec(dllexport)
#  else
#    define CCL_API
#  endif
#else
#  define CCL_CALL
#  define CCL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Phase 1 ABI, version 1.0.0 (experimental until the Phase 7 swap test).
 * Use default platform struct alignment. Initialize every struct to zero and
 * set size to sizeof(struct). A larger size is accepted; unknown tail bytes
 * are ignored/preserved. A smaller size returns CCL_ERROR_INVALID_ARGUMENT.
 * All text is NUL-terminated UTF-8. Pointers must reference valid accessible
 * memory; invalid pointers cannot be diagnosed. No C++ exceptions escape.
 * Calls are thread-safe unless stated otherwise, but must not race DLL unload.
 * Load one Cycles library per process and retain it until process exit; backend
 * driver/global teardown and simultaneous library versions are not yet supported.
 */
typedef int32_t ccl_status;
#define CCL_SUCCESS INT32_C(0)
#define CCL_ERROR_INVALID_ARGUMENT INT32_C(1)
#define CCL_ERROR_NOT_INITIALIZED INT32_C(2)
#define CCL_ERROR_ALREADY_INITIALIZED INT32_C(3)
#define CCL_ERROR_OUT_OF_MEMORY INT32_C(4)
#define CCL_ERROR_INTERNAL INT32_C(5)
#define CCL_ERROR_CALLBACK_REENTRY INT32_C(6)

typedef struct ccl_version {
  uint32_t size;
  uint32_t major, minor, patch;
} ccl_version;

typedef struct ccl_init_params {
  uint32_t size;
  /** Absolute Cycles resource directory (kernel/ and shader/ beneath it).
   * Required: a DLL cannot infer this from the host executable location. */
  const char *resource_path;
  /** Absolute writable user-resource directory; required. Does not override
   * OS temporary paths or independently select an OSL shader/cache directory. */
  const char *user_path;
} ccl_init_params;

typedef struct ccl_device_desc {
  uint32_t size;
  /** Library-owned immutable strings, valid until process exit. Do not free. */
  const char *id;
  const char *description;
  /** Backend name, e.g. CPU, CUDA, OPTIX; hosts must tolerate new names. */
  const char *backend;
  /** Empty if native discovery reported no error. */
  const char *error;
  int32_t display_device;
  int32_t has_osl;
  int32_t has_nanovdb;
  int32_t meets_driver_requirement;
} ccl_device_desc;

/** Log severity: explicit values independent of native Cycles enum ordinals. */
typedef int32_t ccl_log_level;
#define CCL_LOG_FATAL INT32_C(0)
#define CCL_LOG_ERROR INT32_C(1)
#define CCL_LOG_WARNING INT32_C(2)
#define CCL_LOG_INFO INT32_C(3)
#define CCL_LOG_DEBUG INT32_C(4)
#define CCL_LOG_TRACE INT32_C(5)
/** Runs synchronously, serialized, possibly on a worker thread. message is
 * borrowed only during the call; user remains host-owned. Must not throw,
 * block waiting for API work, or reenter any API except ccl_last_error(). */
typedef void(CCL_CALL *ccl_log_fn)(ccl_log_level level, const char *message, void *user);

/** Write this library's API version, not the underlying Cycles version.
 * out is caller-owned; writes only known fields. Any thread, before/after init.
 * Returns INVALID_ARGUMENT for NULL/undersized out, CALLBACK_REENTRY from a
 * callback. On error out is unchanged. */
CCL_API ccl_status CCL_CALL ccl_api_version(ccl_version *out);

/** Query a compile-time capability: cpu, cuda, optix, hip, hiprt, metal,
 * oneapi, osl. Unknown names return success with 0, including deep until its
 * C API extension exists. A compiled backend may have no available hardware.
 * name is borrowed for this call; supported is caller-owned (written on success).
 * Any thread, before/after init. NULL arguments: INVALID_ARGUMENT; callback:
 * CALLBACK_REENTRY. Does not probe drivers. */
CCL_API ccl_status CCL_CALL ccl_has_feature(const char *name, int32_t *supported);

/** Initialize process-wide Cycles resource paths. Copies both strings before
 * return. Does not create directories or verify resource completeness.
 * Any thread; serialized. Repeating identical paths succeeds; different paths
 * return ALREADY_INITIALIZED. NULL/undersized params, empty/relative paths:
 * INVALID_ARGUMENT. Allocation failure: OUT_OF_MEMORY; other native exception:
 * INTERNAL. CALLBACK_REENTRY in a callback. No rendering is started. */
CCL_API ccl_status CCL_CALL ccl_init(const ccl_init_params *params);

/** Borrow this thread's last error message. Valid until its next status-returning
 * API call; successful calls clear it. Never free. Any thread, including a log
 * callback; never fails, never returns NULL. */
CCL_API const char *CCL_CALL ccl_last_error(void);

/** Set/replace a host-owned callback and user pointer; NULL disables it (logs
 * then go to stderr). Native severity filtering remains unchanged. May precede
 * init. Any thread; waits for an in-flight callback before returning, so the old
 * user pointer can then be freed. CALLBACK_REENTRY if called from a callback;
 * INTERNAL on synchronization failure. Callback must remain valid until replaced.
 */
CCL_API ccl_status CCL_CALL ccl_set_log_callback(ccl_log_fn fn, void *user);

/** Discover native devices once and write immutable snapshot length. Driver
 * discovery may be slow and emit logs. count is caller-owned; unchanged on error.
 * Any thread, serialized. Requires init (NOT_INITIALIZED); NULL: INVALID_ARGUMENT;
 * allocation/native exception: OUT_OF_MEMORY/INTERNAL; CALLBACK_REENTRY in callback.
 */
CCL_API ccl_status CCL_CALL ccl_device_count(uint32_t *count);

/** Copy one device's metadata from the same immutable snapshot as device_count.
 * out is caller-owned, strings remain library-owned; no device is allocated.
 * Any thread, serialized; may perform first discovery. Errors as device_count,
 * plus INVALID_ARGUMENT for an invalid index or NULL/undersized out. On error
 * out is unchanged. Device indices are local to this library instance only.
 */
CCL_API ccl_status CCL_CALL ccl_device_info(uint32_t index, ccl_device_desc *out);

#ifdef __cplusplus
}
#endif
#endif
