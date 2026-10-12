# OSL AOV redesign: research for Alex Fuller's #134021

2026-10-13, Australia/Sydney. Research only, pending Alex's answer about helping
finish his PR. This proposes changes to his implementation; it is not a competing
patch. No implementation, builds, or render tests were performed.

## PR structure and review

Local reference branch `upstream-pr-134021` points to
`86f9dc3e37db2709b316a7cfa5b410be58434138` (fetched without checkout, tags, or
updating FETCH_HEAD). Treat it as read-only; Git branches have no read-only flag.
Documentation remains on `feature/osl-aov`. The PR is a Blender tree with Cycles
under `intern/cycles/`; our standalone tree uses `src/`. Its dependency manifest
specifies OSL 1.13.7.0, so using the current 1.15.3 bundle requires adaptation.

| Part of Alex's patch | Keep or change for the redesign |
| --- | --- |
| `scene/osl.cpp`: discover disconnected AOV nodes, generate their dependencies before the material output | Keep traversal and shared-node reuse. Remove active-pass `offset >= 0` filtering; retain all relevant typed inputs in OSL graph simplification. |
| `scene/shader_nodes.cpp`, shader flags, three `node_output_aov_*.osl` files, shader CMake list | Keep integration points. Replace `Ci += ... * debug(Name)` with ordinary typed output parameters. Current main already has `has_aov_output_node`; reuse it when adapting. |
| CPU `execute_init` / per-AOV `execute_layer` loop; `entry_layers` and `osl_layer_indices` | Replace with one normal group execution and registered outputs. |
| Debug closure registration and flattening case; bytecode search for debug closures | Remove from the AOV implementation. AOV values must not enter the beauty closure tree. |
| AOV name/type/offset descriptors, film allocation and film-write routing | Keep the runtime mapping concept and existing film writers. Map shader-local slots to current passes outside the compiled group. |
| Scene-wide `renderer_outputs` attribute and `update_output_aovs` shader invalidation | Replace with stable per-group symbol locations. Pass selection must not alter group layout or JIT state. |
| `render_buffer` propagation through surface OSL evaluation and null-buffer call sites | Keep where needed for the post-execution flush; update signatures for current typed states and path visibility. |
| Vector AOV changes across film, passes, kernel data, SVM and scene nodes | Alex's additional scope; agree with him whether to retain separately. Color/value acceptance stays mandatory. |

