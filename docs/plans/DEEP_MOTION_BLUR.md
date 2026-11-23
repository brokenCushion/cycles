# Deep motion blur (phase 4)

Status: **M-a, M-b and M-c implemented and validated** (M-a/M-b 2026-11-23,
M-c 2026-11-24).
Branch `blender-deep-multilayer`.

Goal: deep alpha matches a motion-blurred Cycles beauty for animated characters
with deforming meshes, then hair, then volumes. Deep keeps recording one sample
chain per camera ray at that ray's shutter time; reconstruction already treats
chains from different times as the shutter integral, so no EXR or reader change
is needed.

## Current state

- Before M-a, camera and rigid, unit-scale object motion were supported for surfaces
  (`validate_motion`, [deep.cpp](../../src/session/deep.cpp) around line 608).
- Deforming meshes are rejected twice:
  - preflight: `only static polygon meshes ...` (deep.cpp around line 690);
  - kernel: deep surface capture accepts only `PRIMITIVE_TRIANGLE`
    ([kernel_arch_impl.h](../../src/kernel/device/cpu/kernel_arch_impl.h) around
    line 174, [surface_cuda.h](../../src/kernel/deep/surface_cuda.h) around line 48),
    and shared-edge de-duplication is gated on static triangles (around lines 186 / 72).
- Intersection, shading and depth already use the ray time (Embree motion,
  `motion_triangle_intersect`, OptiX motion GAS, `motion_triangle_shader_setup`,
  `deep_camera_depth`). The block is the guards, not missing geometry support.
- Hair motion: preflight plus a ribbon type check that rejects
  `PRIMITIVE_MOTION_CURVE_RIBBON`.
- Volumes: static camera and object transforms are assumed throughout
  `src/kernel/deep/volume*.h` and the CPU oracle.

## Decisions (user, 2026-11-22)

1. Allow scaled, non-rigid object transform motion in M-a.
2. Support any motion step count; qualify Blender's default and one higher setting.
3. Fixtures: small generated Blender scenes for exact checks, then one of the
   user's animated character scenes. The standalone XML reader is not extended.
4. Order after M-a: hair motion (M-b), then volume transform motion (M-c).
   VDB velocity motion is out of scope.

Rolling shutter and non-uniform shutter curves stay rejected.

## M-a: deforming meshes and scaled transforms

Changes:

- Kernel (CPU and CUDA/OptiX): accept `(type & ~PRIMITIVE_MOTION) == PRIMITIVE_TRIANGLE`;
  extend `deep_same_surface_boundary` de-duplication to pairs of the same
  triangle type (it reads only `tri_vindex` and u/v).
- Preflight: allow mesh motion blur; keep render-time subdivision rejected.
  Relax `validate_motion` to finite, non-degenerate transforms (positive
  determinant); keep reflection rejected. Surfaces take depth from the ray, so
  scale needs no depth change. Volumes keep the rigid rule until M-c.
- Scanner: `tools/scan_blender_deep_scene.py` mirrors the new rules.
- Docs: SUPPORT.md, BLENDER.md rejection table.

Validation:

- Blender fixtures (CPU first, then CUDA/OptiX): translating deform, deform that
  matches camera motion (depth invariant), crossing deforming sheets, shared-edge
  fold under deformation (no double count), scaled animated parent, default and
  higher motion steps.
- Gates: flattened curve vs raw ledger < 1e-6; CPU beauty deep on/off exact;
  flattened deep alpha vs beauty alpha with the allowlist oracle; same-build
  strict byte identity; CUDA/OptiX raw beauty policy for the GPU kernel change;
  cross-backend flattened alpha within 1e-4.
- Regression: existing rigid-motion suites (`validate_motion_gaffer.py`,
  `validate_motion_cuts_gaffer.py`, `validate_motion_beauty_gaffer.py`) and the
  golden references stay unchanged.
