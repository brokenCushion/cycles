# CPU/CUDA homogeneous volume qualification

Historical homogeneous fixtures; current [M8 blockers](../../DEEP_IMPLEMENTATION_STATUS.md)
and [native VDB evidence](NATIVE_VDB_PLAN.md) take precedence.

## Implementation

`kernel/deep/volume.h` captures scalar absorption/scattering extinction with native SVM on
closed convex meshes, a static perspective pinhole camera and fixed samples.
It traces beyond the far clip to resolve containing media, then clips emitted
intervals. A first backface identifies a near-clip-inside ray. Incomplete chains,
128 traversal attempts or exhausted event/medium capacity fail publication.

CPU/CUDA share traversal and leave beauty state/RNG untouched. The host allocates
64 medium slots per lane through Cycles device buffers; kernel threads allocate
nothing. Homogeneous capture uses 512 lanes; native grids use the separate
64-lane/32 MiB reservation documented in [storage](CUDA_STORAGE_VALIDATION.md).
The beauty volume stack is not reused because it can silently ignore overflow.

## Evidence

Artifacts: `builds/validation/cuda-volume/`. Both backends pass 18 scenes and
13 rejection cases covering overlap, inside/clip rays, crossing surfaces,
partial coverage, misses, zero extinction, exact capacity and partial batches.
These fixtures do not cover every coincident or overlapping native grid boundary.
The newer 28-scene/19-rejection absorption and scattering results are under
`builds/validation/m8-production/scattering/{CPU-input,CUDA-input}`.

| Measurement | CPU | CUDA |
| --- | ---: | ---: |
| Maximum Gaffer/ledger curve error | 4.042e-7 | 4.170e-7 |
| Independent ray/box error | 1.285e-6 | 9.409e-7 |
| Beauty deep-on/off RGBA difference | 0 | 1.193e-7 |

Raw CPU/CUDA transmittance differs by at most 8.701e-7. Export tolerance is 1e-6;
ray/box geometry uses a separate 2e-6 gate. CUDA ordinary repeats show the same
beauty noise; single-sample comparisons are exact. Host lifecycle checks include
capacity changes 64 -> 1 -> 8 -> 2, partial batches and disabling deep.

The historical cubin comparison keeps all 75 non-deep kernel attributes unchanged.
The deep kernel uses 168 registers, zero shared bytes and 6,784 local bytes;
medium storage is a separate host-allocated buffer. These are historical compiler
attributes, not achieved occupancy or current production memory measurements.

Small serial benchmarks (64x48, 16 samples, one warm-up plus three retained runs)
have deep-enabled medians 0.769-0.921 s versus 0.393-0.445 s disabled. Each has
96 waits and 9,240,576 readback bytes. Device allocator peaks are 750-777 MB,
not driver-measured resident VRAM. See `benchmark/report.json`; these timings
include startup/export and do not establish production throughput.

## Reproduce / review

Run in Gaffer's Python, with `builds/build-m6/run-cuda.cmd` for CUDA:

```text
validate_volume_capture_gaffer.py EXE OUT/cpu CPU
validate_volume_capture_gaffer.py EXE OUT/cuda CUDA OUT/cpu
benchmark_cuda_capture.py EXE OUT/cuda OUT/benchmark --volume
```

Review: `cuda/m8_cuda_volume_review.gfr` under the artifact directory. It contains
the actual 160x120 four-sample overlapping-box render, CPU/CUDA deep readers,
editable cuts and live DeepToPointCloud nodes. Points represent interval boundaries.
