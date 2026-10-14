# Plan: stable C API and shared-library build for Cycles

Status: Phases 1–4 accepted; Phase 5 passed after authorized wrapper fixes
(2026-10-13). Phases 6–8 passed; Phase 8 strict/numeric opaque/transparent files are byte-identical after matching fixture BVH settings. Phase 9 runtime-policy correction passes 14/14 tests (2026-10-14); SCons installed, Gaffer integration stopped for the native 5.1 versus 5.3 baseline decision. Phase 10 pending. Superseded scratch outputs excluded. Windows ABI swaps verified; other platforms remain unqualified.
Requested by the GafferCycles author.
Branch: `c-api`, created from `deep-output`. All API code lives in `src/capi/`.


## Workspace and timing rules (2026-10-13 update)

- 2026-10-14: routine build tools from PyPI or official project releases may be
  downloaded without further approval, only into isolated environments under
  D:/temp/cycles_c_api. Record name, version, source and hash. Admin/system-wide
  installs, drivers, unofficial sources and non-build-tool downloads still need
  approval. Publication remains prohibited.

- All new temporary artifacts, including builds, renders, logs, caches and process
  TEMP/TMP directories, must live under `D:/temp/cycles_c_api/`. This location is
  disposable: commit long-term code, scripts, documentation and small evidence
  to this repository. Do not make disposable files the sole durable record.
- Leave all existing `D:/CyclesDeepScratch/` contents in place and unchanged;
  do not move or delete them. Existing references remain valid. Future builds
  and test runs must use new paths under the new temporary root, rather than
  writing into previous build directories.
- Another session owns `C:/Users/jun/Documents/ChatGPT/cycles_osl_aov` and
  `D:/temp/cycles_osl_aov`. Do not read, write, clean or otherwise operate in
  those folders. Concurrent builds and renders may share machine resources.
- Timing and benchmark measurements collected while the machine is busy are
  provisional. Repeat any such measurement used for acceptance (including
  forwarding overhead) when the machine is idle, and record the contention
  status and repeat results. Do not stop another session's jobs to obtain idle
  measurements. Record uncertainty if idle status cannot be established.
- These rules do not approve the pending Phase 8 test-matrix correction or
  change any acceptance criterion. The existing stop rules still apply.

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


### Phase 7 preflight results (2026-10-13) — STOPPED for design review

Phase 6 is committed as `4d1a54699`. Phase 7 baseline repeats **13/13 CTests**.
An isolated Windows host preloaded each real OIIO DLL set, verified the loaded
paths/hashes, then attempted to load the accepted Phase 6 library:

| Preloaded OIIO | DLL load | `ccl_init` | Result |
| --- | --- | --- | --- |
| Build-matched **3.1.13.0** | succeeds | SUCCESS; reflection returns 184 types | Initialization smoke passes |
| Gaffer 1.7.2.0's **3.0.6.0** | **WinError 127**, missing procedure | **unreachable** | Required clear init-time rejection cannot run |

Neither host crashed. The DLL directly imports 54 symbols from OpenImageIO and
35 from OpenImageIO_Util absent in that older host set. Import names use
`OpenImageIO::v3_1`, while the preloaded library exports `OpenImageIO_v3_0`.
Windows resolves these imports before executing `ccl_init`; adding checks inside
that function cannot fix this case. This is not a parser or numerical failure.
The probe is not a complete dependency-coexistence qualification: other dependency
checks, renderer use in a matched host, and debug/release ABI swaps remain pending.

**Recommended next design, not implemented:** keep the public `cycles_c.dll`
free of renderer/dependency imports. It performs dependency preflight at init,
then loads a private native backend DLL. The shared, version-matched dependency
choice remains unchanged. This adds a second DLL and C-call forwarding (measure
its socket-setter overhead); platform-specific version inspection still needs
qualification. It does not require native Cycles/kernel changes.

Alternative: require host-side preflight and accept loader-time rejection. That
would change the approved init-time error criterion, so it is not applied.
The user must choose the loader design before continuation under the recorded
stop rule. No runtime guard, split or criterion change has been made. Phase 6
DLL hashes remain unchanged. Phases **8–10 have not started**; no push, fork,
issue, release or other external publication occurred. D: remains above 1.3 TB.

