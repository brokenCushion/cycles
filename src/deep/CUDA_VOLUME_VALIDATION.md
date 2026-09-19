# M8c: bounded CUDA homogeneous absorption capture

## Contract and implementation

The CPU and CUDA paths share `kernel/deep/volume.h`. This retains the M8b
contract: constant scalar absorption, closed convex meshes, static perspective
pinhole camera, positive near clip, fixed sampling, and native SVM shaders.
Volume materials are pure absorption; qualified surfaces may cross the media.
No heterogeneous density, VDB, scattering, deep RGB, volume DOF, adaptive
sampling or motion support is implied.

The accepted camera ray is traced through volume boundaries, including beyond
the far clip to resolve media containing either clip plane. A first backface
means the near clip is inside that medium. Intervals are clipped before emission;
optical depth uses physical ray length, and reconstruction combines overlapping
media. Traversal has a hard 128-intersection limit. Exceeding this or the event/
medium capacity fails the capture and preserves the existing final deep file.

`KernelDeepMedium` is an eight-byte object/start record; a negative start marks
an exited medium. The GPU worker allocates 64 visited-medium planes for 512 lanes
with the existing `device_vector` API. Lane i's medium k is at `k * 512 + i`.
The buffer is allocated only for volume capture, reused between completed
batches, and freed when a surface capture reuses the worker. There are no
per-thread medium/event arrays, dynamic allocations, locks or append atomics.
The CPU adapter uses host-local scratch and the same traversal helper.

The existing native beauty volume stack is not borrowed: its enter operation
may silently ignore overflow and it belongs to the continuing beauty path.
Deep needs explicit overflow and independent near/far-clip interval tracking.
The shared helper leaves the beauty ray, intersection, path flags, RNG and
volume stack untouched. It evaluates only the allowlisted side-effect-free
shaders against local ShaderData and the accepted path identity.

CUDA capture also accepts the initial `INTERSECT_VOLUME_STACK` state used when
the camera may be inside a medium. Completed records/events use the existing
ordered readback and one explicit deep wait per batch. Medium scratch is never
copied back. It costs 262,144 bytes on device and the same host allocation;
with maximum event staging the total is 1,863,680 bytes, within the existing
conservative 2 MiB staging allowance. Other renderer allocations remain outside
the deep working-buffer budget. `medium_bytes_each` reports this allocation.

## Reproduction

Artifacts live under `builds/validation/cuda-volume/`. Use the installed renderer
at `builds/install-m6/cycles.exe` and Gaffer's `env python` launcher:

```text
validate_volume_capture_gaffer.py EXE OUT/cpu CPU
validate_volume_capture_gaffer.py EXE OUT/cuda CUDA OUT/cpu
benchmark_cuda_capture.py EXE OUT/cuda OUT/benchmark --volume
```

CUDA runs need the toolkit environment from `builds/build-m6/run-cuda.cmd`.
Use absolute executable/resource paths to reuse the same compiled-kernel cache.
The optional CUDA native host test now includes volume delivery, event-capacity
changes, odd batch dimensions and disabling deep on a reused session.

## Validation status

The Release build/install and seven native CTest groups pass (10.30 seconds).
The expanded CPU suite passes 18 scenes and 13 rejection cases; maximum Gaffer
cut error is 4.0410239277033355e-7 and independent ray/box error is
1.2843861837419368e-6. Beauty is exactly unchanged in these CPU tests.
All nine matching CPU raw ledgers are byte-identical to the preceding typed-record
validation, including the overlapping-volume review.
CUDA also passes all 18 scenes and 13 rejection cases. Its maximum exported
Gaffer cut error is 4.169288847499786e-7, analytic ray/box error is
9.408919675601979e-7, and raw CPU/CUDA transmittance difference is
8.700794420501978e-7. Maximum beauty difference is 1.1920928955078125e-7;
the repeated deep-disabled renders show the same maximum difference. Every
single-sample CUDA beauty comparison is exact. The native CUDA host lifecycle
test passes, including volume delivery after capacity changes 64 -> 1 -> 8 -> 2,
partial batches and disabling deep. CUDA adaptive, CPU/CUDA/OSL depth-of-field
and CUDA motion regressions pass their existing gates. Eleven CUDA surface
fixtures and four rejection cases pass; all 23 matching raw surface ledgers are
byte-identical to the preceding CUDA storage build.

The suite includes homogeneous and overlapping fog, camera/near/far clip inside,
both clips inside, opaque/transparent surfaces in/before/after fog, partial
coverage, misses, zero extinction, exact event capacity and partial batches.
Failure cases include the existing material/camera restrictions, event overflow
and 65 nested media. Deep cut/oracle thresholds are unchanged. CUDA multisample
beauty uses the existing DOF suite's FLOAT accumulation bound (`samples * 2^-23`
for these unit-range fixtures), with repeated deep-disabled renders measured
separately. Single-sample CUDA and every CPU beauty comparison remain exact.

## Kernel resources

The actual sm_86 cubin is compared with the preceding CUDA storage-patch cubin
in `kernel-resources.json`. The script's `on` key means this candidate and `off`
means the preceding surface-only capture build; both have deep compiled in.
All 75 common non-deep kernels have unchanged driver-reported attributes.
The deep kernel remains at 168 registers and zero shared bytes; local storage
changes from 6,736 to 6,784 bytes (48 bytes more). Driver-recommended block size
remains 384 and minimum grid blocks 68. This is not achieved occupancy.
Cubin size changes from 21,694,112 to 22,163,744 bytes. The medium arrays reside
in the host-allocated global buffer, not in that per-thread local storage.

## Gaffer review

`cuda/m8_cuda_volume_review.gfr` is the actual 160x120, four-sample overlapping
box-volume render. `VolumePoints` displays CUDA front/back interval points in
blue/orange. `CPUVolumePoints` supplies the CPU comparison. Both use the actual
camera field of view and share `VolumeDepthCut.farClip.value` for slicing.
`CutAlpha` shows flattened opacity; `CyclesBeauty` is the ordinary beauty image.
The point clouds visualize deep interval boundaries, not scattering particles.
The graph reloads and all four point nodes evaluate successfully.

## Small-fixture cost measurement

The volume benchmark runs serially at 64x48, 16 samples and event capacity 8.
Each case excludes one warm-up and takes the median of three retained runs.
No other render/compiler jobs were running during the benchmark; Gaffer was
open on the interactive desktop. Wall time includes process startup, rendering,
spill, reconstruction and EXR output. This is not production throughput.

| Case | Deep disabled (s) | Deep enabled (s) |
| --- | ---: | ---: |
| Volume miss | 0.393 | 0.769 |
| Homogeneous slab | 0.438 | 0.920 |
| Overlapping media | 0.445 | 0.921 |
| Transparent surface in fog | 0.414 | 0.876 |

Each enabled run has 96 batches/waits and 9,240,576 readback bytes. Event/metadata
staging is 96,256 bytes per side, plus 262,144 bytes of medium storage per side.
Tracked device allocator peaks are about 750–777 MB, including beauty resources;
this is not resident VRAM measured by the driver. The combined capture/readback
and spill counters remain available in `benchmark/report.json`. They do not
isolate GPU execution from already queued beauty work. Larger production scenes,
continuation scheduling and heterogeneous volume integration remain unqualified.
