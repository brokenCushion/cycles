# Rigid surface motion

Capture uses Cycles' accepted camera ray and shutter time. Z is axial depth in
the camera at that time, using the inverse interpolated camera transform.
Qualified: rigid translation/rotation of mesh instances and perspective cameras,
uniform shutter, box filter, fixed/adaptive sampling and optional DOF on CPU
native/restricted OSL and CUDA native. Deformation, motion scale/reflection,
animated FOV, rolling shutter and nonuniform shutter curves are rejected.

## Evidence

`validate_motion_gaffer.py` covers object/camera motion, matched transforms,
moving silhouettes, transparent depth-order swaps, adaptive + DOF + motion,
opaque hits, thin/disconnected surfaces and CPU OSL. Eleven CPU/CUDA fixtures
plus OSL pass. Maximum ledger/EXR error is 2.888e-8; maximum backend event-depth
difference is 2.862e-6. Matched motion checks shutter-time consistency.
Invalid scenes preserve the previous output. Artifacts:
`builds/build-m6/m7-motion-cpu` and `builds/build-m6/m7-motion-cuda`.

The 640x480, 16-sample corrected primitives evidence is in
`builds/build-m6/m7-boundary-primitives`:

- `raw_cut_validation.json`: 4,915,200 identities per backend, eight cuts;
  maximum error 5.310e-7 against each backend's ledger (limit 1e-6).
- `beauty_isolation.json`: CPU exact; CUDA maximum RGBA difference 1.193e-7.
- `target_record_comparison.json`: shared-edge duplicate crossings removed;
  one native CPU/CUDA cube-coverage difference remains.
- `m7_motion_boundary_review.gfr`: beauty, cuts and live point clouds.

`surface_boundary.h` rejects a duplicate only for the same object/orientation,
a shared indexed edge, matching boundary barycentrics and a relative floating-
point distance bound. Separate layers and opposite-facing thin folds remain.

## Known limits

At file pixel 179,315/sample 6, CPU misses a cube and CUDA hits it. Beauty shows
the same 1/16 coverage difference with deep on and off. Deep differs by 0.7/16
(0.04375), exactly matching the ledger. Strict cross-backend parity therefore
**fails at that pixel**; the report remains failing. The 1e-6 export tolerance
bounds reconstruction against each backend's samples, not native intersection
differences. This limitation must remain visible in release qualification.

Point clouds cannot recover shutter times, lens positions or trajectories from
Z/ZBack/A. Separate layers lose temporal/subpixel correlations; exact independent-
layer motion-blur recomposition is not promised. These surface results do not
qualify volume motion or close [M8 production gates](../../DEEP_MILESTONES.md).

```powershell
builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_motion_gaffer.py builds/install-m6/cycles.exe builds/build-m6/m7-motion-cpu CPU
builds/build-m6/run-cuda.cmd builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_motion_gaffer.py builds/install-m6/cycles.exe builds/build-m6/m7-motion-cuda CUDA builds/build-m6/m7-motion-cpu
```