Evidence: [Phase 7 loader preflight](../../builds/validation/c-api/phase7.json).
Reproduction: `src/capi/test/dependency_preload.py`; full imports, exports and
host logs: `D:/CyclesDeepScratch/c-api/phase7-20261013`.


## Phase 7 loader decision approved (2026-10-13)

The public DLL will retain all 95 ccl_* exports and load a private native backend
only after dependency preflight. The private table getter checks API version, size
and signature. Windows checks PE imports against already loaded modules and file
version resources where available, returning CCL_ERROR_DEPENDENCY_MISMATCH.
Measure forwarding against the retained Phase 3 benchmark; preserve all prior
CTests, native tests and Phase 6 render parity. No native/kernel changes.

Linux/macOS will use dlopen(RTLD_LOCAL): inspect ELF DT_NEEDED/dynamic symbol
versions or Mach-O load commands/export tries against loaded images, compare
available library versions, then load the private backend and handshake. These
platform implementations remain pending their own builds; RTLD_LOCAL alone does
not isolate dependencies or promise compatibility.


## Phase 7 results (2026-10-13) - PASSED

- API 1.6 retains **95 exact ccl_* exports**. The public front imports only
  **KERNEL32.dll and VERSION.dll**. The private backend exports exactly
  `cycles_c_private_get_table`; API version, table size and signature are checked.
  Invalid handshake requests and malformed/truncated PE images are rejected.
- Real matched OIIO 3.1.13 initializes successfully and exposes all **184** source-
  inventoried node types. Preloaded Gaffer OIIO 3.0.6 returns
  **CCL_ERROR_DEPENDENCY_MISMATCH**, names expected/loaded versions and missing
  imports, and never loads the backend. No crash or loader error 127 reaches the
  host. The host's own OIIO call succeeds before and after both cases.
- Release **13/13 CTests**, CPU Debug **13/13**, retained native **9/9**. CPU render
  parity is exact on all nine channels of four fixtures. OptiX passes the unchanged
  calibrated policy for surface/transparency/volume/OSL, five ordinary and four
  seed references each, zero count failures and zero statistical fallback pixels.
  CUDA/OptiX scene and geometry checks and CPU/OptiX OSL checks also pass.
- One unchanged Phase 6 C host runs against old 1.5 Release, split 1.6 Release and
  split 1.6 CPU Debug; no host rebuild. Both new builds pass export/import checks.
- Same unchanged benchmark host, 9 rounds of 1M setters: name **38.908 -> 38.191
  ns**, ID **15.669 -> 16.244 ns**, direct C++ **1.502 -> 1.482 ns** (medians).
  The measured ID increment is **0.575 ns**; the name decrease is measurement
  variation, not a claimed speedup. No native renderer timing claim is made.
- Wrapper correction during qualification: retain the full deduplicated missing-
  symbol message rather than truncating a large transitive import diagnostic.
  No test or acceptance threshold was loosened. All **158** native compile
  commands match API on/off; prior DLLs and production hashes are preserved.
- Windows x64 only. OSL/OCIO resource/version limitations and Linux/macOS design
  are documented above. No native/kernel change or external publication. D:
  remains above 1.3 TB free. Large artifacts: phase7-20261013 and
  phase7-debug-20261013 beneath D:/CyclesDeepScratch/c-api.
- Evidence: [Phase 7](../../builds/validation/c-api/phase7.json).


## Phase 8 draft results (2026-10-13) - STOPPED, NOT ACCEPTED

The optional deep callback extension is drafted as API 1.7 (three additional
ccl_* functions; the previous 95 remain). Core builds retain unsupported stubs;
deep-enabled builds deliver reconstructed visibility samples and ID manifests.
No native Cycles or kernel changes were made.

CPU Release deep-on and core-only builds completed in separate D: directories.
The initial deep-on suite passed **21/22** tests in 17.31 seconds. The new deep
test passed its opaque, no-ID callback case, then aborted on its invalid
combination of strict error=0 and IDs. Native Cycles explicitly rejects that
combination: IDs require numeric error tolerance (src/session/deep.cpp).
This is a test-configuration mistake, not evidence of deep render parity failure.

