# Deep motion blur (phase 4)

Status: **M-a implemented and validated** (2026-11-23); M-b and M-c planned.
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
beauty alpha unbiased, byte identity, CUDA beauty policy 7/7. One OptiX beauty
policy miss on the armature fixture's original render (2 fallback pixels; all
10 fresh candidates clean, OptiX deep-off renders themselves nondeterministic) is
undecided: diagnose or waive.

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

## M-c: volume transform motion

Replace static `worldtocamera`, object inverses and boundary vertices in the
deep volume path and CPU oracle with values interpolated at `ray.time`. Audit
the error-budget proofs (straight-line depth map per ray at a fixed time). Use
the independent CPU shader oracle per ray time, `tools/deep_boundary_cases.json`
motion cases and `validate_volume_camera_curves.py`.
