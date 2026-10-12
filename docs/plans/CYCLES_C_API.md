# Plan: stable C API and shared-library build for Cycles

Status: Phase 1 accepted; Phase 2 implemented and tested, awaiting review (2026-10-12).
Requested by the GafferCycles author.
Branch: `c-api`, created from `deep-output`. All API code lives in `src/capi/`.

## 1. Goal

Let host applications (first: Gaffer) load Cycles as a shared library
(`cycles_c.dll` / `libcycles_c.so` / `.dylib`) through a small, stable C API.
A host built once must keep working when only the Cycles library file is
replaced by a newer or custom build (for example, this repo's deep build).

Today Gaffer compiles against Cycles' C++ headers and static libraries. Its
build must reproduce Cycles' internal compile definitions exactly, because they
change class layouts (Gaffer `SConstruct`: "When encountering mysterious
GafferCycles memory corruptions, your first port of call should be to ensure
that these defines line up with Cycles' build-time settings"). Every Cycles
upgrade means rebuilding Gaffer, and Gaffer supports one Cycles version.

## 2. What GafferCycles uses today (inventory)

Source: GafferHQ/gaffer `src/GafferCycles` (commit `afb285e`, 2026-10-09),
about 11,300 lines in 44 files. 165 distinct `ccl::` names, 945 uses.

| Area | Usage | Main files |
| --- | --- | --- |
| Generic nodes and sockets | `NodeType::find`, `NodeType::type_names`, `SocketType`, `Node::set/get_*`, `NodeEnum`, `ParamValue` | `SocketAlgo.cpp` (701 lines) |
| Type introspection for UI | every node type with socket names, types, defaults, enums | `IECoreCycles.cpp` |
| Shader networks | `ShaderGraph`, `ShaderNode` created by type name, `connect`, OSL (`scene/osl.h`), image and environment textures | `ShaderNetworkAlgo.cpp` (1,695 lines) |
| Geometry | `Mesh` (including subdivision), `Hair`, `PointCloud`, `Volume` (VDB), spheres, instancing; `AttributeSet`, `Attribute::standard_name`, attribute elements | `MeshAlgo`, `CurvesAlgo`, `PointsAlgo`, `VDBAlgo`, `GeometryAlgo`, `PointInstancer` |
| Scene items | `Object` (tfm, motion, visibility, light and shadow sets, lightgroup, pass id), `Light`, `Camera` (perspective, panorama), `Background`, `Film`, `Integrator`, `Pass` (incl. cryptomatte) | `Renderer.cpp`, `Attributes.cpp`, `CameraAlgo.cpp` |
| Rendering | `SessionParams`, `SceneParams`, `Device::available_devices`, `DeviceInfo`, `Session::start/reset/set_pause/wait/cancel/set_samples`, `Progress` callbacks and errors, `Scene::need_reset`, `delete_nodes` | `Renderer.cpp` (2,260 lines) |
| Output | `OutputDriver` subclasses implementing `write_render_tile` and `update_render_tile`, reading `Tile::get_pass_pixels` | `IEDisplayOutputDriver`, `OIIOOutputDriver` |
| Utilities | `ustring`, math types (`float2/3/4`, `Transform`, `array<T>`), logging, murmur hash | throughout |

Most of this already goes through Cycles' reflection system (named node types
with named, typed sockets). That is what makes a small, generic, stable API
practical.

## 3. Design principles

1. **C only at the boundary.** No C++ types, exceptions, STL or templates
   cross the API. Exported symbols are C functions with a `ccl_` prefix. A
   test checks the export table contains nothing else.
2. **Opaque handles.** `ccl_session*`, `ccl_scene*`, `ccl_node*`,
   `ccl_shader_graph*`, and so on. Hosts never see struct layouts.
3. **Generic first.** Nodes are created and configured by type name and socket
   name through Cycles' reflection. New node types and sockets in future Cycles
   versions need no API change. Typed convenience functions are added only where
   reflection can't reach (mesh attributes, session control, outputs).
4. **Introspection.** Hosts can enumerate node types, sockets, socket types,
   defaults and enum values at runtime, so they can build UIs and adapt to the
   loaded library version.
5. **Versioning.**
   - `ccl_api_version(&version)` writes `{size, major, minor, patch}`. Major changes mean a
     breaking change (avoid). Minor adds functions.
   - `ccl_has_feature("deep", &supported)` and similar report optional capabilities.
   - Structs passed across the boundary start with a `size` field so they can
     grow.
   - Enums are `int32_t` with explicit values, never reordered.
6. **Errors.** Every fallible call returns `ccl_status` (an int code). The
   message is available from `ccl_last_error()`, which is thread-local. No
   exceptions escape; all are caught at the boundary.
7. **Ownership.**
   - The library owns nodes it creates.
   - Arrays passed in are copied before the call returns.
   - Strings returned are valid until the next call on the same handle, or are
     copied into host buffers (`size`/`capacity` pattern).
8. **Threading.**
   - Scene edits are allowed only when the session is not rendering
     (paused/reset), matching how GafferCycles works today. The API documents
     and checks this, and returns an error instead of corrupting state.
   - Callbacks (tiles, progress, logging) may run on Cycles worker threads.
9. **No behaviour change to Cycles.** The API is a wrapper. Rendering results
   through the API must be identical to the same scene through the standalone
   XML path.
10. **Build.** A CMake option `WITH_CYCLES_C_API` builds `cycles_c` as a
    SHARED library, linking the existing static Cycles libraries inside it,
    with symbol visibility hidden by default and only `ccl_` functions
    exported. Dependencies (OIIO, OSL, Embree, OpenVDB, etc.) stay internal to
    the library.

## 4. API surface and future outline

Phase 1's actual contract is [cycles_c.h](../../src/capi/cycles_c.h).
It uses fixed-width status codes and size-prefixed structs, documented ownership,
threading and error behavior. The seven Phase 1 functions are:

```c
ccl_status  ccl_api_version(ccl_version *out);
ccl_status  ccl_has_feature(const char *name, int32_t *supported);
ccl_status  ccl_init(const ccl_init_params *params);
const char *ccl_last_error(void);
ccl_status  ccl_set_log_callback(ccl_log_fn fn, void *user);
ccl_status  ccl_device_count(uint32_t *count);
ccl_status  ccl_device_info(uint32_t index, ccl_device_desc *out);
```

Phase 2 (API 1.1) adds these fully documented queries. The header defines
`ccl_node_type_desc`, `ccl_socket_desc`, `ccl_default_value`, `ccl_enum_value`,
explicit socket-type/flag constants and input/output direction values.

```c
ccl_status ccl_node_type_count(uint32_t *count);
ccl_status ccl_node_type_info(uint32_t index, ccl_node_type_desc *out);
ccl_status ccl_socket_info(const char *node_name, ccl_socket_direction direction,
                           uint32_t index, ccl_socket_desc *out);
ccl_status ccl_socket_default_value(const char *node_name, ccl_socket_direction direction,
                                    uint32_t index, uint64_t element, ccl_default_value *out);
ccl_status ccl_socket_enum_value(const char *node_name, ccl_socket_direction direction,
                                 uint32_t index, uint32_t enum_index, ccl_enum_value *out);
```

Single-record queries replace the provisional bulk socket/enum arrays: each
output has its own size field, avoiding a future struct-size/array-stride ABI
ambiguity. Node descriptors give both socket counts; socket descriptors give
default and enum counts. Defaults are one typed C scalar/vector/transform/string
per element. No raw Cycles default-value storage escapes.

Refinements from the initial outline:

- Fallible queries and callback registration return status; errors do not look
  like empty device lists. Version output can grow without a struct-by-value ABI.
- Init accepts native Cycles resource and user roots. Independent kernel, OSL and
  temp-path overrides are not promised by the existing native path interface.
- Enumeration copies an immutable native-device snapshot. Its strings remain
  library-owned and valid for process lifetime. Backend feature flags describe
  compiled support, not hardware availability. `deep` remains false until Phase 8.
- Logging has serialized callbacks and an explicit no-reentry rule. Replacing a
  callback waits for it to finish before the host releases the old user data.
- Phase 1 retains one loaded library until process exit; unloading and simultaneous
  versions require lifecycle work before the Phase 7 compatibility promise.

Before later phases, retain the status/out convention for fallible queries; define node/graph deletion invalidation, session edit
synchronization (pause alone may not suffice), and callback/buffer lifetimes.
No later-phase function below is implemented or part of the exported ABI yet.

```c
/* Session and scene */
ccl_status  ccl_session_create(const ccl_session_params *params, ccl_session **out);
void        ccl_session_destroy(ccl_session *s);
ccl_scene  *ccl_session_scene(ccl_session *s);
ccl_status  ccl_session_start(ccl_session *s);
ccl_status  ccl_session_pause(ccl_session *s, int pause);
ccl_status  ccl_session_reset(ccl_session *s, const ccl_buffer_params *buffer);
ccl_status  ccl_session_wait(ccl_session *s);
ccl_status  ccl_session_cancel(ccl_session *s);
ccl_status  ccl_session_set_samples(ccl_session *s, int samples);
ccl_status  ccl_session_progress(ccl_session *s, ccl_progress *out);
void        ccl_session_set_progress_callback(ccl_session *s, ccl_progress_fn fn, void *user);

/* Nodes (scene-level: object, mesh, hair, pointcloud, volume, light, shader, pass;
 * singletons: camera, film, integrator, background) */
ccl_status  ccl_node_create(ccl_scene *scene, const char *type, const char *name, ccl_node **out);
ccl_status  ccl_node_delete(ccl_scene *scene, ccl_node *node);
ccl_node   *ccl_scene_singleton(ccl_scene *scene, const char *type);   /* "camera", "film", ... */
ccl_status  ccl_node_set_float(ccl_node *n, const char *socket, float v);
ccl_status  ccl_node_set_int / _bool / _float3 / _string / _transform / _enum(...);
ccl_status  ccl_node_set_array(ccl_node *n, const char *socket, ccl_type t, const void *data, size_t count);
ccl_status  ccl_node_set_node(ccl_node *n, const char *socket, ccl_node *ref);   /* object->geometry */
ccl_status  ccl_node_set_node_array(ccl_node *n, const char *socket, ccl_node *const *refs, size_t count);
ccl_status  ccl_node_get_*(...);
ccl_status  ccl_node_tag_update(ccl_node *n);

/* Geometry attributes (beyond sockets) */
ccl_status  ccl_geometry_add_attribute(ccl_node *geom, const char *name, int standard,
                                       ccl_type t, ccl_attr_element e, const void *data, size_t count);

/* Shader graphs */
ccl_status  ccl_shader_graph_create(ccl_scene *scene, ccl_shader_graph **out);
ccl_status  ccl_shader_graph_add(ccl_shader_graph *g, const char *node_type, ccl_node **out);
ccl_status  ccl_shader_graph_connect(ccl_shader_graph *g, ccl_node *from, const char *out_socket,
                                     ccl_node *to, const char *in_socket);
ccl_status  ccl_shader_graph_output(ccl_shader_graph *g, ccl_node **out);
ccl_status  ccl_shader_set_graph(ccl_node *shader, ccl_shader_graph *g);   /* takes ownership */
ccl_status  ccl_shader_graph_add_osl(ccl_shader_graph *g, const char *path_or_source, ccl_node **out);

/* Outputs */
ccl_status  ccl_session_set_output(ccl_session *s, const ccl_output_callbacks *cb, void *user);
/* callbacks: write_tile / update_tile receive a ccl_tile*; host calls
 * ccl_tile_info(...) and ccl_tile_get_pass_pixels(tile, pass, channels, float *dst) */

/* Optional extension: deep (only when ccl_has_feature("deep")) */
ccl_status  ccl_session_set_deep_output(ccl_session *s, const ccl_deep_callbacks *cb, void *user);
/* deep_tile callback; host reads per-pixel samples with ccl_deep_tile_get_pixel(...) */
```

## 5. Phases (each ends with a stop for review)

### Phase 1: Skeleton and build
- Branch `c-api` from `deep-output`. Folder `src/capi/` with `cycles_c.h`
  (public, plain C, documented) and the implementation in C++.
- CMake option `WITH_CYCLES_C_API`: a shared library target with hidden
  visibility. On Windows, export through a `.def` file or `__declspec(dllexport)`
  macros.
- Implement version, features, init, last error, logging, device enumeration.
- Add a C test program (`src/capi/test/`, compiled as C, not C++) that loads the
  library and prints the version and devices.
- Check: an exported-symbol test (`dumpbin /exports` / `nm -D`) confirms only
  `ccl_*` symbols are exported.
- Acceptance: builds on Windows (clang-cl toolchain as the rest of the repo),
  the C test runs, and existing regression is unaffected.

Phase 1 results (2026-10-12):
- Windows clang-cl 20.1.8 Release build and isolated installation passed, with
  deep disabled. All 548 installed paths stayed within the new D: prefix.
- Plain-C dynamic host, private contract tests and exact export-table check
  passed; installed standalone version smoke also passed (4/4 CTests).
- The installed DLL also passed with the C host in a separate directory. The
  host imports only Windows/C runtime libraries, not Cycles or its C++ libraries.
- CUDA and OptiX RTX 3080 plus Ryzen CPU discovery matches native enumeration.
  Exactly seven `ccl_*` functions are exported, with no extra symbols.
- Existing nine CTests passed before and after on the preserved qualified
  binaries, with test output redirected to D:. The 158 existing compilation
  commands are identical between API-on and API-off configurations. No native
  rendering/kernel sources or qualified executable hashes changed.
- The full 81/30/65 render matrix was not rerun. Linux/macOS, safe unload,
  rendering through the API and cross-version ABI swaps are not verified here.

Small evidence: [Phase 1 results](../../builds/validation/c-api/phase1.json).
Build, install, logs and command records:
`D:/CyclesDeepScratch/c-api/phase1-20261012`.

### Phase 2: Introspection
- Node types, sockets, socket types, defaults, enum values, flags.
- Acceptance: for every node type, a C dump matches the C++ reflection data
  (generated comparison test).
- Independently derive the expected type inventory from every source
  `NODE_DEFINE`, including an explicit accounting of abstract and generated
  registrations. Compare against the **loaded DLL**, not just a second binary
  linking the same static libraries. If registrations are dropped, whole-archive
  the self-registering libraries and repeat both comparisons.

Phase 2 results (2026-10-12):
- Baseline Phase 1 tests: 4/4 passed. Phase 2 tests: 6/6 passed (including the
  installed standalone smoke). Preserved native regression CTests: 9/9 passed.
- The source-derived inventory caught `buffer_params` and `buffer_pass` missing
  from the DLL. Whole-archive retention of `cycles_scene` and `cycles_session`
  fixes this without modifying registration or renderer code.
- Actual DLL inventory: all 118 `NODE_DEFINE` registrations, 64 generated
  conversion types and two used abstract bases, 184 total. The lazy unused
  `procedural_base` is accounted for separately.
- Full C-DLL/native dump equality: 184 types, 1,277 sockets, 963 default elements
  and 816 enum entries. Added array-conversion, error/bounds, concurrent-query,
  larger-struct and source-parser negative tests. Null native empty strings are
  normalized to non-null empty C strings as required by the public contract.
- Installed DLL inventory/parity/exports pass, as does the unchanged Phase 1 C
  host against API 1.1. Exactly twelve public C functions are exported.
- All 158 existing compilation commands match API-on versus API-off. Native
  sources, Phase 1 DLL and qualified production executable hashes are unchanged.
- Direct runtime dependency list is recorded in `src/capi/README.md`; mixed
  dependency isolation is a Phase 7 decision/test, not a Phase 2 implementation.
- Full 81/30/65 rendering, Linux/macOS builds and dependency coexistence were not
  tested here. No Phase 3 work has started.

Small evidence: [Phase 2 results](../../builds/validation/c-api/phase2.json).
Full dumps, pre-fix inventory, build/install and logs:
`D:/CyclesDeepScratch/c-api/phase2-20261012`.

### Phase 3: Scene, nodes and sockets
- Create, delete and edit scene-level nodes by type name. Singletons. Typed and
  array setters/getters, node references. Precise errors for unknown types or
  sockets and type mismatches.
- Acceptance: unit tests per socket type, including arrays and node references.

### Phase 4: Shader graphs, including OSL
- Generic shader nodes by type name, connections, output node, OSL script
  nodes.
- Acceptance: graphs equivalent to existing XML test scenes.

### Phase 5: Geometry and attributes
- Mesh (triangles, polygons, subdivision), hair/curves, point clouds, volumes
  (VDB file and grid), instancing through objects. Standard and custom
  attributes with all elements.
- Acceptance: geometry tests and render parity (Phase 6).

### Phase 6: Session, rendering and outputs
- Session lifecycle mirroring GafferCycles' usage (start, pause, reset, wait,
  cancel, set_samples), progress, errors, and output callbacks with pass
  pixels.
