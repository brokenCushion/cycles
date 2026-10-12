# Plan: stable C API and shared-library build for Cycles

Status: Phases 1–4 accepted; Phase 5 passed after authorized wrapper fixes
(2026-10-13). Phase 6 passed; Phase 7 is next. Experimental until Phase 7.
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

Phase 3 introduced scene-owned nodes and six borrowed singletons; API 1.3 now
uses Session-owned scenes. The unchanged editing surface includes
all scalar and array socket accessors, and group-update tagging. Every typed
setter/getter has both name and cached-ID forms. Representative signatures:

```c
ccl_status ccl_session_params_default(ccl_session_params *params);
ccl_status ccl_session_create(const ccl_session_params *params, ccl_session **out);
ccl_status ccl_session_scene(ccl_session *session, ccl_scene **out);
ccl_status ccl_session_destroy(ccl_session *session);
ccl_status ccl_scene_singleton(ccl_scene *scene, const char *type, ccl_node **out);
ccl_status ccl_node_create(ccl_scene *scene, const char *type, const char *name, ccl_node **out);
ccl_status ccl_node_delete(ccl_scene *scene, ccl_node *node);
ccl_status ccl_socket_find(const char *node_type, const char *name, ccl_socket_id *out);
ccl_status ccl_node_set_float(ccl_node *node, const char *socket, float value);
ccl_status ccl_node_set_float_id(ccl_node *node, ccl_socket_id socket, float value);
ccl_status ccl_node_get_float_id(ccl_node *node, ccl_socket_id socket, float *out);
ccl_status ccl_node_tag_update(ccl_node *node);
```

Ownership refinement: native Session creation/device selection replaces the
Phase 3 standalone CPU editing scene. There is no scene adoption step. Destruction
and singleton lookup return status. IDs are
stable only for this loaded library and exact node type; they are not serialized
identifiers. Arrays, including node arrays, use one typed/capacity-checked API
instead of separate node-array functions. Fixed-layout math values are plain C;
metadata retains its size-prefix convention. Hosts must skip unknown future
`ccl_socket_type` values. These decisions are documented in the header and are
accepted in Phase 3. The ownership correction below supersedes standalone scene
creation without changing the node/socket functions.

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
Session creation/scene borrowing and shader graph functions below are now
implemented in API 1.3; rendering, output and geometry-attribute functions remain
provisional. The fully documented header is authoritative.

