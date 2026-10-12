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

/** Extensible enumeration: hosts MUST skip sockets with unknown type values.
 * A newer library reporting an unknown type is not an error. Never assume the
 * last currently defined value is an upper bound in future libraries. */
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

/** Scene editing added in API 1.2. All functions below require init and run
 * serialized on any thread except a callback. No rendering occurs in Phase 3.
 * Inputs are borrowed for the call; setters copy values, arrays and strings.
 * Handles are library-owned: never dereference/free them or use after deletion
 * or scene destruction. A scene owns all its nodes. Destroying it invalidates
 * every node handle. NULL is not a valid scene/node (except a null reference).
 * Common errors: NOT_INITIALIZED, INVALID_ARGUMENT (invalid handle/NULL output),
 * CALLBACK_REENTRY, OUT_OF_MEMORY, INTERNAL. Output values remain unchanged on
 * failure except get_array's documented required-capacity result. */
typedef struct ccl_scene ccl_scene;
typedef struct ccl_node ccl_node;
typedef uint64_t ccl_socket_id;
#define CCL_ERROR_TYPE_MISMATCH INT32_C(10)
#define CCL_ERROR_IN_USE INT32_C(11)
#define CCL_ERROR_BUFFER_TOO_SMALL INT32_C(12)
typedef struct ccl_float2 {
  float x, y;
} ccl_float2;
typedef struct ccl_float3 {
  float x, y, z;
} ccl_float3;
/** Fixed layout: row-major affine 3x4; translations at m[3], m[7], m[11]. */
typedef struct ccl_transform {
  float m[12];
} ccl_transform;

/** Create an editable scene using native default SceneParams and a CPU device.
 * No kernels are loaded and no GPU is allocated. Caller owns the handle until
 * scene_destroy; common editing errors above, UNSUPPORTED without a CPU device.
 * Session adoption/render-device selection belongs to the later session API. */
CCL_API ccl_status CCL_CALL ccl_scene_create(ccl_scene **out);
/** Destroy scene and all its nodes, releasing native resources. Common editing
 * ownership/threading/errors above. References within the scene need not be
 * cleared first; native destruction order handles them. */
CCL_API ccl_status CCL_CALL ccl_scene_destroy(ccl_scene *scene);
/** Borrow camera, dicing_camera, film, integrator, background or scene_attribute.
 * Cannot delete these nodes. Common editing contract; unknown name: NOT_FOUND.
 * Repeated queries return the same handle while the scene lives. */
CCL_API ccl_status CCL_CALL ccl_scene_singleton(ccl_scene *scene,
                                                const char *type,
                                                ccl_node **out);
/** Create a scene-owned object, mesh, hair, pointcloud, volume, shader, pass,
 * particle_system or concrete light type from reflection (pointlight etc.).
 * Copies optional name (NULL uses native type name). Shaders start with an empty
 * native output graph. Common editing contract;
 * unknown type: NOT_FOUND; abstract/shader-graph/singleton type: UNSUPPORTED. */
CCL_API ccl_status CCL_CALL ccl_node_create(ccl_scene *scene,
                                            const char *type,
                                            const char *name,
                                            ccl_node **out);
/** Delete a node belonging to scene. Common editing contract; referenced node:
 * IN_USE; borrowed singleton/default node: UNSUPPORTED; wrong scene:
 * INVALID_ARGUMENT. Clear incoming references first. Cycles retains deleted
 * shaders internally until scene destruction; the API handle is invalidated. */
CCL_API ccl_status CCL_CALL ccl_node_delete(ccl_scene *scene, ccl_node *node);
/** Tag scene managers after a group of socket edits. Setters already mark socket
 * changes; this call propagates them via native tag_update. Common contract.
 * Geometry updates conservatively request a rebuild. */
CCL_API ccl_status CCL_CALL ccl_node_tag_update(ccl_node *node);
/** Resolve an INPUT socket once. ID is stable for this loaded library instance,
 * for the exact node type, across scenes and edits; never persist or share it
 * with another library instance/build. Zero is invalid. Common contract;
 * unknown node type/socket: NOT_FOUND. No native pointer crosses the boundary. */
CCL_API ccl_status CCL_CALL ccl_socket_find(const char *node_type,
                                            const char *name,
                                            ccl_socket_id *out);

/** Typed socket access contract (applies to every set/get and _id variant below):
 * names resolve INPUT sockets; IDs bypass name lookup and must belong to the
 * exact node type. Unknown name: NOT_FOUND; wrong/invalid ID: INVALID_ARGUMENT;
 * wrong accessor type: TYPE_MISMATCH. Common editing ownership/threading/errors
 * above. Getters write caller-owned outputs only on success. Setters copy inputs.
 * bool is int32 0/1 (other values INVALID_ARGUMENT); enum is an existing numeric
 * entry (unknown entry INVALID_ARGUMENT). float3 accepts COLOR/VECTOR/POINT/NORMAL.
 * String getters borrow UTF-8 until the next scene edit/destruction; never free.
 * Node references may be NULL, otherwise must be live in the same scene and
 * satisfy the socket's node-type constraint (INVALID_ARGUMENT/TYPE_MISMATCH).
 * A returned node handle is borrowed from the scene, not separately owned.
 * UNDEFINED/CLOSURE and future unrecognized types have no value accessor. */
