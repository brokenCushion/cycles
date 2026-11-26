# Deep alpha qualification matrix

Current scope follows the [validation policy](VALIDATION.md)
and [production evidence](evidence/README.md). Phases 0-9 are accepted,
including the full-resolution OptiX landscape with IDs and all-samples/64-sample
deep capture. See the [production evidence](evidence/README.md).
Qualification covers the named fixtures and production settings; arbitrary
feature combinations are not implied.

Geometry and object preflight checks apply only to geometry reachable by camera
rays. Indirect-only and camera-invisible objects may use unsupported geometry or
surface shaders; shared geometry is still checked if any camera-visible object
uses it, including holdouts. Global settings, volume detection and manifests are
unchanged.

| Feature | Qualified scope |
| --- | --- |
| Devices | Single CPU/CUDA/OptiX with SVM; CPU/OptiX OSL scalar surfaces and named volume fixtures, background render. CUDA OSL is unsupported. Explicit SVM shader-volume evaluation passes on CPU/CUDA/OptiX. |
| Shading | Native SVM scalar camera opacity/extinction; OSL constant/texture/noise/camera-query surfaces and Script/native-node materials, within preflight. Shader volumes cover constant, textured and grid-reading extinction with stated adaptive stepping error. OSL trace/@ao/@bevel, unsafe ray/path/unknown queries, dynamic textures and unprovable closure capacity fail explicitly. Runtime rejects coloured/invalid extinction and cache misses. |
| Geometry | Polygon surfaces/rigid instances; homogeneous boundaries and native scalar NanoVDB density with linear interpolation. Volume objects may move during the shutter with finite, non-degenerate, unreflected transform motion including scale/shear; boundaries are traversed in object space at each ray's time (M-c). Deforming volume boundaries and VDB velocity motion are rejected. Scaled native grids require Volume Space = World (the Object-space density factor is not applied; known issue). |
| Hair | CPU/CUDA/OptiX: SVM Ribbon surfaces, static or moving: object transform motion (finite, non-degenerate, unreflected, scale allowed), deforming particle and Curves hair, and camera motion over hair; any motion step count, qualified at 1 and 3. Effective Deep Volume OFF and depth tolerance 0. Native transparent continuation, object-level IDs and holdouts; capacities 16/64 checked. THICK/THICK_LINEAR, OSL on hair, Hair Info, hair in volume mode, reflected moving hair and point clouds remain rejected. Ribbon edge coverage can differ between CPU (Embree) and GPU as in native Blender (see the cross-backend note below). |
| Camera | Surface-only perspective/orthographic, DOF and rigid camera motion (unit scale, no shear, the static camera's handedness at every motion step; Blender's Z-flipped camera matrix qualifies). Animated FOV, rolling shutter and non-uniform shutter curves are rejected. Deep Volume requires a mono perspective pinhole camera (no DOF, positive near clip); rigid camera motion is supported with Deep Volume (M-c). |
| Motion blur (surfaces) | Deforming meshes (armature, shape keys, modifiers) and finite, non-degenerate, unreflected object transform motion, including animated scale, at any motion step count (qualified at 1 and 3). Verified with Blender fixtures on CPU/CUDA/OptiX and a 98k-triangle deforming character stand-in. Hair motion is supported within the Hair row (M-b). Volume transform motion and camera motion over volumes (M-c) are supported on CPU/CUDA/OptiX at any step count (qualified at 1 and 3); strict mode can exceed the per-pixel reconstruction interval budget for fast-moving media, so use a numeric error. See [plan](../plans/DEEP_MOTION_BLUR.md). |
| Samples | Fixed/native adaptive accepted populations; `--deep-samples 0` uses all, positive N retains the first N without changing beauty. |
| Denoising | Native OIDN, including GPU OIDN in supplied landscape fixtures; deep remains separate from beauty. |
| IDs | Optional UINT `id`, raw name hashes and manifest; numeric error required. Strict + IDs fails preflight. |
| Holdout | Object flags and native Holdout closure preserve camera opacity; checked holdout manifest subset. Shadow catchers/caustics remain rejected. |
| Surface depth merging | Same object only, checked relative depth span; numeric default `--deep-z-tolerance 1e-4`, zero disables it, strict forces zero. |
| Output | FLOAT Z/ZBack/A, positive axial depth in scene units. Deep RGB, subpixel masks and other backends are later work. |
| Multiple Blender view layers | CPU three-layer byte identity and OptiX fixture/animation checks pass. `{view_layer}` gives separate deep EXRs; collision preflight includes disabled/Single Layer-skipped layers and ignores Windows path case. Single-layer naming is unchanged; publication is per file. Cancelling an interactive render during the second layer leaves the first layer published and no later or temporary files (junkshop, 2026-11-22). The unchanged junkshop scene renders all three layers (BG, Robots, Girl with Ribbon hair) with OpenEXR and Gaffer checks passing (junkshop5), and at full size with Gaffer DeepRecolor checks; see [production use](BLENDER.md#verified-production-render). Folded volume meshes can leave volume intervals open to the clip end ([limits](BLENDER.md#known-limits-in-production)). See [results](BLENDER_DEEP_MULTILAYER_RESULTS.md). |

## Numerical and storage contract

Strict reproduces legacy payload and deterministic headers under the
[validation policy](VALIDATION.md). Numeric error splits the user setting across device/host/publication
allowances; their sum bounds absolute transmittance error. Validators read the
effective error and sample setting from EXR headers. ID publication sums measured
per-object errors; independent exact-cubic/camera/depth-cut checks remain mandatory.
Positive z tolerance approximates the interiors of merged surface depth bands;
combined alpha and curves outside those bands retain the error checks. Use zero
for the whole-curve depth contract. Strict always uses zero.
OSL volumes, or SVM volumes explicitly selected with `--deep-volume-shader-eval`,
use deterministic adaptive midpoint step doubling. Numeric error E assigns E/2 to stepping (stated,
not proven) and E/2 to bounded reconstruction/publication. The EXR records the
method, world-unit step range, selection rule and both allowances. Shader
variation can exceed the stepping allowance; each fixture needs a 4x-finer-step
check. Grid voxel size selects the step; `--deep-volume-step` caps it and is
required without a grid. Native analytic capture remains the default for SVM.
Completed misses count in normalization; incomplete/overflowing work fails explicitly.

Device buffers are host-allocated and bounded. Spill and parallel host fitting
are separate from beauty. No GPU allocation/files/STL/exceptions. Cancellation,
capacity and I/O failures preserve the previous EXR through atomic local publication;
network/power-loss/frame-transaction durability is not qualified. Scalar alpha
does not encode arbitrary subpixel correlations or recover colour from beauty.

Capture spill goes to delete-on-close files in the spill directory: Output > Deep >
Spill Directory (Blender) or `--deep-spill-dir`, else `CYCLES_DEEP_SPILL_DIR`, else
the system temp folder. The system removes them on close, crash or kill. The append
stream is zstd-compressed in 64 KiB blocks; this is lossless and leaves deep EXR
bytes unchanged (`CYCLES_DEEP_SPILL_COMPRESSION=0` stores raw records). Compression
turns off when its roughly 430 KiB per band does not fit `deep_memory_mb/8`; the log
then says `compression=off (budget)`. The identity index is a sparse file but still
reaches width x height x samples x 24 B. Any capture error (I/O errors include errno,
bytes and file) stops the render: CPU within the current pass, CUDA/OptiX at the
next pass boundary; the previous EXR is kept. After each pass the renderer projects
the spill size; a non-adaptive render that cannot fit fails, an adaptive render gets
a warning (its sample count is only an upper bound). Any render stops when free
space, or the optional `CYCLES_DEEP_SPILL_MAX_MB` quota, drops below 256 MiB. Budget
failures print the bytes needed and the minimum `deep_memory_mb`; the export row
check runs before the first row is written. Strict surfaces remain unbounded.

Grid-volume layers above 8 samples reduce each pixel during rendering instead of at
export: the same per-ray density and one-ray fits and the same balanced combines with
allowance R/(2*levels), applied to aligned sample ranges as soon as both halves exist.
Export folds the remaining groups and runs the final fit. Strict and non-adaptive
numeric output is byte-identical to export-time reduction, whatever the arrival order.
Levels come from the sample cap; with adaptive sampling this gives smaller per-level
allowances than before, so output stays within the stated bound but is not
byte-identical to earlier builds. Intermediate storage is about popcount(n) <=
log2(samples) curves per pixel. Resident nodes use deep_memory_mb/8 (hard cap 5/4 of
it); the rest go to delete-on-close node streams in the spill directory, compacted
between passes. Duplicates, incomplete coverage, a node over the interval limit, or a
reduction ledger over its allowance fail with numbers. Per-sample diagnostic records
keep the raw spill as well. `CYCLES_DEEP_VOLUME_REDUCE=0` restores raw spill.

Compact deep output: Output > Deep > Deep Size (`--deep-size` in the standalone
renderer) chooses Exact (default; files identical to earlier builds) or Compact.
Compact stays within the file's `cycles:maxTransmittanceError` and records its choices
in `cycles:deepSize` / `cycles:deepCompact*` header attributes. (1) Same-object
hard-surface hits within the relative surface depth tolerance become one point sample
at the group's front depth, with the exact alpha product, so transmittance outside each
group is unchanged; layers following the scene use at least 1e-3, a layer's Custom value
wins (hair keeps 0). (2) Each pixel's volume curve is simplified greedily; merged
intervals keep their total optical depth so error does not build up along the ray.
Allowance 1e-4 at 1e-3, about 1e-5 at 1e-4, half that for shader-evaluated volumes,
none in strict. (3) Alpha is HALF when the per-pixel budget allows (surfaces at error
about 1e-3 or more, volumes with at least 2e-5 left), otherwise the file stays FLOAT.
(4) Z and ZBack stay FLOAT; point samples carry ZBack = Z, because Gaffer's DeepToFlat
Range mode drops a missing ZBack and Nuke's behaviour without one is unconfirmed. All
costs are measured exactly per pixel against the publication ledger; a pixel that cannot
fit fails the export with its numbers. Junkshop frame 2 full size: 3.02 GB -> 0.82 GB.

CPU beauty remains exact. CUDA/OptiX beauty policy and its GPU-only change scope
are in [VALIDATION.md](VALIDATION.md); source hashes and 75 kernel resource records
provide the host-only proof.
Strict backend equality is not promised: OptiX hardware intersections compute
surface t differently from CUDA BVH2. Across builds/toolchains/backends, both
sides qualify independently and flattened alpha differs by at most 1e-4.
A case-specific waiver applies only when pristine Blender without deep
reproduces a beauty-alpha difference of the same size AND each backend's deep
flattened alpha matches its own beauty alpha within its existing header bound.
Report both differences. Known native difference: **OSL texture-driven opacity
can differ between CPU and OptiX by up to ~1e-3, as in native Blender** (CPU
OpenImageIO textures versus OptiX Cycles images). **Ribbon hair edge coverage
can differ between CPU and CUDA/OptiX by single-sample flips at ribbon edges (up
to ~0.07 flattened alpha at 64 spp), as in native Blender beauty alpha** (Embree
flat curves versus Cycles GPU ribbon intersection; present with motion blur off;
each backend's deep matches its own beauty; M-b, 2026-11-23). All other cases retain 1e-4;
curves, depth shifts and counts are informational. Byte/curve identity applies
only within the same build and backend.

## Reproduction

Use [the single regression command](../../src/deep/README.md#regression-command): nine CTests,
CPU/CUDA matrices, boundaries, oracles, CPU beauty, resource/source proof and
81/81 strict + 30/30 numeric identity. OptiX SVM identity is 65/65 with `--optix`; the separate OSL suite checks 30
mode/backend cases, 26 atomic rejections and calibrated OptiX beauty. Gaffer is
optional for interactive review.
Large validation output/TEMP lives on D:, with only small reports under `builds/`.
Verified historical sample ZIPs stream without disk expansion. The approved
[M8 release record](M8_RELEASE_VALIDATION.md) is retained; other historical
reports have an [archive index](ARCHIVED_REPORTS.md). Current results are in
the [production evidence](evidence/README.md).

## Additional qualified SVM surface nodes

The Step 2 fixtures qualify these opaque closures on polygon meshes:
`glossy_bsdf`, `metallic_bsdf`, `refraction_bsdf`, `sheen_bsdf`, `toon_bsdf`,
`subsurface_scattering`, `hair_bsdf`, `principled_hair_bsdf`. They do not contribute
to straight camera transparency. Static CPU/CUDA/OptiX Ribbon geometry is qualified only within the hair row above.

The additional math/utility nodes are `vector_math`, `map_range`, `separate_xyz`,
`combine_xyz`, `mix_float`, `mix_vector`, `mix_vector_non_uniform`, `vector_rotate`,
`float_curve`, `vector_curves`, `normal`, `blackbody`, `wavelength`, and
`vector_transform`. Numeric conversions and opacity dependency checks still apply.

Additional surface Light Path outputs: Is Diffuse Ray, Is Glossy Ray, Is Singular
Ray, Is Reflection Ray, Is Transmission Ray, Is Volume Scatter Ray, Diffuse Depth,
Glossy Depth, Transmission Depth and Portal Depth. These retain camera-path
values after transparent continuation. Existing Is Camera Ray, Is Shadow Ray,
Ray Length and Ray Depth support is unchanged.

Transparent Depth remains rejected: the volume capture path does not advance its
counter. Ray Portal remains rejected because it redirects rays. AO/Bevel remain
allowed only outside opacity dependencies. Tier 3/4 nodes, vector_map_range,
curve geometry outside the static Ribbon scope, point-cloud geometry, adaptive subdivision, deforming motion and shadow
catchers remain outside this expansion.

CPU tests cover 32 color-only/closure byte-identity pairs and all 24 new
opacity-driving utility/Light Path cases against the raw camera-chain oracle,
with exact CPU deep-on/off RGBA. CUDA and OptiX run the same 24 oracle fixtures
and rejection checks. This does not promise deterministic equality to noisy
beauty alpha for arbitrary shader mixtures. See the
[investigation and qualification results](BLENDER_DEEP_MULTILAYER_RESULTS.md#step-2b-opacity-gate-investigation).
