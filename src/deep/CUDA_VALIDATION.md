# M6 — first GPU backend (CUDA)

The first measured backend targets NVIDIA CUDA on the local GeForce RTX 3080
(compute capability 8.6). CUDA 12.8.93 is installed in `build-cuda-toolkit/`
from NVIDIA redistribution archives with SHA-256 verification. It is a
workspace-local toolchain; the system driver is unchanged.

## Implementation

- CUDA launches a separate `deep_surface` kernel immediately after camera
  initialization. It owns one deterministic output slot per scheduled camera
  lane and performs a straight visibility traversal. Beauty path state is
  restored after the traversal; beauty kernels and RNG are not used for the
  deep result.
- Results are copied back in batches of 512 records (270,336 bytes per batch
  at the 64-event limit) and drained into the M5 disk-backed capture. There is
  no device-side append counter or truncation path. Incomplete chains, cache
  failures, unsupported primitives and cancellation fail the frame.
- CUDA M6 accepts native SVM materials only. GPU OSL, adaptive sampling,
  motion blur, DOF, volumes and other deferred features remain unsupported.

## Evidence

`install-m6/cycles.exe --list-devices`, launched with `build-m6/run-cuda.cmd`,
reports:

```
CUDA  NVIDIA GeForce RTX 3080
CPU   AMD Ryzen 9 5900X 12-Core Processor
```

The first runtime kernel compile produced a cached `sm_86` cubin in
`install-m6/cache/kernels/` (about 21.7 MB). A 16x12, four-sample, 64-event
deep render of the 64-layer fixture completed and published beauty, CSV and
Deep EXR. CUDA capture logs reported 768 records total across its render
iterations, with kernel, transfer and spill timings. A four-event run failed
cleanly on the intentional traversal limit and did not publish a partial deep
frame.

The 16x12, four-sample CPU/CUDA pair has 768/768 complete camera identities,
zero chain-length mismatches, maximum depth difference `1.9073486328125e-6`,
and zero local-alpha difference. Different beauty file hashes do not establish
pixel differences or their cause; M6 compares decoded pixel values. The four-event CUDA run exits
nonzero without publication, and CUDA+OSL exits nonzero with the documented
CPU-only OSL diagnostic.

The Gaffer parity script is [validate_cuda_gaffer.py](validate_cuda_gaffer.py).
The final host build/install and expanded Gaffer suite passed on 2026-09-17.
Earlier escalation attempts had been rejected by automatic approval review
because of an account usage limit; that no longer blocked this validation run.
All five CTest groups pass. `build-m6/acceptance/report.json` records ten scenes:
stack, diffuse, opaque back, miss, clear, cutoff, beauty bounce limit, opaque
capture, checker cutout and image texture. Each compares all 3,072 camera sample
identities across CPU/CUDA, CUDA export against its raw transmittance ledger on
both sides of every boundary, and CUDA deep-on/off beauty pixels.

Maximum depth difference is `1.9073486328125e-6`; local-alpha difference is
`5.960464477539063e-8`; export curve error is `2.60770320892334e-8`.
Deep-on/off beauty maximum is `3.725290298461914e-9`. These tiny fixtures are
correctness tests, not throughput benchmarks. Event overflow (including fully
clear surfaces), colored transparency and GPU OSL reject safely and preserve
an existing deep destination. Exactly-full two-event capture succeeds.

## Reproduce

```powershell
cmake -S . -B build-m6 -G 'Visual Studio 17 2022' -A x64 `
  -DWITH_CYCLES_DEVICE_CUDA=ON -DWITH_CYCLES_DEVICE_OPTIX=OFF `
  -DWITH_CYCLES_DEVICE_HIP=OFF -DWITH_CYCLES_OSL=ON `
  -DWITH_CYCLES_DEEP_OPAQUE=ON -DWITH_CYCLES_DEEP_TESTS=ON
