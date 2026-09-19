# Deep output architecture review against peer review requirements

Date: 2026-09-19. Scope: local M0–M8b implementation, including uncommitted
changes on top of `659e8dda9`. This is a reviewed implementation plan, not a
claim that all proposed interfaces or optimizations already exist.

**Implementation update:** patches 1–4 are implemented in the working tree. See
[NATIVE_OUTPUT_VALIDATION.md](NATIVE_OUTPUT_VALIDATION.md) and
[TYPED_RECORD_VALIDATION.md](TYPED_RECORD_VALIDATION.md). The findings below
record the pre-patch audit. The implemented host API uses bounded per-pixel
reads rather than adding a separate packed scanline format; the standalone
adapter assembles scanlines. Reconstruction tolerances remain fixed to the
qualified surface/volume contracts. Typed records use separate alpha/optical-depth
fields and explicit status/reason codes; `OpaqueCapture` is now `deep::Capture`.
Patch 3 separates 28-byte lane metadata from configured-capacity event planes
and uses one explicit deep wait per batch. See
[CUDA_STORAGE_VALIDATION.md](CUDA_STORAGE_VALIDATION.md) for correctness,
readback bytes and measured resource/timing results. Broader performance
qualification remains open. Patch 4 shares homogeneous volume traversal between
CPU and CUDA with a bounded device medium pool; see
[CUDA_VOLUME_VALIDATION.md](CUDA_VOLUME_VALIDATION.md) for its restricted scope
and qualification results.

## Decision

Keep the validated surface/volume reconstruction and CPU behavior as references.
Before adding CUDA volumes, introduce a host-facing output contract and a shared
device capture contract using existing Cycles ownership and queue patterns.
Do not port `volume_cpu.h` wholesale or build a second rendering framework.
Do not require Blender or Gaffer to configure, render or receive deep output.

## Findings from this checkout

| Requirement | Evidence | Assessment / action |
| --- | --- | --- |
| Standalone Cycles | `src/deep` uses C++/OpenEXR; Gaffer scripts consume output files | Satisfied for runtime dependencies. Keep visualization tools separate from renderer dependencies. |
| Native ownership | CLI owns `OpaqueCapture`; `Film::deep_capture` is a non-owning pointer | Gap. Move lifetime/configuration to the session/path-trace layer; Film should describe film settings, not point into application-owned capture storage. |
| Reusable output | `Session::set_output_driver`, `PathTrace::tile_buffer_write`, `PathTraceTile` already connect renderer output to hosts | Gap. Deep export currently runs separately in the CLI after the session. Extend this existing output route. |
| Device allocation | `PathTraceWorkGPU::deep_records_` is a `device_vector`; allocation and transfer use `DeviceQueue` | Already follows native allocation mechanisms. Retain those mechanisms. |
| No GPU-thread allocation | CUDA writes `record->events` through a global-memory pointer | Satisfied for current surface capture. The 128-float event array is inside preallocated device records, not an automatic per-thread array. |
| Small thread-local state | CUDA surface traversal uses ShaderData; CPU volume uses a complete CPU state copy and three 64-element tracking arrays | Gap for a GPU volume port. Do not transfer CPU storage choices unchanged. Profile shader state as well as event storage. |
| Coalesced access | `KernelDeepRecord` interleaves lane metadata and 128 event floats | Layout candidate for improvement: adjacent lanes writing event k address records 532 bytes apart. This is a source-level observation, not a measured bandwidth result. |
| Minimize synchronization | `capture_deep_tiles` handles 512 records per batch; synchronizes after launch, then copies and synchronizes again | Confirmed overhead opportunity. The queue contract explicitly supports ordered copies without an intermediate host synchronization. Validate error/cancellation handling before combining waits. |
| Divergence | CUDA loops through an entire visibility chain in one invocation | Correctness works for qualified cases; execution cost is unqualified. Compare bounded continuation work with the current complete-chain baseline. |
| Explicit failure | Negative record counts distinguish skip/failure; storage and publication reject incomplete chains | Preserve fail-closed behavior, but replace overloaded counts with explicit status/reason fields. |
| Shared devices | CPU capture is inside CPU architecture implementation; CUDA has a separate surface traversal; volumes are CPU-only | Define shared kernel logic and record meaning before adding further copies. Compilation availability does not establish backend correctness. |
| Appropriate libraries | Neutral reference/capture/writer targets support independent numerical and EXR tests | Practical benefit exists. Keep them while useful; no requirement to create more libraries or move everything at once. |
| Naming | `OpaqueCapture`, `deep_surface`, `WITH_CYCLES_DEEP_OPAQUE` now cover more than their names imply | Rename as part of the relevant ownership/kernel patches, with build-option compatibility; do not do a broad unrelated naming cleanup. |

### Memory and performance facts

Current surface record: four unsigned metadata fields + one int count + 128
floats = 532 bytes in the current layout. A 512-record batch carries 272,384
bytes of payload in each full host/device copy. This is not total GPU memory:
integrator state, shader resources, device allocator behavior and host staging
must also be counted. Validate layout explicitly when changing it.