/** Copy a bool into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_bool(ccl_node *node, const char *socket, int32_t value);
/** Read a bool from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_bool(ccl_node *node, const char *socket, int32_t *out);
/** Copy a bool into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_bool_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 int32_t value);
/** Read a bool from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_bool_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 int32_t *out);
/** Copy a float into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float(ccl_node *node, const char *socket, float value);
/** Read a float from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float(ccl_node *node, const char *socket, float *out);
/** Copy a float into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float_id(ccl_node *node,
                                                  ccl_socket_id socket,
                                                  float value);
/** Read a float from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float_id(ccl_node *node,
                                                  ccl_socket_id socket,
                                                  float *out);
/** Copy a int into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_int(ccl_node *node, const char *socket, int32_t value);
/** Read a int from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_int(ccl_node *node, const char *socket, int32_t *out);
/** Copy a int into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_int_id(ccl_node *node,
                                                ccl_socket_id socket,
                                                int32_t value);
/** Read a int from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_int_id(ccl_node *node,
                                                ccl_socket_id socket,
                                                int32_t *out);
/** Copy a uint into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_uint(ccl_node *node, const char *socket, uint32_t value);
/** Read a uint from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_uint(ccl_node *node, const char *socket, uint32_t *out);
/** Copy a uint into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_uint_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 uint32_t value);
/** Read a uint from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_uint_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 uint32_t *out);
/** Copy a uint64 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_uint64(ccl_node *node,
                                                const char *socket,
                                                uint64_t value);
/** Read a uint64 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_uint64(ccl_node *node, const char *socket, uint64_t *out);
/** Copy a uint64 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_uint64_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   uint64_t value);
/** Read a uint64 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_uint64_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   uint64_t *out);
/** Copy a float2 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float2(ccl_node *node,
                                                const char *socket,
                                                ccl_float2 value);
/** Read a float2 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float2(ccl_node *node,
                                                const char *socket,
                                                ccl_float2 *out);
/** Copy a float2 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float2_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   ccl_float2 value);
/** Read a float2 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float2_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   ccl_float2 *out);
/** Copy a float3 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float3(ccl_node *node,
                                                const char *socket,
                                                ccl_float3 value);
/** Read a float3 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float3(ccl_node *node,
                                                const char *socket,
                                                ccl_float3 *out);
/** Copy a float3 into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_float3_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   ccl_float3 value);
/** Read a float3 from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_float3_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   ccl_float3 *out);
/** Copy a string into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_string(ccl_node *node,
                                                const char *socket,
                                                const char *value);
/** Read a string from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_string(ccl_node *node,
                                                const char *socket,
                                                const char **out);
/** Copy a string into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_string_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   const char *value);
/** Read a string from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_string_id(ccl_node *node,
                                                   ccl_socket_id socket,
                                                   const char **out);
/** Copy a enum into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_enum(ccl_node *node, const char *socket, int32_t value);
/** Read a enum from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_enum(ccl_node *node, const char *socket, int32_t *out);
/** Copy a enum into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_enum_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 int32_t value);
/** Read a enum from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_enum_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 int32_t *out);
/** Copy a transform into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_transform(ccl_node *node,
                                                   const char *socket,
                                                   ccl_transform value);
/** Read a transform from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_transform(ccl_node *node,
                                                   const char *socket,
                                                   ccl_transform *out);
/** Copy a transform into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_transform_id(ccl_node *node,
                                                      ccl_socket_id socket,
                                                      ccl_transform value);
/** Read a transform from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_transform_id(ccl_node *node,
                                                      ccl_socket_id socket,
                                                      ccl_transform *out);
/** Copy a node into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_node(ccl_node *node, const char *socket, ccl_node *value);
/** Read a node from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_node(ccl_node *node, const char *socket, ccl_node **out);
/** Copy a node into an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_node_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 ccl_node *value);
/** Read a node from an input socket; typed access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_node_id(ccl_node *node,
                                                 ccl_socket_id socket,
                                                 ccl_node **out);

/** Array access uses the exact CCL_SOCKET_*_ARRAY type, not its scalar type.
 * Host elements: BOOLEAN_ARRAY int32_t (0/1), INT_ARRAY int32_t, FLOAT_ARRAY float,
 * POINT2_ARRAY ccl_float2, COLOR/VECTOR/POINT/NORMAL_ARRAY ccl_float3,
 * TRANSFORM_ARRAY ccl_transform, STRING_ARRAY const char*, NODE_ARRAY ccl_node* (non-null entries
 * only). Elements are tightly packed, properly aligned C values, not native layouts. Set copies
 * count elements (and pointed-to strings); NULL data allowed only at count=0. Invalid
 * bool/string/reference/overflow: INVALID_ARGUMENT; unknown array type: UNSUPPORTED; socket type
 * mismatch: TYPE_MISMATCH. No partial writes. Get copies up to capacity elements only if capacity
 * >= required. Always writes required on success or BUFFER_TOO_SMALL; data is unchanged on
 * BUFFER_TOO_SMALL. Query length with data=NULL, capacity=0 (success even for nonempty arrays).
 * Returned strings/node handles are borrowed as in typed access. Common editing
 * ownership/threading/errors and name/ID rules above apply. */
/** Copy array into socket; array access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_array(
    ccl_node *node, const char *socket, ccl_socket_type type, const void *data, uint64_t count);
/** Copy array from socket or query length; array access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_array(ccl_node *node,
                                               const char *socket,
                                               ccl_socket_type type,
                                               void *data,
                                               uint64_t capacity,
                                               uint64_t *required);
/** Copy array into socket; array access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_set_array_id(
    ccl_node *node, ccl_socket_id socket, ccl_socket_type type, const void *data, uint64_t count);
/** Copy array from socket or query length; array access contract above. */
CCL_API ccl_status CCL_CALL ccl_node_get_array_id(ccl_node *node,
                                                  ccl_socket_id socket,
                                                  ccl_socket_type type,
                                                  void *data,
                                                  uint64_t capacity,
                                                  uint64_t *required);

#ifdef __cplusplus
}
#endif
#endif
