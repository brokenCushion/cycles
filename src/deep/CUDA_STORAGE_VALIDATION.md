# Configured-capacity CUDA capture buffers

Architecture patch 3, 2026-09-19. The surface traversal and reconstruction
contract remain unchanged. CUDA volumes are a subsequent patch.

## Storage and ordering

`KernelDeepRecord` now contains only 28 bytes of identity/completion metadata.
Events occupy a separate preallocated `device_vector<KernelDeepEvent>` with
`512 * max(1, configured_capacity)` entries. Event k for lane i is at
`k * 512 + i`: neighboring active lanes write neighboring 20-byte events.
The host validates the count before gathering a lane into contiguous spill
storage. There are no GPU-thread allocations or per-thread event arrays.

| Configured events | Previous bytes per side | New bytes per side |
| ---: | ---: | ---: |
| 1 (opaque) | 669,696 | 24,576 |
| 8 | 669,696 | 96,256 |
| 16 | 669,696 | 178,176 |
| 64 | 669,696 | 669,696 |

Each side means either device storage or its host readback storage. The worst
case stays within the existing conservative 2 MiB staging/I/O allowance.
Spill disk layout and reconstruction budgets do not change. Capacity and lane
counts are bounded before multiplication; changing capacity reallocates the
event buffer after the previous batch has been consumed.

Initialization, capture and both readbacks use the same Cycles DeviceQueue.
Only one explicit deep synchronization remains per batch, at host consumption;
the prior post-kernel and allocation waits are removed. Initialization runs only
when a device buffer is allocated. Cancellation is checked before launch and
after readback; failed or incomplete results still prevent deep publication.

Diagnostic output records batches, explicit deep synchronization count, total
readback bytes, actual staging capacity, and the device allocator's tracked
peak bytes. `capture_readback_seconds` combines enqueue, queued work, copies and
the final wait; it cannot separate kernel execution from PCIe transfer. It can
include camera/beauty work already queued. `spill_seconds` includes host gathering
and spill writes. Allocator peak is not a driver measurement of resident VRAM.

## Validation and measurement

Artifacts are under `builds/validation/cuda-storage/`. The benchmark script
`benchmark_cuda_capture.py` runs empty, opaque, mixed-depth and 64-layer scenes
at 64x48, 16 samples, serially with deep off/on. Each case excludes one warm-up
and records three repeats. Process/render/export wall time is measured; these
small fixtures do not establish production throughput.

The native host-driver test accepts an optional final `CUDA` argument and
exercises session reuse with event capacities 64, 1, 8 and 2, odd dimensions,
disabled deep, cancellation before delivery and callback failures.

The Release build/install and all seven native CTest groups pass (10.07 seconds).
Eleven CUDA surface fixtures and four rejection cases pass, including exact
capacity and 64 layers. All 23 matching raw CSV ledgers are byte-identical to the
typed-record baseline. Maximum exported-curve error is `2.60770320892334e-8`;
CPU/CUDA depth difference is at most `1.9073486328125e-6`. Maximum surface beauty
difference is `3.725290298461914e-9`, within the existing accumulation tolerance.

The optional CUDA host-driver lifecycle run passes, including capacity changes
64 -> 1 -> 8 -> 2 within a reused session and partial batches. CUDA adaptive,
CPU/CUDA/OSL depth-of-field, and CUDA rigid-motion regression scripts also pass
their existing gates. No acceptance thresholds were relaxed.

The 640x480, 16-sample moving sphere/cube/cylinder review matches the saved
M7 CUDA baseline exactly: identical per-pixel sample counts and zero maximum
Z/ZBack/A difference. The first comparison mistakenly used the static source
against that motion baseline and correctly failed; using the matching
`builds/build-m6/m7-motion-primitives/scene.xml` resolves that setup error.
`review/cuda_storage_review.gfr` provides Before/After DeepToPointCloud nodes
with linked depth-cut controls. Both point clouds compute and the saved graph
reloads successfully. This CUDA-to-CUDA check does not change the documented
M7 CPU/CUDA geometry-edge limitation.

Warm-cache benchmark medians, seconds:

| Case | Before wall | After wall | Before capture + readback | After capture + readback |
| --- | ---: | ---: | ---: | ---: |
| Empty, capacity 2 | 0.735 | 0.712 | 0.0330 | 0.0137 |
| Opaque, capacity 1 | 0.759 | 0.736 | 0.0338 | 0.0127 |
| Mixed, capacity 8 | 0.760 | 0.740 | 0.0352 | 0.0171 |
| 64 layers, capacity 64 | 1.231 | 1.224 | 0.1387 | 0.1214 |

No other render or compiler jobs were launched during either benchmark series.
The desktop remained interactive; three repeats on small fixtures do not support
a universal speedup claim. Deep-disabled medians were 0.357–0.364 seconds before
and 0.361–0.376 seconds after. The 64-layer wall-time difference is small enough
to treat as noise. Spill/reconstruction/output still dominate total time.

Each benchmark records 49,152 samples in 96 batches. New measured readbacks are
3,342,336 / 2,359,296 / 9,240,576 / 64,290,816 bytes for the four cases above.
The previous full-record layout requires 64,290,816 bytes for each. There are
96 measured explicit deep waits; the former code requires 192 batch waits plus
two capture-call initialization waits for these workloads. The candidate's
allocator-tracked peak ranges from 589,477,464 to 590,155,124 bytes; these totals
include the other renderer allocations, not just deep staging.

`kernel-resources.json` compares the actual `sm_86` cubins. Its `on` key is the
candidate and `off` key is the preceding typed-record build; both have deep
compiled in. All 76 common kernels (including deep capture) have identical
driver-reported attributes. The deep kernel uses 168 registers, 6,736 local bytes,
zero shared bytes, and a recommended block size of 384. Cubin bytes change from
21,688,096 to 21,694,112. Recommended occupancy is not measured utilization.

## Remaining scope

The 512-lane batch limit remains. A partially occupied final batch copies its
allocated capacity; readback is not compacted to accepted/event counts. Full-chain
traversal remains, so divergence is not claimed solved. Larger batches, continuation
queues and overlapping staging buffers require separate measurements and budget
work. No overlapping-volume GPU benchmark exists while CUDA volumes are unsupported.
The subsequent M8c patch adds that restricted capture and a small overlapping-volume
benchmark; see [CUDA_VOLUME_VALIDATION.md](CUDA_VOLUME_VALIDATION.md).