**Stop for approval:** correct the test matrix to strict byte comparison without
IDs, and a separate numeric-error IDs case, still byte-identical to native
standalone output at the same settings. No native restriction, comparator,
reference or threshold has been changed after the failure. Standalone EXR byte
comparison is pending; transparent cases were not reached. Phase 8 is not passed.

Wrapper correction in the draft preserves the outer callback re-entry guard
across nested logging/callback scopes. The corresponding contract assertion and
final deep test-source edits need rebuilding and retesting: a read-only Ninja
dry run reports those two test executables pending. The initial 21/22 result is
not qualification of those final test-source edits. The core-only build completed
but its tests were not run after the stop.

Test integration initially reused native app translation units as separate
sources, colliding with the source-keyed compile-command proof. Including those
existing helpers inside the API test translation unit fixed the test integration;
the unchanged proof then passed (173 native units deep-on; 158 core-only).
No acceptance criterion was relaxed.

Phases 9 and 10 have not started. Nothing was published. No build/test process
remains running; D: free space was 1,390,720,962,560 bytes at the stop.
Large artifacts: D:/CyclesDeepScratch/c-api/phase8-20261013 and
D:/CyclesDeepScratch/c-api/phase8-core-20261013.
Evidence: [Phase 8 stopped result](../../builds/validation/c-api/phase8.json).


## Phase 8 continuation approved (2026-10-13)

Phase 7 is accepted. Test strict mode without IDs and numeric error 1e-3 with
IDs, requiring byte identity against standalone at identical settings in both
opaque and transparent cases. Rebuild final test sources in a fresh disposable
D:/temp/cycles_c_api directory. Continue Phases 8-10 locally.

A configuration contradicting the documented native support matrix may now be
corrected without stopping if criteria remain at least as strict and the
correction is recorded. Real failures, native/internal changes, uncovered design
trade-offs and external publication retain their stop rules. Phase 9 local Gaffer
clone/build belongs under D:/temp/cycles_c_api; Phase 10 is a local document only.


## Phase 8 continuation incident (2026-10-13) - STOPPED

The strict/no-ID and numeric/ID test configuration was corrected as approved.
A copied temporary build driver failed to replace its forward-slash old-root
literal because the replacement key used Windows backslashes. It read the old
configure command and ran configure/build/install in
`D:/CyclesDeepScratch/c-api/phase8-20261013`, violating the explicit preservation
instruction. The old toolchain also retained its old TEMP/TMP path.

Known overwritten state includes CMake/generated build state, logs and audit
reports, API build/test outputs, and installed `cycles_c.dll`,
`cycles_c_backend_private.dll` and `lib/cycles_c.lib`. Previous bytes are not
claimed preserved. No rollback was attempted. The faulty copied driver in the
new temporary directory is disabled. No tests or renders were run in this
continuation, and no native sources or the other session's folders were edited.

Phase 8 remains unqualified; Phases 9-10 have not started. Await user direction
on the preservation incident before continuing. Future drivers must validate
their actual root, configure build/install paths and toolchain TEMP/TMP against
D:/temp/cycles_c_api before any command, instead of relying on text replacement.
Evidence: [path incident](../../builds/validation/c-api/phase8-path-incident.json).


## Fresh Phase 8 qualification approved (2026-10-13)

`D:/CyclesDeepScratch/c-api/phase8-20261013`: **overwritten by an incorrect
script root on 2026-10-13; contents superseded, not evidence**. Leave it as-is.
Phase 8 qualification relies only on the fresh build under D:/temp/cycles_c_api.
Every future build/test/install driver must resolve and guard build, output,
install, TEMP/TMP and CMAKE_INSTALL_PREFIX paths before executing. Only committed
files may use repository destinations; all temporary output stays in the C API
temporary root. The guard self-test must reject CyclesDeepScratch destinations.


## Phase 8 fresh results (2026-10-13) - FAILED BYTE PARITY; STOPPED

Qualification uses only `D:/temp/cycles_c_api/phase8-core-qualified-20261013`
and `phase8-deep-qualified-20261013`. Core **13/13** and deep **22/22** CTests
pass, including the nine native tests and all opaque/transparent callback cases
(strict without IDs; numeric 1e-3 with IDs). Final callback guard assertions and
exception/error cases were rebuilt and passed. Both builds expose 98 ccl_*
functions, one private backend getter, and only KERNEL32/VERSION front imports.
Native compile commands match API off/on: core 158, deep 173. No native changes.

