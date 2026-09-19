# Native VDB capture implementation and qualification

## Current result

Live Gaffer review is now verified (2026-09-20): `final-cpu/native_vdb_review.gfr`
displays the deep-derived cyan plume through VDBDeepPoints. Earlier desktop
access/presentation pending notes below are historical and resolved.

The supplied VDB now renders through custom Blender/Cycles CPU and CUDA paths
at 256x256, one sample. Final outputs under `builds/validation/native-vdb/`:

- `final-cpu/scene.deep.exr`: 112,780,431 bytes, 10.0517 seconds; SHA256
  `69d2449d9ceb61fe8723ea7b0aad11b88d7b83c8a7b69d554c988cb591c4b431`.
- `final-cuda/scene.deep.exr`: 112,787,785 bytes, 61.1818 seconds; SHA256
  `1ba2992de0c6668a4a4788269850ab7c8fea7b7c86d4a56e0e2d0dbbaddeacad`.

These timings are qualification runs, not a controlled backend speed benchmark.
Every beauty RGBA pixel matches its device's deep-off reference. Gaffer validates
five depth cuts over 81 diagnostic pixels against stored interval curves (maximum
error CPU 2.4614e-7, CUDA 2.7566e-7). Each `native_vdb_review.gfr` reloads with one
million deep-derived points. CPU/CUDA comparison at 45,004 diagnostic boundary/
midpoint probes has maximum transmittance difference 5.8071e-7 (limit 1e-6).

An actual native render with doubled object scale/camera translation and half
extinction matches all 16,308,564 original samples exactly: alpha and offsets
are identical, Z/ZBack are exactly doubled. The independent OpenVDB oracle also
qualifies shared CPU/CUDA grid integration on 128 rays through 89,102 cells.

CUDA runtime compilation uses
`BLENDER_USER_RESOURCES=builds/blender/user-resources` for its writable cache.
Live Gaffer viewport verification is still pending desktop access; the latest
window activation/capture reports `foreground window did not report a process id`.
No claim of all-device support,
general volume materials, emission/scattering deep RGB, or complete production
qualification is made. Details below retain the implementation rationale and
earlier gates; this section is the current outcome.

The sections below record staged implementation history. Statements about work
not yet connected describe those earlier stages; the current outcome is above.

## Implemented cell calculation (not yet connected to traversal)

`kernel/deep/density.h` now derives cubic Bernstein controls directly from the
eight cell corners and the ray segment's endpoints in cell coordinates. Convex
interpolation avoids cancellation from a power-basis expansion. It provides
clipping, exact-polynomial integration, and a convex-hull bound on the optical
depth difference from a constant-extinction interval. For nonnegative extinction,
this also bounds absolute transmittance error in real arithmetic. Floating-point
and final EXR error still need separate allowances in the complete path.

The shared functions have no allocation, recursion or shader dependency and use
Cycles' device/address-space qualifiers. CPU tests check 500 cells against direct
corner interpolation and independent two-point Gaussian quadrature, including a
nonzero interior feature with zero endpoint densities. A native CUDA executable
checks 512 cells at 101 probes each using the same kernel header and CUDA
compatibility definitions. Maximum checked oracle error is 1.06581e-14.
All nine CTest groups pass. Logs live in `builds/validation/native-vdb/`.

CUDA reproduction (from a configured VS/CUDA environment):

```text
nvcc -std=c++17 -I src src/deep/density_cell_cuda_test.cu -o builds/validation/native-vdb/density_cell_cuda_test.exe
builds/validation/native-vdb/density_cell_cuda_test.exe
```

This proves cell arithmetic on bounded test cases, not grid traversal, transform
handling, renderer integration or the supplied asset's deep output.

## Implemented grid cursor and native accessor

`kernel/deep/grid.h` walks integer interpolation-cell boundaries while retaining
the original ray parameter. It handles negative grid coordinates, exact boundary
starts and tied axes, recomputes boundary times instead of accumulating deltas,
and distinguishes completion, invalid precision/state and exhausted traversal
capacity. State is fixed size and allocation-free.

CPU tests compare 500 rays with independent integer-plane enumeration and
integrate an isolated one-voxel tent in both ray directions. CUDA hardware tests
compare the visited cell sequence against the same independent plane oracle for
512 rays, including exact corner ties and stationary axes. Both pass; logs:
`grid-test.log` and `grid-cuda-test.log` in `builds/validation/native-vdb/`.

`kernel/deep/volume_grid.h` now reads native NanoVDB corners and produces bounded
FLOAT density coefficients. FLOAT, Fp16 and FpN accessor instantiations compile on
CUDA; actual-asset runtime qualification below currently covers FLOAT only.