- **Parity test:** render several existing standalone XML test scenes through
  both paths. A small C/C++ converter builds the same scene through the API.
  Images must be identical (CPU exact; GPU under the repo's existing beauty
  policy).
- Acceptance: parity on surface, transparency, volume and OSL scenes; an
  interactive edit test (pause, edit, reset, resume).

### Phase 7: ABI swap test
- Build the C test host once. Run it unchanged against two different library
  builds (for example, Phase 6 and a build from a different commit, plus a debug
  vs release build) and confirm it works and reports each version.
- Acceptance: no host rebuild needed; `ccl_api_version` and features reflect
  the loaded library.
- Dependency coexistence is also an acceptance gate: the host may already have
  loaded its own OpenImageIO, OSL, OpenVDB, Embree, OCIO and USD versions (as
  Gaffer does). Add a test host that preloads a **different OIIO version** before
  loading `cycles_c`, exercises both interfaces and checks actual loaded-module
  identities. Successful DLL loading alone is not sufficient. Document the
  supported version combinations and reject unsupported combinations clearly.
- Decide with the GafferCycles author between static/hidden dependencies,
  renamed private dependency DLLs (including transitive imports), or a shared
  version-matched VFX Reference Platform dependency set. Phase 2 only documents
  these options; it does not implement dependency isolation.

