# M8: analytic reference and homogeneous absorption capture

M8c adds CUDA capture with shared CPU/GPU traversal and bounded device medium
storage. See [CUDA_VOLUME_VALIDATION.md](CUDA_VOLUME_VALIDATION.md) for current
support and validation. The M8a/M8b sections below retain their historical scope.

## Scope

M8 is in progress. This first sub-gate implements and checks extinction interval
math and compositor behavior before connecting volume capture to Cycles.
M8b now connects the restricted CPU absorption path described below. No VDB, arbitrary
heterogeneous shader, deep RGB, in-scattering, or GPU volume support is claimed.
Existing surface capture and its production writer remain available.

`volume.h/.cpp` accept complete weighted camera samples with local surface
opacity and piecewise homogeneous extinction intervals. Each interval contains
positive camera axial front/back depths and integrated optical depth
`sigma_t * physical_ray_length`. Using axial length instead would understate
off-axis attenuation. Camera-inside input begins at the positive near clip.
Overlapping media add optical depth; accepted empty rays contribute weight.

Reconstruction averages transmittance across rays before fitting ordered,
nonoverlapping exponential intervals. It splits at all medium and surface
boundaries. A surface contributes a zero-length opacity step. The log of a
mixture of exponentials has curvature equal to the weighted variance of its
extinction rates. Bounding that variance by one quarter of the squared rate
range gives a conservative bound throughout each fitted interval. Subdivision
continues until absolute transmittance error is at most 2e-7. This does not
accumulate per interval: each endpoint is fitted to the original average.

This is a correctness reference, not a production fitting algorithm. The
partial-coverage fixture uses 1,024 intervals; mixed extinction uses 1,033.
The default 65,536 output-interval cap and exhausted floating-point precision
fail explicitly. Opaque positive-length intervals are rejected because their
interior extinction cannot be recovered from alpha 1.

The reference writer emits FLOAT Z/ZBack/A and reserves 8e-7 for quantization.
It compares both sides of all original/quantized boundaries plus any interior
stationary point of their exponential difference. Collapsed FLOAT intervals
are rejected. Atomic publication preserves the destination on failure. This
whole-image fixture API is separate from bounded renderer export; it does not
extend the surface spill format or its memory guarantees.

## Verification

Build/test commands from repository root:

```powershell
cmake --build build-m6 --target cycles_deep_volume_test cycles_deep_exr_test cycles_deep_production_test --config Release --parallel 2 -- /verbosity:quiet
ctest --test-dir build-m6 -C Release --output-on-failure
.\build-m6\bin\Release\cycles_deep_volume_test.exe build-m6/m8-volume-reference
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_volume_gaffer.py build-m6/m8-volume-reference
```

All six CTest groups pass, including the unchanged capture, surface reference,
production publication/reduction, EXR round-trip and version tests. Volume tests
cover Beer-Lambert attenuation, exponential splitting, interior curve errors,
25 randomized four-ray ledgers, incomplete/duplicate identities, invalid
extinction, fitting limits, and failed-publication preservation.

Ten analytic EXRs: homogeneous, off-axis, camera-inside, overlapping media,
partial coverage, crossing transparent surface, opaque surface, mixed
extinction, piecewise density, and empty. Piecewise density is supplied analytic
data, not validation of a heterogeneous renderer sampler.

Gaffer 1.7.2.0 DeepSlice reads and splits all ten fixtures. Maximum measured
alpha error is **4.7404541236861775e-7**, below **1e-6**. The independent
160x120 analytic two-sphere scene passes five depth cuts across every pixel:
maximum error **1.5214900250803964e-7**. Its physical ray chords determine optical
depth, including overlap. The spheres are an analytic reference, not a Cycles
render. Results are in `build-m6/m8-volume-reference/gaffer_validation.json`.

## Gaffer review

Open `build-m6/m8-volume-reference/m8_volume_reference.gfr`.

- `VolumeIntervalPoints`: combined live DeepToPointCloud outputs; blue is Z,
  orange is ZBack. These are interval boundaries, not scattering particles.
- `FogDepthCut.farClip.value`: changes the image depth cut and both point clouds.
- `FullFogAlpha`: flattened full opacity; inspect the alpha channel.
- `FogDepthCut`: flattened partial-depth opacity; inspect alpha.

The graph saves/reloads and both clouds evaluate. At the saved generation cut
5.5 there are 7,008 points per cloud. The review can be adjusted interactively.

## M8b: actual CPU renderer capture