The next integration work is object/grid transforms and renderer capture storage
for the resulting cubic cells. Keep the existing surface
event format efficient: a separate, bounded density-coefficient buffer can carry
four coefficients per native volume cell, while compact spill appends coefficients
only for those cells. Host preflight must account for CPU worker scratch and GPU
batch buffers, and batch size must shrink as per-ray capacity grows. This storage
extension now exists in host Capture behind an explicit volume-grid mode.
`KernelDeepDensity` now defines a 32-byte companion layout (four FLOAT controls
plus double front/back depths), while the original
surface event remains 20 bytes. Compact spill stores the events followed by
coefficients only for cubic events. In-memory storage budgets the companion
array; spill preflight also budgets expanded per-sample integration intervals.
The grid mode accepts up to 4096 raw events; existing modes retain 64. Renderer
worker/device buffers and shader preflight are not wired to this mode yet.
Actual-grid measurements required up to 11,169 fitted intervals from 807 occupied
cell records across the 128 qualification rays. Grid capture therefore has a
separately budgeted 16,384 reconstruction limit; other modes retain 2048.
Capacity exhaustion remains explicit. This is measured headroom for those rays,
not a guarantee that every camera configuration fits.

Capture tests compare memory and spill across more than one event-cache working
set, mixed surface/cubic records, 65-event samples, duplicate detection and invalid
coefficients. Varying-density tests reconstruct both storage modes against an
independent polynomial integral and append after a cached partial-page read.
Nine regression groups passed after storage integration; affected capture and
publication tests passed again after retaining the allocation-free surface read
path. No native VDB deep EXR has been rendered yet.

## Actual supplied-grid qualification

The shared `deep_volume_grid_capture` now emits ordered records into externally
allocated buffers, supports strided CUDA lanes and skips empty cells. CPU and
CUDA checks on the actual grid verify unchanged integrated extinction; CUDA also
checks depth ordering and a deliberately exhausted output buffer. Both pass the
128-ray oracle at maximum transmittance error 2.6756429294394479e-9. Logs:
`native-capture-test.log` and `native-capture-cuda-test.log`. The depth mapping in
these tests is synthetic affine depth (1 + 100*t), not a renderer camera. Native
shader metadata, object/grid transforms and renderer invocation remain to wire.

Host `integrate_cubic_density` now converts the length-weighted Bernstein
records into constant-extinction intervals. It uses the shared primitive chord
bound, divides the tolerance by maximum simultaneous cell overlap, and counts
output before allocating it. A fixed depth-first stack bounds subdivision;
capacity and depth-precision exhaustion throw. Independent polynomial-oracle
checks cover interior density, overlapping and adjacent cells, total extinction,
empty cells, negative coefficients and capacity failure. This is not yet wired
to Capture or a renderer output; FLOAT export has a separate error budget.

The native test reads `firePlume_0000.vdb`, converts its `density` grid through
Cycles' `openvdb_to_nanovdb` utility at full precision, and traverses it through
the kernel's `CachedReadAccessor<float>`. The independent oracle enumerates
integer planes and uses OpenVDB BoxSampler with two-point Gaussian quadrature.

CPU and CUDA hardware each pass 128 rays through 89,102 interpolation cells;
125 rays intersect nonzero density. Maximum endpoint-transmittance error against
the OpenVDB oracle is 2.6756429294394479e-9 (gate 1e-7), including stored FLOAT
coefficient rounding. Logs: `native-grid-test.log`, `native-grid-cuda-test.log`.
All nine CTest groups also pass (`accessor-ctest.log`, 10.92 seconds).

These are index-space grid tests, not the final camera/object transforms,
full-frame capture, EXR publication or Gaffer depth-cut qualification.

Reproduce from the workspace root with the configured VS/CUDA environment:

```text
cmake --build builds/build-m6 --target cycles_deep_vdb_grid_test --config Release
builds/build-m6/bin/Release/cycles_deep_vdb_grid_test.exe test-assets/vdb/firePlume_0000.vdb builds/validation/native-vdb/grid-qualification
nvcc -std=c++17 -I src src/deep/vdb_grid_cuda_test.cu -o builds/validation/native-vdb/vdb_grid_cuda_test.exe
builds/validation/native-vdb/vdb_grid_cuda_test.exe builds/validation/native-vdb/grid-qualification/density.float.nanovdb builds/validation/native-vdb/grid-qualification/rays.csv
```

The Windows test build stages its OpenVDB and TBB runtime libraries. The initial
missing-DLL launch issue is fixed. Generated raw grids and oracle files remain
under ignored `builds/`; the supplied asset is unchanged.

The Blender scene helper now explicitly sets FULL grid precision, matching the
qualified data. Updated scene and successful beauty reference are under
`builds/validation/native-vdb/full-precision/`; the earlier fixture used Blender's
default precision and should not be used for the final paired deep comparison.

## Native integration points inspected