### Phase 8: Deep extension
- Expose the existing deep output (Z/ZBack/A/id) through
  `ccl_session_set_deep_output`, gated by `ccl_has_feature("deep")`.
- Acceptance: deep EXR written by a host through the API is identical to the
  standalone deep output for the same scene (strict mode byte-identical).

### Phase 9: Gaffer proof of concept (separate repository)
- In a fork of GafferHQ/gaffer, add an alternative GafferCycles backend that
  loads `cycles_c` dynamically and uses only `cycles_c.h`. Start with a subset:
  meshes, basic shaders, lights, camera, final and interactive renders.
- Acceptance: the same Gaffer test scenes render identically through the old
  and new backends; Gaffer runs with two different `cycles_c` builds without a
  rebuild.
- Review with the GafferCycles author before broadening coverage.

### Phase 10: Upstream proposal
- Short design write-up plus a link to the working prototype, for the Cycles
  module and Gaffer developers. Only after the GafferCycles author agrees the
  shape is right.

## 6. Rules for this work

- Wrapper only: no changes to Cycles rendering behaviour or kernels. If an
  internal change is unavoidable (for example a missing accessor), keep it
  minimal and list it in `src/capi/README.md`.
- The public header is the contract. Document every function (purpose,
  ownership, threading, errors) in the header itself.