The M6 resource report lists 168 registers and 6,720 local bytes for its deep
kernel. Those are historical measurements from the earlier cubin, not measured
attributes of the current M8 tree. They do not prove that the global event
array caused local-memory usage. Rebuild/profile the actual candidate before
making resource or speed claims. See `CUDA_VALIDATION.md` and
`measure_cuda_resources.py`.

## Proposed host ownership and output contract

Follow the existing `Session -> PathTrace -> PathTraceWork` hierarchy:

1. Add a small deep-settings structure in the session layer: enabled capture
   mode, event limit, total deep working-memory budget and reconstruction error.
   File paths and CLI syntax stay in the application/output driver.
2. PathTrace owns the capture ledger, completeness state and reconstruction
   lifetime. CPU/GPU work implementations receive the same capture contract.
   Workers do not own publication or an output-driver callback.
3. Extend `OutputDriver` with an opt-in deep capability and a final deep-tile
   callback. Existing drivers have a default unsupported capability; enabling
   deep with such a driver produces a clear error, never silent data loss.
4. A host-only `DeepTile` exposes image/data window, layer/view, sample population,
   depth convention and synchronous bounded row reads. Each row supplies pixel
   sample offsets/counts and reconstructed Z/ZBack/A arrays. Raw optical depth
   is not confused with output alpha. No dependency on Imf, FILE, or Gaffer is
   exposed through this interface.
5. Tile/row storage is valid only during the callback/read scope. Hosts copy data
   if they need it longer. Reconstruct rows on demand; no mandatory whole-frame
   allocation. A non-file test driver must be able to consume the same output.
6. For the first integration, retain the current full-frame/no-tiling restriction.
   Reuse the tile identity mechanism without claiming tiled, multi-device or
   resume support. Reject those combinations until their lifecycle is tested.
7. Invoke final deep delivery only after workers join and sample completeness
   succeeds. Flat beauty remains available through the existing callback.
   Define callback failure propagation through existing session progress/error
   handling and keep cancellation checks between reconstructed rows.
8. The standalone deep-capable driver streams rows to the atomic EXR writer.
   Final publication occurs only after all rows, close and cancellation checks
   pass. Beauty identity/file pairing is a driver responsibility. Non-file hosts
   receive explicit completion/failure and must stage results until completion.

Candidate API names are `DeepSettings`, `OutputDriver::DeepTile`,
`supports_deep_output()` and `write_deep_render_tile()`. Final signatures must be
implemented alongside `PathTraceTile` and its tests, not frozen in a separate
plugin ABI. Document that adding virtual methods requires host rebuilds.

## Proposed shared capture contract

Kernel data uses Cycles primitive types, explicit fields and device addresses;
no STL, exceptions, locks, files, ordinary host pointers or dynamic allocation.

- Identity: full-image pixel, accepted camera sample, population where needed,
  and render/batch generation. A reused path index alone is not an identity.
- Status: not scheduled, skipped by sampling, active, complete, or failed.
  A complete zero-event chain is a miss; a skipped lane is not a miss.
- Failure reason: event capacity, traversal/medium-state capacity, unsupported
  state/material, nonfinite data or device failure. Failed data never publishes.
- Event: front depth, back depth, value and explicit kind. Surface value is local
  alpha; volume value is integrated scalar optical depth. Volume intervals may
  overlap on a raw ray; reconstruction handles their joint transmittance.
- Completion is written only after the complete visibility chain resolves.
  Host consumption is ordered after the relevant kernel work/copy has completed.
- Preserve accepted ray, lens/time, identity and weight semantics. Do not draw
  extra beauty RNG values or expose partially modified beauty state to another
  queued stage. Surface-only behavior must remain unchanged.

Suggested placement: shared collection helpers beside existing integrator
surface/volume traversal in `src/kernel/integrator`; data declarations with
kernel types; storage/scheduling in `src/integrator`; settings/output in
`src/session`. CPU architecture and GPU entry points remain thin adapters.
Use existing kernel feature masks, registration and backend compilation paths.
Avoid a new generic device abstraction.

## Bounded device storage and scheduling

Start with a host-allocated pool for N active capture lanes and E events per
lane. Keep metadata separate from event planes. A candidate event index is
`event_number * lane_capacity + lane_slot`, so neighboring active lanes writing
the same event use neighboring memory. Each lane owns its offsets/count;
event writes therefore do not require a shared append atomic. Queue compaction,
if used, follows Cycles' existing bounded-counter mechanisms.

For a candidate 16-byte event, event payload is `16*N*E` bytes. Add explicit
metadata/continuation bytes, device staging, host readback, spill buffers and
reconstruction working set to the budget. Compute checked sizes before launch;
reduce N or reject configuration when the budget cannot fit. E is the configured
capacity, not an unconditional 64-event reservation for every beauty path.
Keep active-lane storage distinct from the full image/sample population.