- Production: one user character scene, flat-vs-deep alpha and Gaffer DeepRecolor,
  as in the junkshop check ([BLENDER.md](../deep/BLENDER.md#verified-production-render)).

Risks: near-coincident hits on deforming shared edges; CPU vs OptiX edge hits
with more than two motion steps; shaders reading motion attributes.

### M-a results (2026-11-23)

Implemented (uncommitted at time of writing). Blender fixtures (shape key, shape
key with 3 steps, armature, scale, crossing, fold, parented armature) pass on
CPU, CUDA and OptiX: ledger under 1e-6, CPU beauty deep on/off exact, deep vs
beauty alpha unbiased, byte identity, CUDA beauty policy 7/7. OptiX beauty
recalibration (120 seed-123 controls, leave-one-out limit 1): crossing and
shapekey pass 11/11. Armature's original deep-on render had 2 fallback pixels;
candidates 0-9 had 0. An accumulation-trace diagnostic
(`D:\temp\cycles_deep_motion\diag_armature`, 8 deep-on and 8 deep-off renders)
showed bit-identical bounce-0 intersections (315/315) and bit-identical
per-sample contribution multisets on every pass between deep-on and deep-off.
The deviation (Denoising Albedo.B 5-6 ULP) is the order of atomic float adds
into the render buffer, which also varies run to run with deep off (up to 11 ULP).
Deep capture's extra queue synchronisation may shift the distribution of that
order but changes no summed value. Not a deep bug; no renderer change. Resolved
under the VALIDATION.md step 4 surface addendum (approved 2026-11-23).

Character stand-in (`D:\temp\cycles_deep_motion\character`, `camfix`): 98k-triangle
skinned body with walk cycle and shape keys, a folding semi-transparent cape, a
flag under an animated non-uniformly scaled parent, animated dolly camera. CPU
480x270 at steps 1 and 3 and OptiX 960x540 frames 9 and 22 pass; cross-backend
differences are single-sample edge flips; Gaffer DeepRecolor matches except where
the beauty's own alpha is noisy over semi-transparent surfaces (deep stores the
exact coverage).

Camera fix (2026-11-23): Blender cameras failed preflight because
`blender_camera_matrix` returns `tfm * scale(1,1,-1)` (determinant -1) and the
rigid rule required a positive determinant (a bug predating M-a). The rule now
keeps each step's handedness equal to the static matrix's and still requires
finite, unit, orthogonal axes. `transform_motion_decompose` stores a reflection
as scale -1 and slerps the rotation; the camera ray and `deep_camera_depth`
share the interpolated matrix, so depth is exact. Verified by matched-camera
invariance (max 8.6e-6 CPU, 4.8e-6 CUDA, 2.05e-5 OptiX at 3 steps, the OptiX
residual likely from its SRT object interpolation), analytic dolly ranges,
standalone reflected `<transform>` fixtures and the character scene with the
real dolly. Volume depth still uses the static `worldtocamera`, so Deep Volume
keeps motion blur rejected; M-c must interpolate the camera at `ray.time` in
`volume.h`, `volume_native.h` and the CPU oracle (`deep_volume_oracle`).

## M-b: hair motion

Accept `PRIMITIVE_MOTION_CURVE_RIBBON`; split camera motion from hair geometry
motion in preflight; keep Ribbon only, tolerance 0, capacities 16/64. Fixtures:
moving ribbons and camera-only motion over static hair. Same gates; a native
backend waiver may be needed for cross-backend ribbon edges.

### M-b results (2026-11-23)

Implemented: motion ribbons accepted in CPU/CUDA/OptiX capture (the old ribbon
check let only static ribbons through); preflight drops the hair motion rule and
gives hair objects the surface motion rule; scanner and tests mirror it. 13
positive and 6 negative Blender fixtures (`D:\temp\cycles_deep_motion\hair`) pass
on CPU/CUDA/OptiX: ledger < 1e-6, CPU deep on/off exact, unbiased alpha, byte
identity, exact ribbon depth under motion and deformation, analytic dolly depth.
Raw beauty policy: CUDA 13/13; OptiX 12/13, one Denoising Albedo.B pixel (20 ULP)
in curves_deform. Repeats show deep-off varying symmetrically (655 vs 677 pixels,
up to 23 ULP) and the deep-on repeat equal to deep-off, the same signature as M-a's
armature; resolved by the step 4 surface accumulation trace (identical
intersections and contributions in 8+8 renders, deep-off order spread 14 ULP).
Cross-backend ribbon edges use the native waiver in SUPPORT.md: differences occur
with motion blur off and match pristine beauty alpha (correlation 0.8-0.98).
Static Ribbon suite byte-identical to M-a on all backends. The character with Fur
visible passes CPU steps 1/3, a moving-fur variant and OptiX frames 9/22 with the
real dolly.

Known pre-existing drift (not motion): with strict deep, the scanner treats hair
tolerance as 0 while preflight checks the raw layer tolerance, so strict + Custom
layer tolerance > 0 + hair is rejected at render time but passes the scanner.

## M-c: volume transform motion

Replace static `worldtocamera`, object inverses and boundary vertices in the
deep volume path and CPU oracle with values interpolated at `ray.time`. Audit
the error-budget proofs (straight-line depth map per ray at a fixed time). Use
the independent CPU shader oracle per ray time, `tools/deep_boundary_cases.json`
motion cases and `validate_volume_camera_curves.py`.

### M-c results (2026-11-24)

Implemented: `deep_camera_depth_row(cam, motion, time)` gives the depth row of the
same interpolated camera `deep_camera_depth` uses (unchanged when static); the
homogeneous, native NanoVDB and shader-evaluated volume paths and the CPU oracle
use it plus the ray-time object inverse. Preflight drops the Deep Volume motion
rejection; the camera keeps the rigid/handedness rule; volume objects take the
object rule (scale/shear allowed, no reflection); deforming volume boundaries and
VDB velocity-grid blur are rejected. Shader-evaluated grids under motion use the
smallest voxel edge over the shutter. Scanner mirrors it (`volume_deform`,
`volume_velocity`); drift suite 162 cases, 0 false positives/negatives.

Error budgets: every proof is per ray, and each ray's transforms are fixed at its
time, so the affine depth map, straight object-space ray, cubic-per-cell grid
bound, telescoped products and cutoff clamp still hold; the pixel average is a
convex mix of per-ray curves. No budget change.

Gates (`D:\temp\cycles_deep_motion\volume`): 21 positive + 4 negative Blender
fixtures pass on CPU/CUDA/OptiX (deep vs raw curve ≤ 0.272 × bound, ledger
≤ 1.2e-8, unbiased alpha, CPU beauty exact, byte identity); ray-time shader
oracle ≤ 7.9e-5 (h/64), native grids ≤ 1.95e-7; time-aware geometry oracle depth
≤ 4.8e-5; matched camera+volume vs static rig ≤ 1.7e-5 with 0 count differences;
boundary matched-motion cases 10/10; cross-backend flat alpha 42/42 ≤ 1.8e-6;
static suites identical to M-b (deep 125/125, CPU beauty 53/53, static hair
41/41); M-a/M-b suites pass; CUDA 75/75 common kernel resources unchanged.
Production (960x540, 64 spp, moving VDB plume, moving camera, mist, moving sign):
CPU and OptiX bias 1.86e-6 ± 4.47e-6, CPU–OptiX p99 2.2e-7, Gaffer DeepRecolor
RGB p99 2.7e-3, 190 MB at error 1e-3.

GPU raw beauty: CUDA 20/21 and OptiX 20/21 calibrated. The two M-c GPU raw-beauty
flags (CUDA `vdb_static` (31,18), OptiX `vdb_scale_eval` (34,21), Combined.A)
were resolved on 2026-11-24 by the volume majorant-snapshot diagnostic: deep-on
and deep-off agree, bit-identically in the required six renders. On CUDA the
flagged alternate value also appears in snapshot deep-off renders, so it comes
from majorant/accumulation order and not from deep capture (see VALIDATION.md
step 4).

Found during M-c (pre-existing, static):
- Scaled native VDBs with Volume Space = Object (Blender's default) get the wrong
  deep density: Cycles multiplies by `object_volume_density`, the deep grid path
  does not (a 0.00375-scaled VDB gave τ 267× too low). Workaround: Space = World.
- A ground plane coplanar with a mist slab's bottom face gives deep alpha above
  beauty (mean +0.137).
- Strict mode can overflow the volume reconstruction interval budget with
  fast-moving homogeneous boxes (explicit failure).
- One unexplained OptiX "Illegal address (deep_surface)" while ~4 GPU jobs shared
  the card; not reproduced alone.
