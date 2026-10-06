# CUDA deep storage

Surface capture uses up to 512 lanes. Each lane has 28 bytes of identity/status
and a configured-capacity event plane: event k for lane i is at `k * lanes + i`.
The host gathers completed lanes into compact spill records. Device pools are
host-allocated; kernels do not allocate per-thread event arrays.

Native VDB capture additionally stores density coefficients. It now uses 64 lanes
at 4096 events with a separate 32 MiB preflight reservation for host/device mirrors,
medium state and readback scratch. A compile-time bound checks the layout sizes.
See [VDB bounds](NATIVE_VDB_PLAN.md). Homogeneous volume capture uses bounded medium
tracking; it is no longer CPU-only.

Initialization, capture and readbacks share a Cycles DeviceQueue. One deep wait
per batch precedes host consumption. Cancellation and failures prevent publication.
Session capacity changes reallocate buffers after prior work has been consumed.
Final partial batches currently copy their allocated capacity.

## Measurement

`benchmark_cuda_capture.py` records one warm-up and three repeats for small
empty, opaque, mixed-depth and 64-layer surface fixtures. Historical results live
under `builds/validation/cuda-storage/`; they establish layout/readback behavior,
not production VDB throughput. Detailed old patch comparisons remain in Git history.

Reported counters include batches, waits, readback bytes and allocator-tracked
peak memory. Capture/readback time includes queued work and synchronization; it
is not isolated kernel or PCIe time. Spill time includes gathering and writes.
Allocator peaks are not measurements of resident VRAM.

The optional CUDA host-driver test exercises capacity changes, partial batches,
disabled deep, cancellation and callback failures. Repeated 1024x768 VDB renders
and device resource measurements now pass the fixture targets. Larger batches
still require explicit budget accounting and qualification. Shared source alone
does not qualify other backends.

[Release gates](../../DEEP_MILESTONES.md) | [Current evidence](../../DEEP_PERFORMANCE_AND_VDB.md)
## Current boundary traversal

Phase 2 uses native BVH candidates for CUDA bounds larger than 32 triangles;
smaller bounds and other backends retain the scan. Exact double-precision
refinement and crossing order are unchanged; stack overflow fails explicitly.
The current Blender sm_86 deep kernel uses 168 registers, no shared memory
and 8000 local bytes (7312 before Phase 2). Compiler spills remain 80-byte
stores / 76-byte loads. All 75 common beauty kernels retain their attributes.
Driver-recommended block size is 384 threads; this is not measured utilization.
Resources: `builds/validation/landscape-cloud/optimization-phase2/resources-{before,after}.json`.
The 587x250/four-sample run peaks at 4967 MiB device-wide; capture is 33.28 s.
See [plan results](../../DEEP_OPTIMIZATION_PLAN.md#6-results).