Volume medium tracking must also live in bounded storage or reuse a proven
Cycles volume-stack mechanism on separate capture state. Do not put the CPU's
three 64-element tracking arrays into every GPU thread. Preserve camera-inside
and far-clip-inside behavior, overlap, and explicit overflow when replacing that
algorithm; the native volume stack's own limits must be audited before reuse.

First remove redundant synchronization using ordered queue operations and a
single wait at host consumption. Do not assume asynchronous overlap or pinned
memory behavior across every backend. Then measure larger batches and bounded
staging reuse. Two staging buffers are only justified if measurement shows
overlap benefits and the combined memory fits the budget.

For divergence, compare the current full-chain traversal with bounded work per
dispatch plus a continuation queue. Keep per-ray progress in capture storage;
do not borrow live beauty state across asynchronous dispatches. Retire misses
and complete rays, compact only unfinished capture work using existing queue
patterns, and preserve camera identity through compaction. Launch/compaction
overhead may outweigh gains on short chains, so choose from measurements rather
than assuming a wavefront split is automatically faster. Shader and volume
work may need separate stages if resource measurements justify them.

## MoonRay reference: what to adapt and what not to copy

Inspected public source at commit
`eef67ae992b5037943a7716cca96ed443c36dcec` (no code copied):

- [DeepBuffer.h](https://github.com/OpenMoonRay/moonray/blob/eef67ae992b5037943a7716cca96ed443c36dcec/lib/rendering/pbr/core/DeepBuffer.h):
  separates per-ray volume input segments from output segments and radiance/AOV
  samples. Useful conceptual distinction; our current scope remains scalar
  extinction without deep RGB. Its documented volume coverage simplification
  is not our numerical acceptance contract.
- [DeepBuffer.cc](https://github.com/OpenMoonRay/moonray/blob/eef67ae992b5037943a7716cca96ed443c36dcec/lib/rendering/pbr/core/DeepBuffer.cc):
  inspected surface insertion uses malloc/linked segments and a mutex-protected
  variant. These host mechanisms are not a GPU allocation design to import.
  No claim that this source provides bounded per-GPU-thread deep storage.
- [RenderOutputDriverImplWrite.cc](https://github.com/OpenMoonRay/moonray/blob/eef67ae992b5037943a7716cca96ed443c36dcec/lib/rendering/rndr/RenderOutputDriverImplWrite.cc):
  routes deep writing through renderer output handling and supplies per-pixel
  sample counts. Adapt the output/lifecycle concept to Cycles' own OutputDriver.
- [RenderOutput documentation](https://docs.openmoonray.org/user-reference/scene-objects/render-output/RenderOutput/):
  deep is an output type. This supports exposing deep through the host output
  interface, not restricting it to a standalone command-line side effect.

This is targeted source inspection, not a comprehensive audit of MoonRay GPU
execution, compression, or production correctness.

## Patch order and acceptance gates

1. **Native ownership/output integration.** Move configuration and capture
   lifetime into session/integrator code; implement deep OutputDriver delivery
   and a standalone writer adapter. Add a non-file consumer test, unsupported
   driver failure, cancellation and lifecycle checks. Preserve existing EXRs
   and beauty results. Implemented; see the native output validation record.
2. **Shared typed records and CPU adapter.** Replace surface pairs/volume triples
   and overloaded count markers with the shared contract. Preserve spill
   budgets and all M7/M8a/M8b numerical tests; resolve naming with this change.
   Implemented with updated byte accounting; see typed record validation.
3. **CUDA surface buffer/scheduling migration.** Implement preallocated bounded
   device layout with explicit status and queue-ordered readback. Compare the
   qualified surface results and collect current register/local-memory, peak
   allocation, transfer bytes, synchronization count and isolated throughput.
   Implemented and measured on surface fixtures; see CUDA storage validation.
   Larger-batch/continuation and production throughput qualification remain open.
4. **CUDA homogeneous volumes.** Use the shared capture helpers and bounded
   medium state. Pass camera-inside, clipping, overlaps, surface crossings,
   partial coverage, overflow and deep-on/off beauty tests. Present in Gaffer.
   Implemented for the static, fixed-sample M8b contract; see CUDA volume validation.
5. **Other backends.** Build and qualify OptiX/HIP/Metal/oneAPI separately. Shared
   source does not imply identical intersection or resource behavior. Unavailable
   hardware remains explicitly untested and unsupported.

For stages 3–4, measure repeated isolated runs with deep off/on across empty,
opaque, many-layer, mixed-depth and overlapping-volume scenes. Separate capture,
readback, spill, reconstruction and EXR time. Do not invent a speedup target from
the current mixed-workload timings. Preserve the 1e-6 deep-curve gate and report
native CPU/GPU geometry differences separately, as in M7.

No renderer behavior changed for this audit. Existing Gaffer review artifacts
remain the correctness baseline; a new visual review accompanies implementation
changes, not a documentation-only architecture review.
