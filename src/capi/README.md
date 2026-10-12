# Cycles C API — Phases 1–7; Phase 8 draft stopped

Experimental shared-library boundary for host applications such as Gaffer.
**Experimental until Phase 7**; the stable ABI promise has not begun.
The contract is [cycles_c.h](cycles_c.h): it contains no Cycles C++ headers or
native deep types. Optional C deep callback types are drafted but not yet qualified. It initializes resource paths, enumerates native devices, reports
compiled capabilities, routes logs and introspects built-in node types, sockets,
defaults, flags and enums. API 1.6 creates device-selected native Sessions and
borrows their scenes, edits sockets by name or cached ID, and builds shader graphs
including OSL scripts. It renders through native Sessions, with progress and named
float-pass callbacks, and supports the documented blocking pause/edit/reset flow.


## Workspace and timing rules (2026-10-13 update)

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

## Build and test

Use the repository's [build prerequisites](../../BUILDING.md), including its
supported compiler and dependency bundle. Configure a **fresh build directory**
and set the install prefix at configure time:

```text
cmake -S . -B <build> -DWITH_CYCLES_C_API=ON -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=<new-install>
cmake --build <build>
ctest --test-dir <build> -R "^cycles_c_" --output-on-failure
```

On Windows, run these in the configured clang-cl/Visual Studio environment.
Runtime dependencies must be discoverable by the OS loader (the dependency
bundle's DLL directories on PATH for build-tree tests). The existing dependency
installation helper also supplies them for installed builds. Inspect generated
install destinations before installing; never reuse a qualified install prefix.
Keep build directories and test output outside the repository, on D: locally.
Before a full `cmake --install <build>`, build the default target too; the native
install rules also need resources such as compiled OSL shaders, not just the DLL.
For a build without the unrelated USD/Hydra integration, set both
`WITH_CYCLES_USD=OFF` and `WITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF`.

`cycles_c_host <absolute-library-path> <absolute-resource-path> <absolute-user-path>`
is compiled as C11, uses LoadLibrary/GetProcAddress or dlopen/dlsym, and has no
Cycles import-library dependency. `cycles_c_contract` additionally checks native
enumeration, exception containment, struct sizes, thread-local errors and logging.
`cycles_c_exports` requires the exact 86 public functions, rejecting extra
symbols even if they start with `ccl_`. No tests depend on deep being enabled.

## Reflection and registration completeness

API 1.1 adds five queries: node count, node description, socket description,
one default element, and one enum entry. Inputs and outputs are separate;
inherited sockets are included. Type and enum lists are sorted by name, while
sockets retain declaration order. Defaults use C scalars, UTF-8 strings and
explicit float components, never a native C++ memory layout. Empty array defaults
are distinct from missing defaults; closures/outputs have no default. Native
empty strings are represented by non-null empty C strings. Instance-specific
OSL sockets are queried through `ccl_node_socket_count/info/find` in API 1.3.

Two independent checks protect this boundary:

- `cycles_c_source_inventory` scans source `NODE_DEFINE` declarations and the
  conversion-node generator, then queries the **actual DLL**. It caught missing
  `buffer_params` and `buffer_pass` before the link fix. The wrapper now retains
  `cycles_scene` and `cycles_session` with `/WHOLEARCHIVE`, `--whole-archive` or
  `-force_load`; no native registration code changed. The source scanner fails
  on unrecognized registration shapes rather than silently skipping them.
- `cycles_c_reflection` compares the full plain-C dynamic-host dump with a
  separate whole-archive native C++ dump: node/base/kind, socket names/UI labels,
  directions/types/flags/constraints, default values and enum names/numbers.
  Exact float hex output preserves the native values. The native dump does not
  call the wrapper. The contract test also exercises nonempty defaults of all
  eleven array types, including types whose built-in defaults are empty.

Measured inventory: 118 `NODE_DEFINE` types + 64 generated conversion types +
2 registered abstract bases = **184 types**; **1,277 sockets, 963 default elements,
816 enum entries** match. The unused `procedural_base` is explicitly accounted
for as a lazy abstract definition, not a missing `NODE_DEFINE` registration.
The original pre-fix inventory is retained in the D: validation directory.

## Session-owned scenes and cached sockets (API 1.3)

**Breaking experimental correction:** standalone `ccl_scene_create/destroy` are
removed. Native Cycles constructs its Scene inside Session with that Session's
device; an independent CPU scene cannot be adopted by a GPU session. The wrapper
now follows this ownership directly, without native changes or copying scenes.

Call `ccl_session_params_default`, then choose a device index from enumeration
(`UINT32_MAX` selects CPU). Parameters expose native scene options: SVM/OSL, BVH,
curves, texture resolution/cache, and background mode, plus thread/headless setup.
Only single-device selection is exposed; internal multi-device BVH combinations
are not a public option. Unsupported OSL/device combinations fail without fallback.
Creation starts the native waiting thread, **not a render**. Start/pause/reset/wait,
render output and progress remain Phase 6.

```c
ccl_session_params params = {0};
params.size = sizeof(params);
ccl_session *session = NULL;
ccl_scene *scene = NULL;
ccl_node *camera = NULL;
ccl_socket_id fov = 0;
/* After ccl_init; check every status in real host code. */
ccl_session_params_default(&params);
ccl_session_create(&params, &session);
ccl_session_scene(session, &scene);
ccl_scene_singleton(scene, "camera", &camera);
ccl_socket_find("camera", "fov", &fov);
ccl_node_set_float_id(camera, fov, 0.8f);
ccl_node_tag_update(camera);
ccl_session_destroy(session); /* Also invalidates scene, nodes and graphs. */
```

All Phase 3 node/socket signatures remain unchanged. Scene items use reflected
names; camera, dicing camera, film, integrator, background and scene attributes
are borrowed singletons. Destroy every session before finishing with the API.

Each typed accessor has name and `_id` variants. IDs are nonzero `uint64_t`
values scoped to the **loaded library and exact node type**, reusable across
scenes and nodes of that type. They do not encode native addresses, and their
validity is unaffected by growing the lookup cache. Do not save IDs in scene
files or reuse them with another DLL/build. The ID path performs no string lookup.
Hosts must **skip unknown socket-type values** from a newer library; they are
not a reason to reject the library or the entire node.

Scalar types use fixed-width C values, explicit float2/float3 and row-major 3x4
transforms. Enums use the numeric values reported by reflection. Array calls use
an explicit array type, copied host data, element counts and output capacities;
a null output with zero capacity queries the required count. Node arrays use
`ccl_node*` elements through the same array API. This replaces separate provisional
node-array functions. Strings and node handles returned by getters are borrowed;
the header specifies their lifetime. Math value structs have fixed layouts;
size-prefixed extensible metadata structs from earlier phases remain unchanged.

Calls are serialized, including concurrent edits. Setters mark native socket
changes; call `ccl_node_tag_update` after a group of edits to notify the native
managers. No concurrent rendering exists yet; pause/reset/edit synchronization
must be enforced when render controls arrive in Phase 6. The benchmark measures setters
alone, without this once-per-edit-group update notification.

References must belong to the same scene and satisfy native socket constraints.
Scalar node references can be null; native node arrays require non-null entries.
Deleting a referenced node returns `IN_USE`; borrowed singleton/default nodes
cannot be deleted. Native shaders are retained until scene destruction, but their
deleted API handles are invalidated immediately. Small retired handle records
remain until scene destruction to avoid address reuse within an editing scene.
Never use a handle after node deletion or scene destruction.

`cycles_c_scene_host` is a plain-C client of the actual DLL: it covers all 13
creatable scene item types, six singletons, references, deletion and cached IDs,
including reuse after resolving every built-in input socket. `cycles_c_editing`
checks all 25 value-bearing socket types against native storage, all eleven array
types, invalid enum/bool/reference values, output capacity, copied storage and
concurrent edits. Two non-value types are explicitly rejected. Synthetic private
nodes cover rare socket types without adding fake types to the exported registry.

`cycles_c_benchmark` loads the actual DLL and compares name and ID setters with
an independently linked direct `Node::set()` on a native camera. All three alternate
values on the cached `fov` socket. It warms up once, rotates execution order over
nine one-million-edit rounds and reports median/min/max nanoseconds per call.
The direct baseline has neither ABI validation nor locking; API measurements
include both plus the dynamic call. This is a hot, single-thread microbenchmark,
not a Gaffer workload or an end-to-end performance promise. Results and limitations
are recorded in [Phase 3 evidence](../../builds/validation/c-api/phase3.json).

## Shader graphs and OSL (Phase 4)

Create a scene-owned graph, add built-in nodes by reflected type name, set values
with the existing node setters, then connect canonical reflected socket names.
Names are case-sensitive (`BSDF`, for example); UI labels are not socket keys.
The graph supplies its single output node. Native automatic conversions are kept;
cycles, cross-graph endpoints, unsupported conversions and occupied inputs return
errors. Disconnect an occupied input explicitly before reconnecting it.

`ccl_shader_set_graph` transfers ownership to a scene shader, replaces its previous
graph and tags the native shader update. **It consumes the graph handle and every
node handle from that graph.** Native attachment may remove proxy nodes, so their
addresses cannot safely stay exposed. To edit an attached network, construct and
attach a replacement. Unattached graphs can be discarded explicitly or are cleaned
up with the Session. Scene shader handles remain valid after attachment.

OSL uses an OSL-enabled Session and an absolute `.osl` or `.oso` file. This refines
the outline's ambiguous `path_or_source`: inline source is not implemented. Native
source compilation writes/reuses a sibling `.oso` according to modification time;
put source in a host-owned writable cache. API loading does not execute the shader
or promise custom OSL AOV support. Native unsupported parameter shapes are omitted,
just as in the XML loader. Query actual instance sockets rather than the built-in
type registry. OSL socket IDs work with all existing typed `_id` calls but are
instance-specific and invalidated by graph attachment/destruction. Built-in IDs
remain valid for the loaded library's lifetime. Unknown future socket types must
be skipped.

Phase 4 verification:

- Phase 3 baseline **9/9**, session-ownership correction **9/9**, final **10/10**
  CTests. All Phase 3 scene/editing cases rerun on **CUDA and OptiX** Sessions;
  white-box checks verify native Scene/device identity and parameter mapping.
- **16 graphs from six existing XML examples** match native loading, including
  surface, volume, displacement, automatic conversion and OSL stripes. Compared
  node/socket types, stored input values and every input connection on CPU and
  OptiX. Legacy comma-separated color values are normalized to equivalent spaces
  in the test DOM for the current native volume parser; files remain unchanged.
  Legacy unknown XML attributes are ignored by both paths, as the native loader
  does. This tests graph construction, **not rendered-image parity** (Phase 6).
- An external plain-C client exercises the installed DLL's graph operations and
  OSL load/reflection/connection/attachment on CPU and OptiX. Negative checks cover
  malformed/missing OSL, cycles, cross-owner links, conversions, occupied inputs,
  invalid handles, and OSL ID invalidation. Native expected compiler errors are
  part of the invalid-source test.
- **80 C-only exports**, complete 184-type source inventory and unchanged full
  reflection parity; **158** native compile commands identical API on/off.
  **548** install paths audited inside the fresh D: prefix. Native regression
  **9/9** passes; earlier Phase 1/2/3 DLLs and production executables unchanged.
- Socket benchmark rerun: direct **1.49 ns**, name **31.21 ns**, ID **13.26 ns**
  medians; the same Phase 3 microbenchmark caveats apply.

Small evidence: [Phase 4 results](../../builds/validation/c-api/phase4.json).
Full logs/build/install: `D:/CyclesDeepScratch/c-api/phase4-20261012`.
GPU checks are explicit (fail if the requested device is absent), for example:

```text
cycles_c_scene_host <install> CUDA
cycles_c_editing <install> OPTIX
cycles_c_scene_host <install> OPTIX <absolute-cached-stripes.osl>
cycles_c_shaders <install> <repo>/examples <writable-test-directory> OPTIX
```

## Phase 1 outline decisions

- Version, feature and device-count queries return `ccl_status` and write into
  host-owned outputs. An error cannot masquerade as zero devices or an unsupported
  feature. Version output also has a size field instead of returning an extensible
  struct by value. All public integers have explicit widths.
- Init exposes the **two existing native resource roots**, not independent kernel,
  OSL and temporary paths that native `path_init` does not provide. Repeated init
  with identical strings is allowed; changing roots is rejected.
- Device metadata is an immutable discovery snapshot. Its strings live for the
  library's process lifetime. Compiled backend support is distinct from available
  hardware. OSL denotes compiled support; individual devices report their support.
- The deep capability stays false until the C API extension is implemented, even
  if the underlying Cycles build includes deep code.
- Log callbacks are serialized, may run on worker threads, and cannot reenter the
  API. Removing/replacing a callback waits for the old call to finish. Callback
  strings are borrowed only during that call. Native severity filtering is kept.
- Phase 1 supports one loaded Cycles library retained until process exit. Safe
  unload, multiple simultaneous versions, resource relocation and backend runtime
  compatibility need explicit lifecycle design before the Phase 7 swap promise.

Phase 3 extended status/out returns to node creation/deletion; Phase 4 defines
Session/graph ownership. Render synchronization, callback lifetime, cancellation
and tile buffer stride rules still need their Phase 6 contracts.

## Runtime dependencies and host coexistence

`dumpbin /dependents` on the Windows Phase 1–4 DLLs reports the same
direct runtime imports (2026-10-12):

- `OpenImageIO.dll`, `OpenImageIO_Util.dll`
- `oslexec.dll`, `oslcomp.dll`, `oslquery.dll`
- `openvdb.dll`, `embree4.dll`, `OpenColorIO_2_5.dll`
- `OpenImageDenoise.dll`, `tbb12.dll`, `sycl8.dll`
- `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`
- `KERNEL32.dll`, `ADVAPI32.dll`, `CFGMGR32.dll`, `VERSION.dll`
- Windows UCRT API sets `api-ms-win-crt-{stdio,heap,runtime,filesystem,time,math,
  convert,environment,utility,string,locale}-l1-1-0.dll`.

This is the direct import table, not a complete transitive or driver-load list.
CUDA/OptiX/HIP drivers can be loaded dynamically. This build disables USD/Hydra,
so it has no direct USD import. Dependencies may load further DLLs themselves.
The C ABI **does not yet isolate these dependencies** from a host's loaded DLLs.

For the GafferCycles author to decide before Phase 7:

| Option | Tradeoff |
| --- | --- |
| Static dependencies with hidden symbols | Stronger isolation where supported; larger binaries, additional build/licensing work, and transitive global-state/symbol interposition checks. |
| Renamed private DLLs | Keeps dynamic dependencies; requires rebuilding/renaming all affected transitive imports and checking plugins and driver interactions. |
| Version-matched VFX Reference Platform stack | Shares a coordinated dependency set; simpler integration but restricts compatible renderer swaps and does not support arbitrary mixed versions. |

Phase 7 must preload a different OIIO version before `cycles_c`, exercise both,
and verify loaded-module identities. It must also establish the supported policy
for OSL, OpenVDB, Embree, OCIO and USD coexistence. None of these isolation options
is implemented in Phase 2.

## Internal changes

No Cycles rendering, device, scene, scheduling or kernel source was changed.
Build integration adds an opt-in `WITH_CYCLES_C_API` and shared wrapper target,
and enables position-independent static objects when that option is on. Phase 2
adds whole-archive retention for `cycles_scene` and `cycles_session` in the wrapper
and its independent reference test only. The
wrapper calls existing `path_init`, `log_init` and `Device::available_devices`.
Phase 3 uses native Scene factories, Node setters/getters and manager update calls.
Phase 4 adds native Session ownership, ShaderGraph editing and OSL loading; no
scene-adoption hook, deep dependency or native shader/compiler patch was needed.
No unavoidable internal source changes were needed. Two existing wrapper setup details:
new shaders receive an empty native output graph (native updates require it), and
unfinished objects with null geometry are erased from the native collection and
tagged directly because native single-object deletion dereferences geometry.
Tests cover both; rendering and kernel behavior are unchanged.
Static implementation symbols are private to the shared library: explicit
exports on Windows, linker export lists on ELF/Mach-O. Dependencies may still
be separate runtime libraries; a C ABI does not make them self-contained or
guarantee compatibility between arbitrary dependency versions.

Verified coverage includes Phases 1–5; render parity is still pending. ABI evolution and render
parity remain acceptance gates in the [plan](../../docs/plans/CYCLES_C_API.md).

## Continuing Phases 5–10 (2026-10-13)

Phase 4 is accepted. Phase 5 baseline: **10/10 C API CTests passed**. Subsequent
phases run locally in order, with a results entry and commit after each phase.
Stop conditions and acceptance criteria are in the plan. Large outputs remain on
D:, with a 200 GB free-space floor; no external publication is authorized.

The dependency choice above is now resolved: shared host/VFX Reference Platform
versions. Phase 7 must check loaded OIIO/OSL/OpenVDB/OCIO/Embree versions at init,
reject mismatches clearly, and test matched and preloaded mismatched-OIIO hosts.
Phase 9 uses a local Gaffer clone and local dependency builds on D:. Phase 10
produces a local upstream proposal only.

## Phase 5 results — stopped (2026-10-13)

Baseline **10/10**; fresh build/install succeeds; after-test **10/11**. The new
geometry test fails on its first center POSITION write: native `motion_steps=0`
is valid for non-motion geometry, but the draft attribute wrapper rejects it.
This requires a wrapper fix, so the user's stop rule applies. No fix, retry or
workaround was applied. Phases 6–10 have not started.

API 1.4 is an **unqualified draft**, adding geometry allocation, attributes and
VDB file/grid loading. Existing topology socket setters and object references are
reused. Later geometry assertions, device geometry tests and render parity have
not been established. This build must not be treated as an accepted replacement
for the preserved Phase 4 install.

Exact 86 C exports, 184 source-inventoried node types and reflection parity pass.
158 native compile commands match API-off; 548 installed paths are inside the new
prefix. Native sources and all checked Phase 1–4/production binaries are unchanged.
No internal Cycles change or external publishing action occurred. See
[stopped results](../../builds/validation/c-api/phase5.json) and the
[plan](../../docs/plans/CYCLES_C_API.md) for the failure and unrun checks.

## Phase 5 resumed results (2026-10-13)

**Passed: 11/11 CTests, 9/9 preserved native tests; CPU/CUDA/OptiX geometry suites.**
The two wrapper fixes are center-only handling for native motion_steps=0 and
OpenVDB type registration before file loading. Existing tests and criteria are
unchanged from the stopped commit. No Cycles internals or kernels changed.

The earlier stopped result is superseded by this entry. API 1.4 adds geometry
allocation and copied standard/custom attributes (including packed normals,
byte colors, subdivision and motion) plus VDB file/grid loading. Use existing
node sockets for topology/shaders and object references for instancing. Ordinary
polygon triangulation is supplied by the host, matching native Mesh; subdivision
polygons use the native subd sockets and attribute set. Tags retain native update
behavior. All six new functions have ownership/thread/error contracts in the header.

Installed exports (86), registration (184 types), reflection, retained C hosts and
shader tests pass; qualified previous binaries remain unchanged. Render parity,
Linux/macOS and sanitizers are not covered by Phase 5. The refined user rule allows
wrapper bug fixes with unchanged tests/criteria; results above record both fixes.

## Phase 6 design review required (2026-10-13)

Phase 5 passed and was committed as `04eb35d65`. Phase 6 baseline is the final
11/11 CTests plus 9/9 native tests. No Phase 6 render-control implementation or
acceptance test change has been made.

Native pause is asynchronous; native wait waits for job completion, not paused
idleness. Background sessions ignore pause, and ready_to_reset is a display
heuristic. A wrapper pause flag alone cannot enforce the planned no-rendering
edit condition. The plan records the source locations and three alternatives:
quiesce with cancel/wait before edits and require reset (recommended), relax the
promise to native scene-mutex synchronization, or seek a native acknowledged-pause
hook. These differ in accumulation, latency and integration scope, so work stopped
under the user's design-decision rule. No Cycles internal change was made.


## Phase 6 approved contract and draft (2026-10-13)

The user approved the synchronous edit barrier. API 1.5 adds reset/start/pause/
wait/cancel/sample limits, copied progress and named float-pass output callbacks.
Pause/resume without edits retains samples. The first attempted edit of a paused
job cancels and joins native work before touching its scene; reset is then required
before resume. That edit can block. An unpaused job rejects edits, and scene reads
require idle work. A host wait releases the API lock but pins session lifetime;
destroy/reset/edit/start return IN_USE until it returns. Another thread may still
query progress, resume or cancel. Callbacks borrow data and may not reenter the API
(except last_error), throw, or wait on another API call.

A nonblocking Blender viewport-style scene-mutex/edit/reset path is recorded as a
possible future minor-version addition only; it is not implemented. No native
Cycles or kernel change is needed for the approved blocking contract.

Phase 6 is not qualified yet. The retained baseline passed 11/11 tests; final
render/device/export checks and results will be recorded below.


## Phase 6 results (2026-10-13) — PASSED

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


## Phase 7 preflight results (2026-10-13) — STOPPED for design review

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


### Loader layout and dependency contract (Phase 7 implementation in progress)

On Windows, ship `cycles_c.dll`, `cycles_c_backend_private.dll` and the matched
runtime dependencies together. Only the front is a host API. It uses a static
C++ runtime so it does not import renderer or redistributable C++ DLLs; its
actual import table is checked, rather than assuming this from linker flags.
The private backend has one table getter with exact API/size/signature checks.
Its C++ types and allocations never cross into the front. Native implementation
and all node registration archives stay in the backend.

Before executing the backend, the front parses its PE imports and those of its
bundled dependencies. Every named/ordinal import from an already loaded module
must resolve. Version resources are compared against values captured from the
build's actual bundled DLL list. OSL and OCIO in the current toolchain have no
file version resource; their imported, version-namespaced symbols are checked,
but this cannot prove patch-level semantic equivalence. Hosts still need the
recorded version-matched dependency stack. The host must serialize dependency
loading/unloading around init; preflight is not a loader transaction.

No Cycles internal or kernel change is needed. Dependency compatibility is not
arbitrary-version isolation, and the private table is not a second stable API.
The existing process-lifetime library retention rule still applies.


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