cmake --build build-m6 --target install --config Release --parallel 2
build-m6/run-cuda.cmd install-m6/cycles.exe --list-devices
```

The install target now installs `source/kernel/deep` automatically. Run acceptance
in the MSVC/CUDA environment:

```powershell
build-m6/run-cuda.cmd build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_cuda_gaffer.py install-m6/cycles.exe build-m6/acceptance
```

Timing fields in capture logs include host enqueue/synchronization overhead;
they are not CUDA-event device-only timings. There is no append atomic in this
adapter: each lane has a unique output slot. Each side of the staging buffer is
270,336 bytes; this is not a total-process or total-device memory bound.

## Geometry review and stress evidence

`build-m6/acceptance-layers/report.json` adds the 64-transparent-layer fixture
at 16x12, 16 samples: complete CPU/CUDA ledgers, maximum depth difference
`1.9073486328125e-6`, zero local-alpha difference, exported-curve error
`2.6132256358835093e-9`, and identical deep-on/off beauty pixels.

`create_cuda_review.py` renders the existing sphere/cube/cylinder source at
640x480, 16 samples, eight-event capacity and a 32 MiB deep working-buffer
budget. `build-m6/primitives-review/m6_review.gfr` contains CPU and CUDA
DeepToPointCloud nodes (one million displayed points each), beauty readers,
flattened alpha and editable depth cuts. It was saved and reloaded successfully
with Gaffer. Eight depth cuts differ by at most `5.960464477539063e-8` alpha;
no pixel exceeds `1e-6`. CUDA recorded all 4,915,200 camera identities.

CPU/CUDA end-to-end times were 104.485/65.843 seconds, including export and
process overhead. Other validation/compiler work overlapped, so these are
observations, not isolated speedup benchmarks. CUDA capture stage totals were
5.292 seconds for enqueue/synchronize, 4.634 seconds readback, and 28.323
seconds spill. These show the host spill cost warrants further work.

The new review window was launched without replacing the unsaved M5 session.
Desktop capture returned black and activation reported `GetCursorPos failed:
Access is denied (0x80070005)`, so interactive framing is not verified. Select
`CUDA_DeepToPointCloud` in Gaffer and press V to review the CUDA deep samples.

## Compiled-kernel overhead

Built the same installed CUDA source with CUDA 12.8.93 / `sm_86`, using the
runtime flags but omitting `WITH_CYCLES_DEEP_OPAQUE`. Compilation succeeds.
`measure_cuda_resources.py` loads both cubins with the CUDA driver and queries
all 75 common entry points. Results: `build-m6/kernel-resources.json`.

- All 75 retain identical register counts, shared-memory sizes and driver
  occupancy recommendations. This is theoretical occupancy, not achieved
  hardware utilization or a throughput benchmark.
- `integrator_shade_surface` local memory increases from 6,800 to 6,864 bytes
  per thread in the deep-enabled binary, even with deep output disabled. The
  other 74 entry points have identical queried resource attributes. Zero
  disabled-feature cost must not be claimed.
- The separate deep kernel uses 168 registers, 6,720 local bytes and zero shared
  bytes per thread/block as reported by the driver, with a recommended block
  size of 384. Unique lane slots avoid capture append atomics.
- Cubin size increases from 20,226,432 to 21,684,880 bytes (+1,458,448 bytes).
  Cubin size is not measured resident GPU memory. The staging buffer is allocated
  lazily when capture is active.

## Remaining qualification

M6 correctness and the local CUDA build are validated. Before calling the entire
measurement gate complete, run isolated repeated throughput measurements,
separate reconstruction from EXR encoding/publication timings, and quantify
total GPU memory/achieved occupancy on representative scenes. The measured
64-byte surface-shader local-memory increase also needs a throughput check.
Other GPU backends were not built on this machine; they remain unsupported for
deep output and must retain their normal builds in broader build qualification.
No GPU OSL, Blender integration, or Gaffer renderer integration is claimed.