`--deep-volume` enables native SVM CPU capture of constant scalar
`absorption_volume` materials on closed convex polygon meshes. Volume boundaries
must have one material, outward winding and no surface or displacement output.
Preflight verifies manifold edges and convexity. Static nonsingular object
transforms without reflection are allowed. The qualified test set uses boxes.
The camera must be static perspective/pinhole with positive near clip and fixed
sampling. Ordinary allowed surface materials may cross the medium.

The independent traversal copies the accepted camera state and uses native
intersections and the shared volume shader's extinction value. It records
`sigma_t * physical_length`, not random scattering collision locations. It
traces beyond the far clip to find exits of media that contain a clipping plane;
only in-clip intervals are recorded. A first back-facing crossing identifies
camera/near-clip-inside media. Incomplete chains fail export. Beauty state and
random sampling are not modified. Shared-edge duplicates use the M7 topology
and orientation check.

Limits: 128 intersection attempts per camera ray, up to 64 tracked objects,
`--deep-max-events` records (default 16, maximum 64), and 2,048 reconstructed
intervals per pixel. Exceeding a limit fails rather than publishing a truncated
chain. Overlap extinction is summed in the reference reconstruction, after
accounting for each ray's surfaces and accepted misses. Captured triples use
front/back/optical-depth; zero-length records carry surface alpha.

The existing fixed-offset temporary-file ledger now supports these triples.
The volume writer reconstructs one pixel at a time and serializes one scanline;
the conservative deep working-memory preflight includes the maximum output
interval count. It remains a deep-buffer budget, not a total process memory cap.
Surface reduction is rejected in volume mode. Cancellation or errors preserve
the previous final EXR. The raw CSV uses explicit `volume`, `surface` and `miss`
record kinds and complete camera-sample identities.

### Render evidence

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_volume_capture_gaffer.py install-m6/cycles.exe build-m6/m8-cpu-volume
```

Nine actual Cycles render cases: homogeneous slab, camera inside, near clip
inside, far clip inside, overlapping media, opaque surface inside fog, partial
coverage, zero extinction, and the 160x120 four-sample review with two overlapping
boxes. All pixels are checked at eleven depth cuts against complete raw ledgers.
Maximum Gaffer/ledger alpha error: **4.0410239277033355e-7**, below **1e-6**.
Deep-on/off RGBA beauty is exactly equal for every case.

An independent ray/box Beer-Lambert oracle also checks camera sample zero.
Its maximum error is **1.2843861837419368e-6**, below the separate **2e-6**
geometry/intersection gate. The basic slab cases are below 1e-7. The oracle
accounts for the baseline's 1024-entry symmetric box filter: input 0.5 maps
to raster offset `0.5 + 1/(4*511)`, not exactly the pixel centre. This existing
sampling behavior was retained, not changed for deep output.

Twelve failure cases preserve an existing final file: colored extinction,
negative/NaN density, DOF, adaptive sampling, motion, heterogeneous shader,
open mesh, OSL volume, surface reduction, insufficient memory and record
capacity overflow. CUDA volume capture is not implemented; CUDA was unavailable
to the direct validation process, so no runtime CUDA rejection result is claimed.

All six CTest groups pass, including added in-memory/disk volume ledger,
duplicate/completeness and scanline/final-publication cancellation checks.
The nine-fixture CPU native/OSL surface regression also passes, with eight
surface rejection cases. No current CUDA performance or regression claim is
made by these CPU tests.

### Actual-render Gaffer review

Open `build-m6/m8-cpu-volume/m8_cpu_volume_review.gfr`.

- `CyclesVolumeDeep` reads the actual Cycles EXR, not an analytic fixture.
- `VolumePoints` combines live front (blue) and back (orange) point clouds.
- `VolumeDepthCut.farClip.value` slices both clouds and the image.
- `CutAlpha` shows flattened opacity; inspect alpha. `CyclesBeauty` is the
  matching ordinary render.

The graph saves/reloads and both point clouds evaluate. Points show interval
boundaries, not scattering particles or meshes converted to points.

## Remaining M8 work

M8d1 now qualifies known linear-density reference integration and a Gaffer
analytic sphere review; see [evidence](DENSITY_REFERENCE_VALIDATION.md).
This does not enable native heterogeneous or VDB capture.

1. Broaden CUDA volume performance qualification beyond the small acceptance fixtures.
2. Broaden geometry and camera/sampling support with separate acceptance cases.
3. Add controlled heterogeneous integration with measured interior error before
   making VDB claims. Define color/scattering scope separately.

M6 performance qualification and M7 native CPU/CUDA edge precision remain
separate open items.
