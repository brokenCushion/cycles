# Archived validation reports

The approved [M8 release record](M8_RELEASE_VALIDATION.md), including the
qualified executable SHA, remains in the working tree. Current qualification
follows the [optimization plan](../../DEEP_OPTIMIZATION_PLAN.md).

These 13 historical reports last existed at `251c66d7d^` (`035d84b4e`).
They document earlier checkpoints, not the current support matrix.

| Report | One-line summary |
| --- | --- |
| `ADAPTIVE_VALIDATION.md` | Accepted camera populations, skipped lanes and adaptive-sample completeness. |
| `CAPTURE_VALIDATION.md` | Initial opaque camera capture, axial depth, misses and sample identity. |
| `CUDA_STORAGE_VALIDATION.md` | Historical bounded CUDA event planes, readback and resource accounting. |
| `CUDA_VALIDATION.md` | Initial CUDA SVM surface correctness and GPU resource evidence. |
| `CUDA_VOLUME_VALIDATION.md` | CPU/CUDA homogeneous extinction, overlapping media and boundary qualification. |
| `DENSITY_REFERENCE_VALIDATION.md` | Piecewise-linear host density integration and its transmittance error bound. |
| `DOF_VALIDATION.md` | Accepted lens rays, aperture validation and surface depth-of-field fixtures. |
| `EXR_VALIDATION.md` | Deep scanline layout, FLOAT rounding, compression and whole-curve publication checks. |
| `MOTION_VALIDATION.md` | Rigid surface/camera motion, shutter identity and rejected motion features. |
| `NATIVE_OUTPUT_VALIDATION.md` | Deep output-driver API, session reset, cancellation and callback lifecycle. |
| `PRODUCTION_VALIDATION.md` | Bounded capture/spill, reconstruction, reduction and atomic publication. |
| `TRANSPARENCY_VALIDATION.md` | Scalar transparency chains, capacity limits and explicit unsupported-shader failures. |
| `VOLUME_VALIDATION.md` | Transmittance reconstruction, homogeneous capture and checked exponential fitting. |

Read any report without restoring it; substitute its filename:

```powershell
git show 251c66d7d^:src/deep/CUDA_VALIDATION.md
```
