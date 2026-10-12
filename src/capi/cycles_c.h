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

/** Phase 2 ABI, version 1.1.0 (experimental until the Phase 7 swap test).
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

/** Reflection added in API 1.1. All queries require init, are serialized and
 * may run on any thread except a log callback. Inputs are borrowed for the call;
 * outputs are caller-owned; returned strings are immutable library-owned UTF-8
 * valid until process exit. No native struct layout or pointer is exposed.
 * On failure output is unchanged. Common errors: NOT_INITIALIZED,
 * CALLBACK_REENTRY, OUT_OF_MEMORY, INTERNAL; NULL/undersized output or invalid
 * direction/index: INVALID_ARGUMENT; unknown node name: NOT_FOUND.
 * Built-in registry only: instance-specific OSL sockets belong to Phase 4.
 */
#define CCL_ERROR_NOT_FOUND INT32_C(7)
#define CCL_ERROR_NO_DEFAULT INT32_C(8)
#define CCL_ERROR_UNSUPPORTED INT32_C(9)

typedef int32_t ccl_socket_type;
#define CCL_SOCKET_UNDEFINED INT32_C(0)
#define CCL_SOCKET_BOOLEAN INT32_C(1)
#define CCL_SOCKET_FLOAT INT32_C(2)
#define CCL_SOCKET_INT INT32_C(3)
#define CCL_SOCKET_UINT INT32_C(4)
#define CCL_SOCKET_UINT64 INT32_C(5)
#define CCL_SOCKET_COLOR INT32_C(6)
#define CCL_SOCKET_VECTOR INT32_C(7)
#define CCL_SOCKET_POINT INT32_C(8)
#define CCL_SOCKET_NORMAL INT32_C(9)
#define CCL_SOCKET_POINT2 INT32_C(10)
#define CCL_SOCKET_CLOSURE INT32_C(11)
#define CCL_SOCKET_STRING INT32_C(12)
#define CCL_SOCKET_ENUM INT32_C(13)
#define CCL_SOCKET_TRANSFORM INT32_C(14)
#define CCL_SOCKET_NODE INT32_C(15)
#define CCL_SOCKET_BOOLEAN_ARRAY INT32_C(16)
#define CCL_SOCKET_FLOAT_ARRAY INT32_C(17)
#define CCL_SOCKET_INT_ARRAY INT32_C(18)
#define CCL_SOCKET_COLOR_ARRAY INT32_C(19)
#define CCL_SOCKET_VECTOR_ARRAY INT32_C(20)
#define CCL_SOCKET_POINT_ARRAY INT32_C(21)
#define CCL_SOCKET_NORMAL_ARRAY INT32_C(22)
#define CCL_SOCKET_POINT2_ARRAY INT32_C(23)
#define CCL_SOCKET_STRING_ARRAY INT32_C(24)
#define CCL_SOCKET_TRANSFORM_ARRAY INT32_C(25)
#define CCL_SOCKET_NODE_ARRAY INT32_C(26)

#define CCL_SOCKET_LINKABLE UINT32_C(1)
#define CCL_SOCKET_ANIMATABLE UINT32_C(2)
#define CCL_SOCKET_SVM_INTERNAL UINT32_C(4)
#define CCL_SOCKET_OSL_INTERNAL UINT32_C(8)
#define CCL_SOCKET_LINK_TEXTURE_GENERATED UINT32_C(16)
#define CCL_SOCKET_LINK_TEXTURE_NORMAL UINT32_C(32)
#define CCL_SOCKET_LINK_TEXTURE_UV UINT32_C(64)
#define CCL_SOCKET_LINK_TEXTURE_INCOMING UINT32_C(128)
#define CCL_SOCKET_LINK_INCOMING UINT32_C(256)
#define CCL_SOCKET_LINK_NORMAL UINT32_C(512)
#define CCL_SOCKET_LINK_POSITION UINT32_C(1024)
#define CCL_SOCKET_LINK_TANGENT UINT32_C(2048)
#define CCL_SOCKET_LINK_OSL_INITIALIZER UINT32_C(4096)

