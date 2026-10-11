# Archived development reports

The approved [M8 release record](M8_RELEASE_VALIDATION.md), including the
qualified executable SHA, remains in the working tree. Current qualification
follows the [validation policy](VALIDATION.md) and [support matrix](SUPPORT.md).

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

## Completed development documents

The documents below were retired or consolidated during repository cleanup.
Their last working-tree versions are preserved at commit **9e1924416**.
Final guidance lives in this documentation folder and the deep output guide.

| Former path | Summary |
| --- | --- |
| [DEEP_IMPLEMENTATION_STATUS.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/DEEP_IMPLEMENTATION_STATUS.md) | Final status consolidated into support and production evidence. |
| [DEEP_MILESTONES.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/DEEP_MILESTONES.md) | Completed milestone checklist; superseded by the accepted support matrix. |
| [DEEP_OPTIMIZATION_PLAN.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/DEEP_OPTIMIZATION_PLAN.md) | Phase-by-phase decisions and measurements; final rules extracted into VALIDATION.md. |
| [DEEP_PERFORMANCE_AND_VDB.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/DEEP_PERFORMANCE_AND_VDB.md) | Earlier performance and VDB results; final production measurements are in the evidence page. |
| [src/deep/ARCHITECTURE_REVIEW.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/src/deep/ARCHITECTURE_REVIEW.md) | Code map and invariants consolidated with architectural requirements. |
| [src/deep/NATIVE_VDB_PLAN.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/src/deep/NATIVE_VDB_PLAN.md) | Native grid contract consolidated into the deep output guide. |
| [src/deep/LANDSCAPE_COMPATIBILITY.md](https://github.com/brokenCushion/cycles/blob/9e19244164f86f03dbfb7172462dd8fbc2a8a26d/src/deep/LANDSCAPE_COMPATIBILITY.md) | Historical landscape status pointer; superseded by accepted production evidence. |

Read the full completed optimization history without restoring it:

~~~powershell
git show 9e1924416:DEEP_OPTIMIZATION_PLAN.md
~~~

Architectural requirements formerly in PEER_DEEP_OUTPUT_REQUIREMENTS.md are
merged into [ARCHITECTURE.md](ARCHITECTURE.md); the original is also retained
at the same commit. The approved [M8 release record](M8_RELEASE_VALIDATION.md)
stays in the working tree with its executable identity intact.