The first standalone comparison, strict opaque without IDs, **fails byte
identity**: native 10,951 bytes versus API 12,504 bytes. Hashes and command are
retained in the small Phase 8 report and fresh comparison directory. The cause
is not established; no equivalence is inferred from callback tests. Remaining
standalone comparisons were not run after failure. No comparator, threshold or
reference was changed. Phases 9-10 have not started; nothing was published.

The new qualification drivers resolve/guard output, build/install and TEMP/TMP
paths, inspect generated install destinations and verify the actual manifest.
The self-test rejects old scratch, sibling temp, traversal and an old install
prefix. Toolchains and source fixtures remain read-only inputs outside the temp
root. Only committed reports/docs/code may use repository output paths.

Harness-only corrections: convert Path command arguments to strings before JSON
logging (serialization self-check passes); provide the installed DLL directory
in subprocess PATH, then rerun the unchanged tests after initial dependency-load
failures. Timing/benchmark durations are provisional and were not used to accept
a performance claim. No build, test or render remains running for this work.
Evidence: [fresh Phase 8 results](../../builds/validation/c-api/phase8.json).

## Phase 8 final results (2026-10-13) — PASSED

The unchanged `tools/compare_deep_identity.py` confirmed that the original
strict opaque pair had identical headers (no differing attributes, including
run-specific attributes) and ZIPS compression with 20 scanline chunks, but a
real payload mismatch: 2,751 versus 3,001 deep samples; A differed at 890/940
pixels and Z/ZBack at 304/940. IDs were absent by design in this strict case.
Full per-pixel counts/values and chunk layouts are preserved in
`builds/validation/c-api/phase8-opaque-before.json`.

Cause: the native XML reader forces **static BVH**, while the API test fixture
used the default **dynamic BVH**. Camera, integrator, sampling lookup tables and
vertices matched; the native static transform flag differed. Setting the test's
existing `bvh_static=1` option aligns scene settings. This is a harness setup
correction, with no wrapper, native renderer, comparator or criterion change.
Temporary diagnostic instrumentation was removed.

All four host-written EXRs are now **literally byte-identical** to standalone:
opaque/transparent strict without IDs and opaque/transparent numeric 1e-3 with
IDs. The strict opaque pair has 2,751 samples on both sides; all 940 pixels,
headers and compressed chunks match. A direct native Session comparison is now
also asserted inside the deep callback test to retain this regression coverage.

Final deep build: **22/22 CTests**, including nine native tests; core remains
**13/13**. Both expose 98 ccl_* functions, a single private table getter, and
OS-only front imports. Native compile-command proofs remain unchanged. Fresh
builds only qualify; the overwritten scratch directory remains superseded.
No performance acceptance relies on these concurrent-machine test timings.
Small before/after reports and `builds/validation/c-api/phase8.json` retain the
results; large outputs stay under the guarded temporary root. Proceed to Phase 9.
## Phase 9 preflight (2026-10-13) — STOPPED FOR BUILD-SCOPE DECISION

Local read-only upstream checkouts were created under D:/temp/cycles_c_api:
Gaffer `afb285e7f5b7245944f3111321068bcee0ae3888` (the plan's inventory
revision), dependencies `dc6671a600c0764f65f7e7dfb2c69019be1bf511`, and
Cortex `a82231ae5d8dafd41882a1f203accfe10f888a10`. No changes were published.

The requested source-built Windows dependency stack is not provided by the
checked-in upstream recipes: Python uses `./configure`/`make` without a Windows
override; the dependencies Windows CI only builds GafferResources and does not
publish. Gaffer consumes a prebuilt Cortex 10.7.3.1 platform25 archive, while
Cortex's Windows workflow consumes prebuilt dependencies 11.0.0a8. These source
and workflow findings, versions and commands are recorded in
`builds/validation/c-api/phase9-preflight.json`.