typedef int32_t ccl_socket_direction;
#define CCL_SOCKET_INPUT INT32_C(0)
#define CCL_SOCKET_OUTPUT INT32_C(1)

typedef struct ccl_node_type_desc {
  uint32_t size;
  const char *name;
  const char *base; /**< Empty string when there is no base type. */
  int32_t is_shader;
  int32_t is_abstract; /**< Native type has no create function. */
  uint32_t input_count, output_count;
} ccl_node_type_desc;

typedef struct ccl_socket_desc {
  uint32_t size;
  const char *name;
  const char *ui_name;
  ccl_socket_type type;
  uint32_t flags;         /**< CCL_SOCKET_* bit flags; never native enum ordinals. */
  const char *node_type;  /**< Required node base type, or empty when unconstrained. */
  int32_t has_default;    /**< Distinguishes no default from an empty array. */
  uint64_t default_count; /**< 1 for scalar defaults, array element count otherwise. */
  uint32_t enum_count;
} ccl_socket_desc;

typedef struct ccl_default_value {
  uint32_t size;
  ccl_socket_type type;     /**< Scalar element type (COLOR for COLOR_ARRAY, etc.). */
  int32_t signed_value;     /**< BOOLEAN (0/1), INT, or ENUM. */
  uint64_t unsigned_value;  /**< UINT or UINT64. */
  float floats[12];         /**< FLOAT:1, POINT2:2, COLOR/VECTOR/POINT/NORMAL:3;
                             * TRANSFORM:12, row-major affine 3x4, translation at 3/7/11. */
  const char *string_value; /**< STRING only; borrowed, never freed by host. */
  int32_t node_is_null;     /**< NODE default is null; no native pointer crosses ABI. */
} ccl_default_value;

typedef struct ccl_enum_value {
  uint32_t size;
  const char *name;
  int32_t value; /**< Numeric values need not be contiguous or unique. */
} ccl_enum_value;

/** Count registered built-in node types. Ownership/threading/errors as above.
 * The list is sorted by UTF-8 name and stable for this process, not across builds. */
CCL_API ccl_status CCL_CALL ccl_node_type_count(uint32_t *count);
/** Describe the type at index; includes inherited socket counts. Ownership,
 * threading and errors as above. Out-of-range index: INVALID_ARGUMENT. */
CCL_API ccl_status CCL_CALL ccl_node_type_info(uint32_t index, ccl_node_type_desc *out);
/** Describe a socket in native declaration order (including inherited sockets).
 * node_name and direction select the input/output list. Ownership/threading/errors
 * as above; NULL name or out-of-range index: INVALID_ARGUMENT. */
CCL_API ccl_status CCL_CALL ccl_socket_info(const char *node_name,
                                            ccl_socket_direction direction,
                                            uint32_t index,
                                            ccl_socket_desc *out);
/** Read one default element; scalar defaults use element=0. Only the field for
 * the returned scalar type is populated; unused fields are zero. Ownership,
 * threading and common errors as above. NO_DEFAULT for outputs/closures/missing
 * defaults; INVALID_ARGUMENT for an out-of-range element (including empty arrays).
 * UNSUPPORTED if a native non-null node default cannot be represented. */
CCL_API ccl_status CCL_CALL ccl_socket_default_value(const char *node_name,
                                                     ccl_socket_direction direction,
                                                     uint32_t index,
                                                     uint64_t element,
                                                     ccl_default_value *out);
/** Read one named enum entry, sorted by UTF-8 name. Ownership/threading/errors as
 * above; INVALID_ARGUMENT if enum_index is out of range or socket has no enum. */
CCL_API ccl_status CCL_CALL ccl_socket_enum_value(const char *node_name,
                                                  ccl_socket_direction direction,
                                                  uint32_t index,
                                                  uint32_t enum_index,
                                                  ccl_enum_value *out);

#ifdef __cplusplus
}
#endif
#endif
