# M7c — Rigid object and camera motion

## Contract

CPU native/OSL and CUDA native deep capture use the renderer's accepted camera
ray and shutter time. Geometry intersections follow the existing Cycles motion
interpolation. Exported Z is positive axial depth in the camera at that same
time, using the inverse of the interpolated camera-to-world transform. The
static camera path retains its previous transform.

Qualified scope: rigid translation/rotation of polygon-mesh instances and the
perspective camera, uniform shutter, box filter, fixed or adaptive sampling,
with optional depth of field. Existing scalar-material and backend limits
still apply. Deforming meshes, motion scale/reflection, animated field of view,
rolling shutter and nonuniform shutter curves remain rejected.

Motion arrays in standalone XML contain two or more row-major 3x4 transforms
(12 numbers each). Malformed/nonfinite arrays are rejected before assignment.
Deep validation checks rigidity before rendering. Existing final deep output
is preserved on rejected scenes.

## Acceptance evidence

`validate_motion_gaffer.py` renders 24x18 at 64 maximum samples and independently
reads deep output through Gaffer 1.7.2.0. Eight matching CPU/CUDA fixtures cover:

- Object-only and camera-only depth changes across the shutter.
- Matched camera/object translation and rotation: axial Z remains 6.
- A moving silhouette over a second plane.
- Two transparent planes exchanging depth order.
- Adaptive sampling combined with lens sampling and motion.
- Opaque primary-hit capture under matched motion.

A ninth CPU fixture exercises OSL under rotation. Each deep render is compared
with a deep-disabled beauty render. Complete sample identities, misses and
ordered event chains are checked; raw-ledger transmittance is compared with
the EXR on both sides of every depth boundary. CPU/CUDA populations and chain
lengths agree. Paired object/camera renders also satisfy Z_object + Z_camera =
10 for each sample, within 2.385e-6, checking shared shutter-time sampling.

Results from this workspace:

- Maximum raw-ledger/EXR transmittance error: 2.888e-8.
- Maximum CPU/CUDA event-depth difference: 2.862e-6 scene units.
- Deep-enabled/disabled beauty: exact equality in all motion acceptance cases.
- Seven invalid-scene cases per backend preserve a pre-existing output file.
- Static opaque regression passes; all five CTest groups pass.
- Diagnostic graphs save/reload with live DeepToPointCloud evaluation.

Reports, XML, CSV ledgers, EXRs and diagnostic graphs are in
`build-m6/m7-motion-cpu` and `build-m6/m7-motion-cuda`.

## Reproduce