- Keep the core API independent of deep. Deep is an optional extension, so the
  core can be offered upstream without it.
- Measure and test before and after each phase, stop for review after each
  phase, and don't loosen acceptance criteria to pass. Same discipline as
  `DEEP_OPTIMIZATION_PLAN.md`.
- Large build and test outputs go on `D:`. The repository keeps only code,
  docs and small results.

## 7. Questions for the GafferCycles author

1. Should one Gaffer process be able to load several Cycles versions at once,
   or is one per session enough?
2. Which GafferCycles features are essential for a first proof of concept, and
   which can wait (for example instancing, VDB, cryptomatte, OSL)?
3. Is reflection-based configuration (type and socket names as strings)
   acceptable for performance in interactive editing, or are cached socket
   handles needed (for example `ccl_socket_id ccl_socket_find(type, name)`)?
4. How should OSL shader paths and compiled shader caches be handled when the
   library is swapped?
5. Would a Hydra-based route (hdCycles) be an alternative for any of this, or
   is a direct C API preferred?
6. Preferred naming and prefix (`ccl_`), and any ABI conventions Gaffer already
   uses for other renderers.
7. How should Cycles coexist with Gaffer's already-loaded OIIO/OSL/OpenVDB/
   Embree/OCIO/USD versions? Prefer static and hidden dependencies, renamed private
   DLLs, or version matching to the VFX Reference Platform? Which versions must
   the Phase 7 preload-and-use test support? Version matching constrains which
   renderer builds can be swapped and is not arbitrary-version isolation.
