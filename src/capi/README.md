# Cycles C API — Phases 1–4 accepted; Phase 5 draft stopped

Experimental shared-library boundary for host applications such as Gaffer.
**Experimental until Phase 7**; the stable ABI promise has not begun.
The contract is [cycles_c.h](cycles_c.h): it contains no Cycles C++ headers or
deep types. It initializes resource paths, enumerates native devices, reports
compiled capabilities, routes logs and introspects built-in node types, sockets,
defaults, flags and enums. The accepted API 1.3 creates device-selected native Sessions and
borrows their scenes, edits sockets by name or cached ID, and builds shader graphs
including OSL scripts. It does **not** start renders yet.

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
`cycles_c_exports` requires the exact 86 public functions in the unqualified Phase 5 draft (80 in Phase 4), rejecting extra
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

Accepted coverage is limited to Phases 1–4. The Phase 5 draft below is not qualified. ABI evolution and render
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