Run from the workspace in a configured CUDA/MSVC environment for CUDA:

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_motion_gaffer.py install-m6/cycles.exe build-m6/m7-motion-cpu CPU
.\build-m6\run-cuda.cmd .\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_motion_gaffer.py install-m6/cycles.exe build-m6/m7-motion-cuda CUDA build-m6/m7-motion-cpu
```

`create_motion_primitives.py` adds distinct translation endpoints to the existing
sphere/cube/cylinder fixture. `create_cuda_review.py` renders it at 640x480 with
16 samples, compares eight depth cuts, and builds CPU/CUDA beauty, deep,
DeepToFlat, editable DeepSlice and live DeepToPointCloud branches.

## Interpreting the review

These EXRs store shutter-integrated Z/ZBack/A distributions. The point display
projects pixel centres through one camera; it cannot recover original shutter
times, lens origins or world-space trajectories. Separate deep layers also lose
time/subpixel coverage correlations, so exact independent-layer motion-blur
recomposition is not claimed. No hidden geometry is synthesized.

### Initial mismatch, before the shared-edge correction

The initial 640x480 moving-primitives comparison had five CPU/CUDA pixels above 1e-6
alpha difference, maximum 0.043749988. The review graph was saved, reloaded,
opened and its CUDA point cloud framed in Gaffer. Both clouds evaluate at a
one-million-point cap. The graph is `build-m6/m7-motion-primitives/m7_motion_primitives.gfr`.

Independent repeat renders with full raw ledgers reproduce these event-chain
differences (EXR file coordinates, top-left origin):

| Pixel | Sample | CPU | CUDA |
| --- | --- | --- | --- |
| 43,294 | 7 | Three sphere events; first two Z differ by 9.5e-7 | Two sphere events |
| 166,305 | 2 | Three sphere events; first two Z differ by 9.5e-7 | Two sphere events |
| 167,165 | 10 | Three sphere events; last two Z differ by 4.8e-7 | Two sphere events |
| 185,137 | 2 | Three sphere events; first two Z differ by 4.8e-7 | Two sphere events |
| 179,315 | 6 | Miss | One cube event, Z=6.25571632, alpha=0.7 |

This located the discrepancy upstream of EXR reconstruction, in captured mesh
intersections. CPU uses Embree while CUDA uses Cycles' BVH traversal.

`inspect_motion_records.py build-m6/m7-motion-primitives` writes
`target_record_comparison.json` and exits unsuccessfully on these reproduced
discrepancies. `create_cuda_review.py` now explicitly marks and fails acceptance
when any cut exceeds 1e-6, after saving the review graph and report. These
real-geometry checks do **not** pass merely because the diagnostic fixtures pass.

### Shared-edge correction and final evidence

Temporary CPU tracing established that all four sphere pairs were different
triangles at a shared edge, with boundary barycentric coordinates. For example,
pixel 43,294/sample 7 hit triangles 1350 and 1349 at t=3.68082643 and 3.68082762.
Both Cycles and Embree use tolerant triangle-edge tests. A straight repeated
closest-hit traversal can therefore count a single mesh crossing twice.

`surface_boundary.h` now rejects a second crossing only when it has the same
object and orientation, shares exactly two vertex indices, lies on that shared
edge in both triangles' barycentric coordinates, and is within a relative
floating-point distance bound. The first native hit supplies the material.
This applies per accepted ray; samples/times are never combined. Disconnected
layers and opposite-facing entry/exit boundaries are preserved. Capacity is
checked after duplicate rejection, so a redundant edge hit cannot falsely
overflow an otherwise complete chain. Temporary trace instrumentation was removed.

Expanded tests include two surfaces 2e-6 apart in separate objects, disconnected
faces in one object, and a thin connected fold with opposite-facing surfaces.
All retain both events on hit rays. There are now 11 matching CPU/CUDA fixtures
plus the CPU OSL motion case. Native/OSL transparency regressions and all five
CTest groups pass.

The corrected full-resolution artifacts are in `build-m6/m7-boundary-primitives`:

- `CPU.deep.exr` and `CUDA.deep.exr`; complete adjacent raw CSV ledgers.
- `m7_motion_boundary_review.gfr`: saved/reloaded CPU/CUDA beauty, deep,
  cuts and live point-cloud branches, one-million-point display caps.
- `raw_cut_validation.json`: each backend's 4,915,200 sample identities and
  misses checked at eight cuts over every pixel; maximum alpha error
  5.309510866524647e-7 on both backends, below the 1e-6 export limit.
- `beauty_isolation.json`: deep-on/off beauty is exactly equal on CPU;
  maximum CUDA RGBA difference is 1.1920928955078125e-7.
- `target_record_comparison.json`: all four sphere differences removed;
  the sole remaining chain difference is cube pixel 179,315/sample 6.

The remaining cube difference is native to the renderer. At Gaffer pixel
179,164, CPU beauty alpha is 0.6421529650688171 and CUDA is 0.7046529650688171,
exactly a 1/16 coverage difference. Each value is identical with deep enabled
and disabled. Deep alpha differs by 0.7/16=0.04375, matching the single CUDA
cube event in the ledger. This is not an EXR reconstruction or capture-induced
beauty change. Strict CPU/CUDA depth-cut parity still fails at that one pixel,
and its report/exit status have deliberately not been changed to hide it.

**Qualification:** limited rigid-motion capture and reconstruction pass their
independent tests, with the measured native backend edge discrepancy documented.
This does not claim exact CPU/CUDA intersection parity or unrestricted production
motion. Export error tolerances remain 1e-6 against each backend's own ledger;
they are not reinterpreted as a bound on cross-backend geometric differences.
The native edge discrepancy remains a tracked backend limitation for M9.
M6 isolated performance qualification remains open. Volumes remain M8.

Additional validation commands in Gaffer Python:

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_motion_cuts_gaffer.py build-m6/m7-boundary-primitives
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_motion_beauty_gaffer.py build-m6/m7-boundary-primitives
```
