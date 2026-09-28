# Deep storage and publication

M8 owns production qualification. Historical M5 measurements remain in Git
history; current results are in [measured evidence](../../DEEP_PERFORMANCE_AND_VDB.md).

## Storage

`Capture` stores complete camera-sample identities, misses and typed events in
bounded memory with temporary-file spill. Duplicate/incomplete samples fail.
Raw event payload is compact; capacity is not reserved on disk for every unused
slot. CPU and CUDA use host-preallocated capture buffers.

`deep-memory-mb` preflights deep capture, spill pages, reconstruction and export
working storage. Authoritative byte accounting lives in `capture.cpp` and worker
allocation code. It is not a total-process RSS limit: scene, shaders, beauty and
OS caches are separate. Exhausted capacity or failed I/O fails the render.

The `Deep output` log reports export time, logical spill read/write bytes and
combined spill-file size. Transfers include index initialization and page rereads;
OS caching means these counters are not physical disk traffic. The host callback's
export time includes reconstruction, serialization and publication.

Export reconstructs one pixel at a time and retains one FLOAT scanline. Native
volume export uses synchronous OpenEXR compression to bound writer buffering.
Preflight assigns the remaining memory to a row-sample capacity. The writer checks
actual row counts before FLOAT allocation; it no longer requires every pixel in
a wide row to reserve its maximum count. Exceeding the row budget preserves the
prior output and reports the affected row. Repeated 1024x768/four-sample CPU/CUDA
VDB renders pass the stated accuracy, memory, time and output-size targets.

## Reduction

- Surface `--deep-reduce`: optional 0.001 whole-curve allowance, including FLOAT
  export. Preserves final transmittance and checks both sides of all boundaries.
- Volume: bounded streaming merges share the existing 5e-8 reconstruction
  allowance with mixture fitting. Repeated merges accumulate their error bounds;
  surface steps and empty gaps are preserved. Numerical tests and the supplied
  VDB's production output-size/performance gates and final scene matrix pass.
- Volume export also coalesces adjacent fitted intervals within 2.5e-7 of the
  existing export allocation. It reprojects FLOAT coefficients and rechecks the
  complete original curve under the unchanged 1e-6 budget. Steps and gaps remain.
  This reduces file size and long FLOAT flattening accumulation in consumers.
  Preflight now reserves 240 bytes per fitted interval for pixel export scratch.
- Gaffer's Max Points only thins the preview. It does not reduce EXR samples.

## Publication and failures

One owner exports after sample completion. A uniquely reserved sibling temporary
file is flushed and closed before atomic replacement. Cancellation, validation,
I/O or callback failure preserves any previous completed EXR. Normal exception
cleanup removes temporary output. Hard termination may leave a staging directory.
Network-share atomicity and power-loss durability are not qualified.

Beauty, deep and diagnostic CSV are separate publications, not a frame transaction.
Standalone beauty identity metadata identifies the paired beauty file; it does
not turn multiple outputs into one atomic operation.

`production_test.cpp` covers spill equivalence, concurrency, capacity errors,
window offsets, injected allocation/source-I/O failures, mid-export cancellation
and failed replacement. `deep_output_driver_test.cpp` covers CPU/CUDA surface
and volume callback/cancellation recovery, including re-enabling deep output.
Injected device errors suppress delivery and reach render status. Writer tests
cover direct stream-write failures and Windows locked-destination replacement,
checking that the existing EXR survives and staging files are removed.
Injected disk-full failure is not a physical-drive exhaustion test.

[Release gates](../../DEEP_MILESTONES.md) | [EXR contract](EXR_VALIDATION.md)