```c
/* Session and scene */
ccl_status  ccl_session_create(const ccl_session_params *params, ccl_session **out);
ccl_status  ccl_session_destroy(ccl_session *s);
ccl_status  ccl_session_scene(ccl_session *s, ccl_scene **out);
ccl_status  ccl_session_start(ccl_session *s);
ccl_status  ccl_session_pause(ccl_session *s, int pause);
ccl_status  ccl_session_reset(ccl_session *s, const ccl_buffer_params *buffer);
ccl_status  ccl_session_wait(ccl_session *s);
ccl_status  ccl_session_cancel(ccl_session *s);
ccl_status  ccl_session_set_samples(ccl_session *s, int samples);
ccl_status  ccl_session_progress(ccl_session *s, ccl_progress *out);
void        ccl_session_set_progress_callback(ccl_session *s, ccl_progress_fn fn, void *user);

/* Geometry attributes (beyond sockets) */
ccl_status  ccl_geometry_add_attribute(ccl_node *geom, const char *name, int standard,
                                       ccl_type t, ccl_attr_element e, const void *data, size_t count);

/* Shader graphs */
ccl_status  ccl_shader_graph_create(ccl_scene *scene, ccl_shader_graph **out);
ccl_status  ccl_shader_graph_add(ccl_shader_graph *g, const char *node_type, const char *name, ccl_node **out);
ccl_status  ccl_shader_graph_connect(ccl_shader_graph *g, ccl_node *from, const char *out_socket,
                                     ccl_node *to, const char *in_socket);
ccl_status  ccl_shader_graph_output(ccl_shader_graph *g, ccl_node **out);
ccl_status  ccl_shader_set_graph(ccl_node *shader, ccl_shader_graph *g);   /* takes ownership */
ccl_status  ccl_shader_graph_add_osl(ccl_shader_graph *g, const char *absolute_osl_or_oso_path, const char *name, ccl_node **out);

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
  tested in Phase 2.

Small evidence: [Phase 2 results](../../builds/validation/c-api/phase2.json).
Full dumps, pre-fix inventory, build/install and logs:
`D:/CyclesDeepScratch/c-api/phase2-20261012`.

### Phase 3: Scene, nodes and sockets
- Create, delete and edit scene-level nodes by type name. Singletons. Typed and
  array setters/getters, node references. Precise errors for unknown types or
  sockets and type mismatches.
- Provide cached socket IDs as well as name-based setters/getters. IDs are stable
  for this library instance; typed `_id` access avoids per-call string lookup.
- Document that hosts skip unknown future socket-type values rather than fail.
- Acceptance: unit tests per socket type, including arrays and node references;
  benchmark name versus ID setters against direct C++ `Node::set()`.
- Stop for review before Phase 4.

Phase 3 results (2026-10-12):
- Baseline Phase 2 tests: 6/6. Final Phase 3 tests: 9/9. Preserved native CTests:
  9/9. Installed plain-C scene host and unchanged Phase 1/2 dynamic hosts pass.
- 13 scene item factories, six singletons, all 25 value socket types and eleven
  array types tested, plus two non-value rejections, ownership/reference errors,
  copied buffers, invalid values, cache growth and concurrent edits.
- 67 C-only exports; source inventory remains complete (184 types), and full
  native/C reflection parity still matches 1,277 sockets, 963 defaults, 816 enums.
- API-on/off native compilation commands remain identical (158 units). All 548
  installed paths stay inside the new D: prefix. Phase 1/2 DLLs and qualified
  production executables retain their hashes; native rendering sources unchanged.
- Setter benchmark, nine rounds of one million edits (median ns/call):
  direct C++ **1.55**, name **32.53**, cached ID **13.93**.
  ID is **2.33x** faster than name lookup in this hot single-thread
  test. API paths include validation/serialization; direct C++ does not. No
  end-to-end Gaffer or general rendering-speed claim follows from these numbers.
- Native shader update's empty-graph prerequisite and unfinished-object deletion
  are handled in wrapper setup, with tests; no internal source patch was needed.
- No render/session work, Linux/macOS validation, sanitizer or dependency
  coexistence test. Full 81/30/65 renders were not repeated. Stop for Phase 3 review.

Small evidence: [Phase 3 results](../../builds/validation/c-api/phase3.json).
Full build/install, benchmark trials and logs:
`D:/CyclesDeepScratch/c-api/phase3-20261012`.

### Required ownership correction before Phase 4 (user-approved)

API **1.3.0** removes standalone scene creation/destruction. A native Session
creates its own device-owned Scene, returned as a borrowed handle. Public settings
cover explicit device selection and SceneParams-equivalent scene options; native
Session construction waits without starting a render. Session destruction
invalidates its scenes/nodes/graphs. No native scene adoption or copying is added.
All Phase 3 node/socket signatures remain unchanged. Start/pause/reset/wait and
outputs remain Phase 6. The ABI is **experimental until Phase 7**.

Baseline and ownership-only CTests both passed **9/9**. All scene factories,
singletons, value/array/reference and concurrency tests passed through CPU and
CUDA Session-owned scenes before graph work; final verification repeats CUDA and
OptiX and checks native scene/device identity and nondefault parameter mapping.

### Phase 4: Shader graphs, including OSL
- Generic shader nodes by type name, connections, output node, OSL script
  nodes.
- Acceptance: graphs equivalent to existing XML test scenes.

Phase 4 results (2026-10-12):
- Final CTests **10/10**; preserved native regression **9/9**. Installed plain-C
  graph/OSL tests on CPU and OptiX pass; Phase 3 scene/editing suite also passes
  through CUDA and OptiX Sessions. No render starts.
- **16 shader graphs from six existing XML examples** match native loader types,
  values and connections on CPU and OptiX, including OSL stripes, volume and
  displacement. Legacy comma-separated color values are converted to equivalent
  spaces in the test DOM because the current native volume parser rejects commas;
  fixture files/values remain unchanged. Unknown legacy attributes follow the
  native XML reader's ignore behavior. This is graph parity, not image parity.
- Graph connections preserve native conversion behavior, with explicit cycle,
  ownership, type, occupied-input and missing-socket errors; disconnection and
  graph replacement are tested. All handles from a graph are consumed on shader
  attachment because native Cycles may remove proxy nodes there.
- OSL takes an absolute `.osl`/`.oso` path (not ambiguous inline text). Native
  compilation/cache behavior is retained. Instance socket reflection/IDs expose
  OSL parameters without changing existing typed setters; OSL IDs die with the
  instance or graph attachment. Unsupported native OSL parameter shapes are not
  expanded. Custom AOVs remain outside this phase.
- Exact **80 `ccl_*` exports**, complete **184** built-in types, full reflection
  parity, **158** identical API-on/off native compile commands, audited **548**
  installed paths. Phase 1/2/3 DLLs and qualified production binaries unchanged.
  No internal renderer, scene, scheduler, shader or kernel source change.
- Setter benchmark rerun (median ns): direct **1.49**, name **31.21**, ID **13.26**;
  microbenchmark only. No render controls, full 81/30/65 rerender, Linux/macOS,
  sanitizers, mixed-dependency test or stable-ABI promise in this phase.

Outline refinements for review: status/out Session creation/scene borrowing;
explicit graph destruction/disconnection; optional copied node names; graph and
node-handle consumption on attachment; explicit OSL file loading plus per-instance
socket count/info/find. This supports replacement networks without dangling native
proxy/OSL pointers. Pause/edit/render lifetime work remains Phase 6.

Evidence: [Phase 4 results](../../builds/validation/c-api/phase4.json).
Full build/install/logs: `D:/CyclesDeepScratch/c-api/phase4-20261012`.
Phase 4 accepted. Phase 5 baseline: retained C API CTests **10/10**, 2026-10-13;
log `D:/CyclesDeepScratch/c-api/phase5-20261013/baseline.log`.


### Phase 5: Geometry and attributes
- Mesh (triangles, polygons, subdivision), hair/curves, point clouds, volumes
  (VDB file and grid), instancing through objects. Standard and custom
  attributes with all elements.
- Acceptance: geometry tests and render parity (Phase 6).

Phase 5 results (2026-10-13) — **FAILED; stopped, not accepted**:
- Baseline **10/10**. Fresh API 1.4 draft builds and installs; after-test **10/11**.
  The new `cycles_c_geometry` test stops at the first center POSITION write:
  `INVALID_ARGUMENT: Invalid motion step/count`.
- Native `Geometry` defaults `motion_steps` to **0** (no motion), but the new
  wrapper rejects values below 1 even for center step 0. This is a wrapper
  validation defect, not a harness defect. No workaround, retry, gate change or
  fix was applied; permission is needed to fix it and resume qualification.
- Draft adds native mesh/subdivision/hair/point allocation, copied attributes and
  VDB loading. Geometry coverage is **not established**: later assertions did not
  execute. Native topology sockets and object references are reused.
- Exact **86** C exports; source inventory **184** types; reflection parity passes.
  **158** native compile commands identical with API on/off; **548** audited
  install paths. Native sources, Phase 1–4 DLLs and qualified production binaries
  unchanged. D: has over 1.3 TB free; no external publication.
- CUDA/OptiX geometry tests, remaining geometry acceptance, post-phase legacy
  tests and Phases 6–10 were not run after the stop. Render parity remains Phase 6.
- Evidence: [Phase 5 stopped results](../../builds/validation/c-api/phase5.json).
  Full build/install/logs: `D:/CyclesDeepScratch/c-api/phase5-20261013`.

Phase 5 resumed results (2026-10-13) — **PASSED**:
- Fixed two wrapper defects with the existing tests/criteria unchanged: native
  motion_steps=0 now permits the single center buffer, and VDB loading registers
  OpenVDB's built-in types before reading files. No native values or renderer code
  changed. The earlier failure above remains part of the record.
- **11/11** CTests and **9/9** preserved native tests pass. Geometry suite passes
  in CPU, CUDA and OptiX sessions: triangle and polygon/subdivision storage,
  curves, points, object instancing, attribute domains/payloads/packing/motion,
  VDB file/grid loading, invalid input and stale-handle checks. No render starts.
- Installed-library inventory/reflection and exact **86** exports pass. Retained
  C hosts, device editing, and OSL graph tests also pass. Phase 1–4 and production
  hashes unchanged. Existing tests are byte-for-byte unchanged from `d78055335`.
- API 1.4 adds six geometry functions. Reflected topology sockets remain the
  native route; hosts supply regular polygon triangulation and subd topology.
  Render parity is explicitly deferred to Phase 6, not claimed here.
- Updated [evidence](../../builds/validation/c-api/phase5.json); all large files
  remain in `D:/CyclesDeepScratch/c-api/phase5-20261013`.

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
- User decision (2026-10-13): use shared, version-matched dependencies from the
  host/VFX Reference Platform stack, not private static copies. At `ccl_init`,
  compare loaded OIIO/OSL/OpenVDB/OCIO/Embree versions with build versions and
  return a clear error on mismatch. Test that a matched host works and a host
  preloading mismatched OIIO gets that error without a crash. Check loader-time
  initialization as well as calls after init; shared dependencies are not
  arbitrary-version isolation.

### Phase 8: Deep extension
- Expose the existing deep output (Z/ZBack/A/id) through
  `ccl_session_set_deep_output`, gated by `ccl_has_feature("deep")`.
- Acceptance: deep EXR written by a host through the API is identical to the
  standalone deep output for the same scene (strict mode byte-identical).

### Phase 9: Gaffer proof of concept (separate repository)
- In a local clone of GafferHQ/gaffer on `D:`, building Gaffer and dependencies
  locally, add an alternative GafferCycles backend that
  loads `cycles_c` dynamically and uses only `cycles_c.h`. Start with a subset:
  meshes, basic shaders, lights, camera, final and interactive renders.
- Acceptance: the same Gaffer test scenes render identically through the old
  and new backends; Gaffer runs with two different `cycles_c` builds without a
  rebuild.
- Review with the GafferCycles author before broadening coverage.

### Phase 10: Upstream proposal
- Short design write-up plus a link to the working prototype, for the Cycles
  module and Gaffer developers. Only after the GafferCycles author agrees the
  shape is right. User authorizes a **local document under docs/plans only**;
  no posting, fork, push, issue, PR or release publication is authorized.

## 6. Rules for this work

- Wrapper only: no changes to Cycles rendering behaviour or kernels. If an
  internal change is unavoidable (for example a missing accessor), keep it
  minimal and list it in `src/capi/README.md`.
- The public header is the contract. Document every function (purpose,
  ownership, threading, errors) in the header itself.
- Keep the core API independent of deep. Deep is an optional extension, so the
  core can be offered upstream without it.
- Measure and test before and after each phase; do not loosen acceptance criteria.
  User decision (2026-10-13) supersedes per-phase stops: after each Phase 5–10,
  commit and append short results here and in `src/capi/README.md`, then continue.
  Stop for an acceptance failure requiring more than a harness fix, an internal
  Cycles change beyond the wrapper, an uncovered design tradeoff, any external
  publishing action, or less than 200 GB free on `D:`. Full-resolution renders
  required by a phase are authorized; harness repairs and required small-fixture
  controls retain their standing authorization. Give one consolidated report at
  completion or a stop condition.
- Large build and test outputs go on `D:`. The repository keeps only code,
  docs and small results.

## 7. Questions for the GafferCycles author

1. Should one Gaffer process be able to load several Cycles versions at once,
   or is one per session enough?
2. Which GafferCycles features are essential for a first proof of concept, and
   which can wait (for example instancing, VDB, cryptomatte, OSL)?
3. Resolved by user for Phase 3: provide both named access and cached socket IDs;
   measure their cost against native C++ access for the GafferCycles author.
4. How should OSL shader paths and compiled shader caches be handled when the
   library is swapped?
5. Would a Hydra-based route (hdCycles) be an alternative for any of this, or
   is a direct C API preferred?
6. Preferred naming and prefix (`ccl_`), and any ABI conventions Gaffer already
   uses for other renderers.
7. Resolved by user (2026-10-13): shared host/VFX Reference Platform dependency
   versions, checked at `ccl_init`; reject OIIO/OSL/OpenVDB/OCIO/Embree mismatches.
   Phase 7 requires matched-host success and mismatched-OIIO clear failure without
   a crash. Which exact VFX Reference Platform stack should be supported remains
   a deployment question; version matching constrains swappable renderer builds.

## Continuation authorization (2026-10-13)

The user approved correcting the Phase 5 motion_steps=0 wrapper defect and
continuing Phases 5–10. The refined stop rule supersedes earlier failure stops:
fix bugs in new wrapper code without stopping when tests/criteria stay unchanged
and no native Cycles/kernel code changes; record each fix in phase results. Stop
if a fix requires a test/criterion change, native/internal change, an uncovered
design tradeoff, an action leaving this machine, or D: free space below 200 GB.
Per-phase result entries and local commits remain required.

### Phase 6 design stop: pause and safe editing (2026-10-13)

Baseline: Phase 5's final **11/11** CTests and **9/9** native tests; no Phase 6
implementation, render, test modification or internal patch has been made.

Source inspection found a contract choice left open by section 4:
- `Session::set_pause()` (`src/session/session.cpp:657`) sets a request and returns;
  it does not join an in-flight batch. `run_wait_for_work()` (line 450) handles
  the request later and ignores pause for background/offline sessions.
- `Session::wait()` (line 716) waits for the render job to leave RENDER, not for
  pause acknowledgment. Waiting on a paused interactive job is not a pause barrier.
- `ready_to_reset()` delegates to display feedback, not renderer idleness
  (`src/integrator/path_trace.cpp:108`); it requires a display driver.
- Native scene-update synchronization uses `Scene::mutex` (session.cpp:325),
  while executing batches uses a separate protected buffer mutex (line 214).
  Merely recording a wrapper pause flag does not establish the plan's strict
  not-rendering condition for edits. This is source analysis, not a measured race.

Options requiring a user choice before implementing the public contract:
1. **Wrapper-only quiescent editing (recommended):** keep ordinary pause/resume
   native and accumulation-preserving. Before the first edit in a paused running
   session, synchronously cancel/wait for that job; require reset before resume.
   The scene/device stay owned by the same Session. This guarantees no rendering
   during edits, but edits can block, cancellation/output behavior must be tested,
   and the interrupted accumulation cannot simply resume. The planned
   pause/edit/reset/resume test remains required, unchanged.
2. **Native-style scene locking:** retain asynchronous pause and synchronize edits
   with the native scene mutex. An old batch may still be completing. This needs
   agreement to refine the strict no-rendering editing promise and verification
   of each edit/lifetime path; it is not asserted safe merely from the mutex name.
3. **True acknowledged pause:** investigate a minimal native pause-acknowledgment
   hook for strict idleness with preserved accumulation. That would need separate
   permission under the wrapper-only rule; no internal change is proposed as done.

Stopped under the user's uncovered-design-tradeoff condition. Phase 5 remains
passed; Phase 6 has no acceptance failure. Phases 7–10 have not started. No action
leaving this machine, renderer modification or new render occurred.

### Phase 6 pause/edit decision approved (2026-10-13)

Ordinary pause/resume preserves samples. The first editing call on a paused
session cancels and waits for the in-flight job; edits require reset before
resuming. Document blocking in the public header. A possible future minor-version
addition is a non-blocking edit path (scene mutex, edit, reset while rendering
continues), following Blender viewport sync; no work on that path now. Continue
Phases 6–10 under the refined stop rules.


### Phase 6 results (2026-10-13) — PASSED

- API **1.5**, **95 exact C exports**. Nine session functions implement reset,
  start/pause/wait/cancel, sample limits, copied progress and borrowed named-pass
  output. The approved edit barrier blocks in native cancel/wait, requires reset,
  and leaves ordinary pause/resume accumulation intact. A concurrent wait pins
  session lifetime while allowing another thread to query, resume or cancel.
- Baseline **11/11**, final **13/13 CTests**, retained native tests **9/9**.
  Installed DLL source inventory **184 types**, reflection, old C hosts, CPU/CUDA/
  OptiX scene/geometry tests and CPU/OptiX OSL graph tests pass. Callback reentry,
  host output error/exception containment and wait/cancel/destruction checks pass.
- Four existing XML fixtures (surface, transparency, volume, OSL) are loaded by
  native Cycles and independently cloned via the DLL's public header. At 47x20,
  fixed 16 samples, CPU is exact on all nine saved channels. OptiX passes the
  unchanged beauty policy with **five native controls and four seed references
  per fixture**, zero count failures, zero statistical fallback pixels and passing
  bias/envelope checks. Denoising is disabled; normal/depth differences are also
  retained as measurements, not mislabeled as denoising feature passes.
- The policy adapter preserves every FLOAT and explicitly labels its JSON as an
  adapter: the old validator's `deep` subject flag means API-versus-native here;
  **actual_deep=false throughout**. No renderer setting or policy was changed.
- Harness corrections: preserve native relative XML includes in the temporary
  fixture tree, bind the XML loader's implicit world shader explicitly through
  the public background socket, and copy OpenEXR's mutable channel dictionary
  before writing. Acceptance criteria stayed unchanged. No wrapper test failure
  required a renderer/internal change.
- **158** native compile commands match API-on/off; **558** install paths audited.
  Native sources, Phase 1–5 DLLs and qualified production hashes unchanged. All
  large artifacts are under `D:/CyclesDeepScratch/c-api/phase6-20261013`; D: remains
  above 1.3 TB free. Windows only; no sanitizer/cross-platform qualification or
  full production regression is claimed. ABI remains experimental until Phase 7.
- Evidence: [Phase 6](../../builds/validation/c-api/phase6.json).