Brecht's [review](https://projects.blender.org/blender/blender/pulls/134021#issuecomment-1447378)
asked to preserve renderer outputs instead of manually executing multiple layers.
Other comments asked to share the primary-path eligibility check, use braces,
and write film contributions directly rather than allocating an additional
Cycles closure. The later mailing-list recommendation refines this: Larry
[recommends output placement](https://lists.aswf.io/g/osl-dev/message/5476), and
Brecht [requires intermediate storage](https://lists.aswf.io/g/osl-dev/message/5479)
for synchronization, viewport changes, and texture retries. These are design
requests, not evidence that the PR meets acceptance.

## Issues found in the current PR head

Two notes for Alex while adapting head `86f9dc3e37db2709b316a7cfa5b410be58434138`:

- **Primary-path condition:** the surface check in
  [intern/cycles/kernel/osl/closures.cpp:203-204](https://projects.blender.org/blender/blender/src/commit/86f9dc3e37db2709b316a7cfa5b410be58434138/intern/cycles/kernel/osl/closures.cpp#L203)
  and the volume check at
  [276-277](https://projects.blender.org/blender/blender/src/commit/86f9dc3e37db2709b316a7cfa5b410be58434138/intern/cycles/kernel/osl/closures.cpp#L276)
  use OR. It would help to reuse the SVM eligibility condition, which requires
  transparent-background primary status AND single-pass-not-done, plus a valid
  render buffer. OR permits additional paths. The same head's reference is
  [intern/cycles/kernel/svm/aov.h:13-19](https://projects.blender.org/blender/blender/src/commit/86f9dc3e37db2709b316a7cfa5b410be58434138/intern/cycles/kernel/svm/aov.h#L13).
- **Cache-miss discard:** the debug-closure case in
  [intern/cycles/kernel/osl/osl.h:124-140](https://projects.blender.org/blender/blender/src/commit/86f9dc3e37db2709b316a7cfa5b410be58434138/intern/cycles/kernel/osl/osl.h#L124)
  checks only `render_buffer` before the film write. There is no
  `SR_CACHE_MISS` guard there. For current on-demand texture loading, discarding
  the intermediate record before any film write would avoid committing an
  incomplete result and then adding the retried result again.

## OSL 1.15.3 capability check

Read-only inspection of the installed bundle confirmed
`lib/windows_x64/osl/include/OSL/oslversion.h:20-23` is **1.15.3.0**, and
`oslconfig.h:7` declares `OSL_USE_OPTIX 1`. The public header contains the requested
APIs. Release source was inspected at tag `v1.15.3.0`, commit
`76abe62db250664354389329feb3f4f9e5fc7891`; downloads are disposable.

| Backend | Source conclusion |
| --- | --- |
| CPU | Supported. `SymLocationDesc` carries type, byte offset and stride; `add_symlocs(group, ...)` registers output locations; `execute` passes `output_base_ptr` into generated code. |
| OptiX GPU | Supported in the generated ABI/code path, conditional on an OptiX-enabled OSL build. The fused callable forwards the same pointer and shade index into init/entry code, which includes output copies. Current Cycles camera shaders already use it. Runtime behavior for this AOV design remains untested. |

Pinned OSL source references:

- [Public API](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/include/OSL/oslexec.h#L69): descriptors at 69-105, CPU execution at 787-797, group registration at 1031-1037.
- [Output arena](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/include/OSL/oslconfig.h.in#L247): `SymArena::Outputs`.
- [Output identification](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/liboslexec/shadingsys.cpp#L3650): output symlocs recognize both `param` and `layer.param`; a separate global list of user AOV names is unnecessary.
- [Layer retention/execution](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/liboslexec/oslexec_pvt.h#L1292): renderer-output layers cannot be lazy. `instance.cpp:506-509` marks them; `llvm_instance.cpp:1711-1718` runs them from the single entry.
- [Generated copies and GPU ABI](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/liboslexec/llvm_instance.cpp#L1496): fused callable at 1496-1539, typed non-closure output copies at 1805-1847, shared layer generation at 2323-2340, PTX emission at 2494-2497. `backendllvm.h:364-373` computes `base + offset + stride * shadeindex`.
- [Coverage caveat](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/76abe62db250664354389329feb3f4f9e5fc7891/src/testshade/testshade.cpp#L349): testshade explicitly disables output placement in OptiX mode. Its GPU tests cannot establish placement correctness.

Cycles precedent at base `a456b761034dda42c32eef9f4aae0fa5a5c9f604`:
[camera registration](https://github.com/blender/cycles/blob/a456b761034dda42c32eef9f4aae0fa5a5c9f604/src/scene/osl.cpp#L183),
[CPU buffer](https://github.com/blender/cycles/blob/a456b761034dda42c32eef9f4aae0fa5a5c9f604/src/kernel/osl/closures.cpp#L425),
and [OptiX buffer](https://github.com/blender/cycles/blob/a456b761034dda42c32eef9f4aae0fa5a5c9f604/src/kernel/osl/camera.h#L64).
Material callables currently pass a null output pointer; they need integration.

## Proposed flow on top of Alex's patch

1. **Stable shader outputs.** Give each AOV node layer ordinary `color` and
   `float` outputs with fixed parameter names, and register `layer.param` symlocs
   before optimization/JIT. Keep both inputs because the active pass currently
   determines type. User names remain descriptor metadata, so arbitrary names
   do not require dynamically generating OSL source. Each node has separate
   slots, including nodes sharing an AOV name; preserve SVM's multiple-write
   behavior. Compile material output last and omit custom `entry_layers`.
2. **Pass-independent layout.** Lay out every node's slots using OSL types and
   byte offsets, independently of selected passes. OSL graph simplification
   must preserve AOV dependencies even without a matching pass; leave SVM
   simplification behavior intact. Changing pass selection/order/type updates
   only slot-to-film mappings and film storage. Remove Alex's scene-wide
   output list and OSL shader invalidation for such changes; audit equivalent
   invalidation in current main. Editing the shader graph can still recompile.
3. **Private execution storage.** Clear scratch for each shading invocation,
   pass it as `output_base_ptr` with placement shade index zero, and retain it
   through completion. This index is separate from `ShaderGlobals::shade_index`
   used by Cycles services. Provide valid scratch even on paths that will not
   commit film writes: generated output copies must never target null storage.
   Do not share scratch by render pixel, across concurrent paths, or reentrant
   evaluations. Bump/displacement/volume groups without AOV outputs retain
   their existing execution behavior.
4. **Conditional film flush.** After successful full shader evaluation, discard
   the entire record on `SR_CACHE_MISS`. Otherwise require a non-null film
   buffer, `PATH_RAY_TRANSPARENT_BACKGROUND`, and absence of
   `PATH_RAY_SINGLE_PASS_DONE`, using the same eligibility helper as SVM.
   Ignore unmatched names/types and invalid offsets. Flush each mapped slot
   through `film_write_aov_pass_color/value` exactly once; retries start with
   cleared scratch. Background must match SVM, reflection must not write.
   Keep AOV data out of `Ci` and closure flattening.

CPU uses `ShadingSystem::execute` and execution-private host scratch, preserving
existing thread ownership. OptiX supplies device-addressable scratch to the
existing fused direct callable using the launch-scoped storage described below,
with no new AOV-count cap. Output storage is separate from OSL's internal groupdata.
The GPU flush uses existing film atomics; CPU retains existing film-writer
scheduling assumptions. There is no direct OSL write into shared film memory.

## GPU scratch sizing

The film flush/discard runs in the same kernel invocation immediately after the
OSL callable returns. The scratch contents therefore live only for that
invocation within the launch; nothing must persist with a path between kernels.
Use one device buffer, indexed by the launch thread index
(`optixGetLaunchIndex().x` for the current one-dimensional launch), rather than
the integrator path index or render pixel:

`scratch_bytes = max_launch_threads * max_slot_bytes_per_group`

`max_slot_bytes_per_group` is the maximum aligned byte span of the compiled
groups' output symlocs: the largest output offset plus its typed size, including
any requested derivatives and padding. Finalize it after JIT, independently of
selected film passes. Each thread receives its own record and passes that record
pointer to OSL with placement shade index zero. Clear before evaluation and
flush/discard before reusing it for another evaluation in the same thread.

Allocate once per device for the configured maximum launch size, reuse across
serialized launches, and resize when shader recompilation changes the maximum
record size. Wait for existing users before reallocating. AOV selection changes
alone require neither resizing nor recompilation. Concurrent launches must be
serialized or assigned disjoint buffer regions; nested evaluations must not
overwrite a still-live record.

For an illustrative batch of **65,536 launch threads**, 64 AOV
nodes retaining both a color (three 32-bit floats) and a value (one float) need
`64 * 16 = 1,024` bytes per thread, or **64 MiB** total. A color-only layout
would be 48 MiB. At 262,144 threads the dual-output layout is 256 MiB; at
1,048,576 threads it is 1 GiB. These are arithmetic estimates before extra
padding or metadata, not measurements on this busy machine.

Size from the actual queue's maximum launch work size, not the number of
hardware-resident threads. Current
[OptiX enqueue](https://github.com/blender/cycles/blob/a456b761034dda42c32eef9f4aae0fa5a5c9f604/src/device/optix/queue.cpp#L215)
launches `work_size` threads; the
[path scheduler](https://github.com/blender/cycles/blob/a456b761034dda42c32eef9f4aae0fa5a5c9f604/src/integrator/path_trace_work_gpu.cpp#L519)
can dispatch large batches. The 65,536 example is not a verified launch cap.
Storage scales with the largest supported launch, rather than reserving a
record for every path in flight; memory savings depend on that launch bound
being smaller than the full path pool.

A stack/local-memory record is an alternative for small fixed layouts, but the
required group size is known only after JIT, after the caller kernel is compiled.
A fixed caller array would need a conservative limit or kernel recompilation;
dynamic local allocation also requires stack/occupancy analysis. A device buffer
can be sized from the final descriptors without a new AOV-count cap or caller
rebuild, making it the preferred starting point. Validate allocation bounds and
performance when implementation is authorized.

## Checks required after Alex agrees

Keep all acceptance in `OSL_AOV.md`: CPU SVM parity, CPU/OptiX OSL parity,
unchanged beauty/non-AOV passes, and existing tests. Include texture misses and
retries, shared dependencies and Script nodes, transparency/reflection/world,
multiple nodes/materials, and unmatched names. Also verify selection changes
do not re-JIT groups, duplicate nodes preserve accumulation, and scratch never
leaks stale results or overruns. AOV-only scripts can affect shader globals or
evaluation order; beauty invariance is a required test, not an assumption.

Only this note was added. No source files changed, and the PR reference branch
was left untouched after fetching. No patch was applied, remote content changed,
or timing measurement made.