Decision required: authorize the additional Windows dependency-build port, or
allow the official prebuilt dependency bundle while compiling Cortex/Gaffer and
matching cycles_c variants locally. Recommend the latter for the proof of
concept, preserving the approved shared/version-matched contract. The current
Cycles API's qualified OIIO 3.1.13 / OSL 1.15.3 stack differs from upstream
Gaffer dependency recipes (OIIO 3.0.6.1 / OSL 1.14.8.0); no unchecked DLL mixing
or loader-check bypass is permitted. Any native compatibility change encountered
when rebuilding against the host stack still triggers the existing stop rule.

No Gaffer backend or build has started; Phase 9 parity and library-swap criteria
are pending. Phase 10 remains pending because this is a stop condition. D: has
1.389 TB free. No build/render/upload job remains running for this work. The
existing scratch tree and the other session's worktree/temp directories were
not modified. Phase 8 is passed independently of this Phase 9 build decision.
## Phase 9 dependency route approved (2026-10-13)

Phase 8 accepted by the user. The API README now notes native static/dynamic BVH
rounding differences at near-coincident surfaces and requires matching parity settings.

Use GafferHQ's official prebuilt Windows dependencies, then compile Gaffer and
cycles_c front/backend locally against the same OIIO, OSL, OpenVDB, OCIO, Embree,
USD and TBB versions. Validate the Phase 7 loader check inside Gaffer, old/new
backend scene parity and two-library swaps. All outputs stay guarded under
D:/temp/cycles_c_api. Nothing is pushed/published; Phase 10 remains a local proposal.

Download approval is separately required BEFORE acquiring the archive:
- Gaffer checkout afb285e7f5b7245944f3111321068bcee0ae3888 identifies as 1.8.0.0
  and selects Cortex 10.7.3.1. That exact Cortex tag's Windows workflow selects
  `gafferDependencies-11.0.0a8-windows-platform25.zip`.
- Official URL: https://github.com/GafferHQ/dependencies/releases/download/11.0.0a8/gafferDependencies-11.0.0a8-windows-platform25.zip
- GitHub release API reports 415,817,133 bytes (415.82 MB / 396.55 MiB).
- Published SHA-256: `9dc1c6eea1eb6ffd762dbe73af0913e824193dde61ca51433d1559cba74b5712`.
- Verify this digest after download, before extraction/use. Metadata only was
  queried; no archive downloaded. Waiting for the user's explicit download OK.

## Phase 9 package/build results (2026-10-13) — STOPPED AT RUNTIME POLICY

Downloaded only the explicitly approved GafferHQ archive. Its 415,817,133-byte
size and SHA-256 `9dc1c6eea1eb6ffd762dbe73af0913e824193dde61ca51433d1559cba74b5712`
matched before extraction. This is a **pre-release dependency set matching
Gaffer 1.8 development (main), not a released Gaffer**. Guarded archive/extraction
and all build/install/test/TEMP outputs are under D:/temp/cycles_c_api.

The front/backend and native standalone compiled and installed using the package
(OIIO 3.0.6.1, OSL 1.14.8, OpenVDB 12.1.1, Embree 4.4.0, TBB 2021.13.0).
USD 0.26.5 is present for the host; this core API build does not link USD.
No native renderer/kernel source changed. Build-only adaptations import the
shipped OIIO libraries without absent utility executables, relocate OCIO metadata,
use packaged OIIO pugixml, enumerate runtime DLLs for the version manifest, and
provide TBB's auto-link library search path. The exact comparator/tests are unchanged.

**7/13 CTests passed; six fail during ccl_init.** The package runtime manifest
records Microsoft runtime 14.40.33816.0, while test hosts have 14.51.36247.0 loaded.
MSVCP140.dll, VCRUNTIME140.dll and VCRUNTIME140_1.dll have no missing required
symbols, but the current exact-version policy rejects them. The render test's
0xc0000409 follows its failed initialization assertion; no render parity was run.
This is not evidence of a rendering failure or a successful integration.

Stop for the uncovered policy decision: retain exact checks for renderer
libraries, but allow MSVC v14 updates under Microsoft's compatibility rules
(runtime at least as new as the build tools, required symbols present), or require
an exact runtime deployment. The former is recommended but **not implemented**.
Microsoft documents the compatibility constraints at
https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170 .
Do not bypass this check or force an older runtime into a newer compiler's build.

