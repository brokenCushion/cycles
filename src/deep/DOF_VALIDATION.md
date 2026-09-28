# Surface depth of field

Capture uses the accepted Cycles lens ray. Depth is camera-axis Z, not distance
from the lens. Qualified scope is mono perspective, box filter width 1, scalar
surface materials, CPU native/restricted OSL and CUDA native. Aperture, focus,
ratio and rotation must be finite and valid; malformed values fail before output.
See [rigid motion](MOTION_VALIDATION.md) for the combined surface case.

## Evidence

`validate_dof_gaffer.py` checks three focus distances, focused/defocused edges,
adaptive sampling and a rotated six-blade anamorphic aperture. Eight CPU/CUDA
pairs plus a CPU OSL case pass accepted-camera ledger checks (error below 3e-8).
The focused plane preserves camera hit/miss decisions; defocus changes 1,292 of
49,152 decisions. Fourteen invalid-camera cases preserve the previous EXR.

CPU beauty matches exactly. CUDA's 64-sample unit-range fixtures allow
64 * 2^-23 accumulation error (7.63e-6); observed deep-on/off and ordinary-repeat
noise reached 5.723e-6. This is not a universal production tolerance.

Artifacts: `builds/build-m6/m7-dof`. The larger 640x480, 16-sample primitives
review is `builds/build-m6/m7-dof-primitives/m7_dof_primitives.gfr`:
eight CPU/CUDA cuts differ by at most 5.961e-8 alpha, and both point clouds
validate at a one-million-point display cap. Timings were not isolated benchmarks.

## Display limits

DeepToPointCloud projects pixel centres through a central camera. Z/ZBack/A
cannot recover lens origins or subpixel positions. Separate deep layers lose
coverage correlations; exact independent-layer bokeh recomposition is not promised.
Volume combinations require their own [M8 qualification](../../DEEP_MILESTONES.md).

```powershell
builds/build-m6/run-cuda.cmd builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_dof_gaffer.py builds/install-m6/cycles.exe builds/build-m6/m7-dof
```