Native runtime adapter `kernel/deep/volume_native.h` now resolves the standard
density voxel attribute and FLOAT NanoVDB texture, applies native inverse object
and grid transforms in double arithmetic, and computes axial depth from the
static camera matrix while preserving world physical length. It rejects motion,
cubic interpolation and unqualified grid formats. This path is not runtime
qualified yet. `deep_volume` dispatches through a new shader metadata field
(`deep_density_scale`, using existing KernelShader padding), default -1/disabled.
Host shader preflight must populate/tag that field before any renderer can
enable native grid capture. The initial CPU compile exposed/fixed scalar `floor`
namespace lookup in the grid cursor; the CPU kernel rebuild completed with exit
0. CPU worker buffers are now preallocated at execution initialization and
deducted from the memory budget. The optional coefficient pointer passes through
all CPU architecture interfaces. CUDA now allocates a companion device buffer,
caps grid batches at 8192 total event slots, downloads coefficients and forwards
contiguous sample records to Capture. No GPU-thread allocation was added.
Host preflight now derives volume_grid from native Volume geometry and validates
one scalar absorption material driven by density Fac, with finite constant
unclamped multiplication only. It populates/tags shader metadata, rejects other
nodes/interpolation, and retains the homogeneous mesh checks. Grid capture uses
4096 raw slots without mutating the user's surface event limit. Blender's adapter
recipe and current overlay now expose use_deep_volume; the render helper accepts
--deep-volume alongside --deep. All changed core source files were copied to the
existing overlay (without rerunning the destructive clean-overlay preparation).

The standalone full link initially failed because dumpbin confirmed the default
CPU object still exported the old function signature while AVX2 exported the new
one. Its source timestamp was refreshed to force recompilation. Standalone
install build and Blender INSTALL build are currently pending. Runtime shader,
camera-transform and deep output validation remain outstanding. First render
should use the full-precision VDB fixture at reduced percentage, one sample,
--deep --deep-volume --deep-memory-mb 1024, then scale to the complete fixture.

The installed native Blender produced a 10x10 deep probe successfully (161125
bytes). A 256x256 attempt reached native capture but failed DEEP_ERROR_DEPTH on
thin occupied cells whose bounds collapsed when quantized early. Capture now
retains double cell depths in the companion record, budgets its larger size,
and halves CUDA grid batch slots. At final export, collapsed intervals may
become steps only when the complete-curve FLOAT error test passes. Significant
unresolved opacity still fails and preserves prior output. Memory/spill tests
verify exact thin depths; export tests cover low-opacity acceptance and high-
opacity rejection. Nine tests pass in 1.00 seconds; actual CUDA VDB capture still
passes all 128 rays at 2.6756429294394479e-9 maximum endpoint error. Blender
INSTALL is rebuilding this precision change; full-frame retry is outstanding.

- `scene/volume.h`: `Volume` inherits `Mesh`; native volume geometry must pass
  the host preflight explicitly. The present mesh-only, closed-convex test in
  `session/deep.cpp` cannot simply be applied to a sparse volume boundary.
- `kernel/geom/volume.h`: voxel attribute descriptors identify the image texture.
  World positions undergo the object's inverse transform before lookup.
- `kernel/util/image_3d.h`: the image texture's `transform_3d` maps that position
  to grid coordinates. NanoVDB linear interpolation uses `floor(P)` and eight
  integer-coordinate grid values. There is no extra half-voxel shift in this
  path. Float, FpN and Fp16 grids have distinct native accessors.
- `kernel/deep/volume.h`: current capture integrates one constant extinction
  value across a convex medium. Reusing that evaluation for a VDB would be
  incorrect. The ray must visit every crossed interpolation cell (or skip only
  regions with a proven empty bound), including thin occupied features.

## Required implementation

1. Resolve the native density grid and scalar absorption shader through Cycles
   attributes, shader validation and image metadata. Reject unsupported shader
   operations/interpolation explicitly. Preserve object and grid transforms.
2. Traverse grid cells in ray order. Trilinear density restricted to a ray is
   cubic in each cell; use all eight cell values to integrate it and bound the
   error introduced by representing it as deep exponential intervals. Merely
   sampling cell endpoints, or using stochastic beauty samples, is insufficient.
3. Extend bounded capture to carry the resulting ray data. The current 64-event
   limit is not a practical production VDB capacity. Evaluate chunked emission
   or another budgeted representation before increasing per-lane storage;
   account for host/device buffers and fail explicitly on exhausted capacity.
   Never allocate dynamically inside a GPU thread.
4. Handle near/far clipping, camera-inside rays, overlapping media and surface
   events using the same physical-length/axial-depth convention as homogeneous
   capture. Keep beauty RNG and state unchanged.

## Acceptance evidence

- Native cell fixtures: constant density, all-axis ramps, general cubic ray,
  negative grid indices, transformed grids, cell-edge/corner ties, clipped rays,
  and a one-voxel feature missed by sparse marching.
- Compare native CPU/CUDA outputs against independently evaluated grid integrals
  and depth cuts. Exercise explicit storage/precision failures and confirm no
  completed EXR is published for failed captures.
- Load the supplied VDB directly into a native volume scene, record asset/grid
  identity and transforms, render deep plus beauty, and compare deep-on/off
  beauty. Initial asset resolution/sample count must be reported explicitly.
- Present the actual EXR in Gaffer with depth cuts and DeepToPointCloud. Preserve
  the user's existing bakery review and its unsaved Transform/PathFilter edits.

Deep RGB and scattering reconstruction remain separate from the existing scalar
visibility contract; this gate must still test the actual VDB density grid.