Gaffer compilation/alternative backend, in-Gaffer Phase 7 checks, old/new scene
parity and two-library swaps are still pending. SCons is also not installed in
the checked Python environments. Phase 10 remains pending at this stop condition.

## Phase 9 runtime-policy results (2026-10-14)

The user approved a runtime-only exception to exact version matching. The front
records the maximum of the selected toolchain redistributable and dependency
package runtime versions at configure time: **14.44.35211.0**, versus package
14.40.33816.0. Loaded MSVC runtimes must have major 14 and meet that minimum;
every imported symbol is still required. Renderer dependency matching is unchanged.
UCRT remains an OS component checked for imports, not an MSVC-major-14 library.
See the [API runtime policy](../../src/capi/README.md#microsoft-runtime-compatibility-2026-10-14).

After the wrapper correction, the matched build passed **14/14 CTests**, including
CPU native render parity, node inventory/reflection, and the added pure-policy
test. The latter covers newer/equal/older versions, a non-14 major, unavailable
versions, missing symbols despite a newer version, and unchanged renderer checks.
The final sources were rebuilt and retested after adding UCRT coverage. The front
still exports **98 ccl_* functions** and imports only OS libraries; the backend
exports only its private table getter. No native/kernel or acceptance changes.
Timing from the functional benchmark is not used as an acceptance measurement.

Evidence: [runtime qualification](../../builds/validation/c-api/phase9-runtime-policy.json).
This uses the approved **pre-release 11.0.0a8 dependency set for Gaffer 1.8 main**,
not a released Gaffer. Phase 9 is not complete: Gaffer has not been built or
tested with the API. Its Windows workflow installs SCons separately, and no
local SCons command or packaged copy was found. Stop before downloading another
tool under the user's external-action rule. Proposed next action: obtain SCons
4.10.1 from PyPI into the guarded temporary root (this version is also pinned in
Gaffer's macOS workflow), then continue the local build/integration. Nothing
published; Phase 10 local proposal remains pending.
Phase 8 remains accepted. No publication, push or new render occurred; no jobs
remain running for this work. See `builds/validation/c-api/phase9.json` for the
small evidence, exact tests and DLL hashes. Earlier preflight stop is superseded.

## Phase 9 build tools and baseline decision (2026-10-14)

Installed SCons **4.10.1** into `D:/temp/cycles_c_api/build-tools/venv`, not
system Python. The PyPI wheel is 4,136,069 bytes, SHA-256
`bd9d1c52f908d874eba92a8c0c0a8dcf2ed9f3b88ab956d0fce1da479c4e7126`;
the downloaded digest matches PyPI metadata. Version/source/hash are retained in
[build-tool evidence](../../builds/validation/c-api/phase9-build-tools.json).

Copied the approved dependency bundle into a separate guarded Gaffer build root.
It contains Cortex 10.7.0 headers, lacking `IECoreScene/PointInstancer.h` required
by this Gaffer checkout. Selected Gaffer's specified Cortex **10.7.3.1**, revision
`469e5c1` (full revision in evidence), and started its local build against the same
bundle. The initial Gaffer dry-run could not initialize SCons' configure cache;
it was only a preflight, not a compilation or qualification failure.

**Design stop:** the bundle's native Cycles headers identify **5.1.0**, while our
API wraps **5.3.0**. Exact image parity between these would combine native-version
differences with wrapper differences. No image mismatch is claimed; no Gaffer
render has run. Recommend compiling the existing Gaffer C++ backend against the
same 5.3 source/configuration as the API backend, including matching feature
defines and dependencies. That changes the bundled baseline and may require
Gaffer-side adaptation; obtain the user's decision before doing so. Porting the
wrapper to 5.1 or treating cross-version images as the acceptance baseline are
different scopes and have not been selected. No criterion was relaxed.

Cancelled only the verified Cortex build process tree at this stop. Partial
objects remain under the guarded root and are not qualification evidence.
Retested the existing matched API install: **14/14 CTests**, **98 exports**,
private backend getter and OS-only front imports pass. No timing acceptance is
inferred. No Cycles internal changes, publication or changes to old scratch or
the other worktree. Phase 9 integration/parity/swap and Phase 10 remain pending.
[Baseline evidence](../../builds/validation/c-api/phase9-baseline-decision.json).
