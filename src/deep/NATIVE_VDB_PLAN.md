# Native VDB deep alpha

Part of **M8**, not a completed production release.
[Release checklist](../../DEEP_MILESTONES.md) | [Evidence](../../DEEP_PERFORMANCE_AND_VDB.md)

## Current contract

- CPU/CUDA native SVM; static pinhole camera and fixed sample count.
- Native FLOAT NanoVDB density, FULL source precision and linear interpolation.
- One scalar absorption or Henyey-Greenstein scattering node: density attribute
  Fac multiplied by finite, nonnegative constants; grayscale colour. Scattering
  contributes to extinction only. Combined closure graphs remain unqualified.
- Z/ZBack/A visibility only. Emission/scattering colour is M9. Native VDB
  scattering matches equivalent absorption byte-for-byte on CPU/CUDA. Both
  Gaffer checks pass, including the fixed absolute HDR beauty regression gate.
- Unsupported shaders, formats, motion and exhausted capacities fail explicitly.
- CPU/CUDA 664x625 checkpoints and repeated 1024x768/four-sample production
  tests pass. The latter meet accuracy, beauty, memory, size and timing targets;
  final CPU/CUDA scene-matrix coverage also passes.

## Implementation map

| File | Responsibility |
| --- | --- |
| `kernel/deep/density.h` | Cubic Bernstein density along a trilinear cell; integral and approximation bound. |
| `kernel/deep/grid.h` | Allocation-free cell traversal with boundary ties and explicit limits. |
| `kernel/deep/volume_grid.h` | Native corner reads and bounded event emission. |
| `kernel/deep/volume_native.h` | Density attribute, object/grid transforms, physical length and axial depth. |
| `kernel/deep/volume_boundary.h`, `volume.h` | Double-precision triangle crossings and volume interval pairing after BVH object discovery. |
| `session/deep.cpp` | Scene/material allowlist and scalar shader metadata. |
| `integrator/path_trace_work_{cpu,gpu}.cpp` | Host-preallocated worker/device buffers and readback. |
| `deep/capture.cpp` | Memory/spill storage and complete-sample reconstruction. |
| `deep/volume.cpp` | Interval fitting and curve comparisons. |
| `deep/exr_writer.cpp` | FLOAT conversion, error check and atomic publication. |

Paths above are relative to `src/`. Core code has no Blender/Gaffer dependency.

## Bounds and accuracy

- Native grid limit: 4096 raw events, 16384 fitted intervals, 16384 traversal cells.
- CPU scratch is reserved per worker. CUDA now uses 64 lanes with an explicit
  32 MiB host/device staging reservation; the former 8192-slot/two-lane limit
  is removed. This excludes compiler local storage and renderer memory. Measured
  device-wide peaks are 4038 / 4203 MiB on the qualified 1024x768 repeats.
- Companion records retain double-precision front/back depths and FLOAT density
  coefficients. GPU threads do not allocate memory.
- Total curve budget: 1e-6. Allocations: 1e-7 density fit, 5e-8 reconstruction
  (half mixture fitting, half bounded streaming reduction),
  4e-8 coefficient rounding, remainder for FLOAT export.
- A thin interval may collapse to a step only when the whole-curve check passes.
  Unrepresentable extinction fails publication and preserves prior output.

## Tests and next work

`density_cell_test.cpp` / `density_cell_cuda_test.cu` check cell arithmetic and
traversal. `vdb_grid_test.cpp` / `vdb_grid_cuda_test.cu` compare the actual supplied
grid with independent OpenVDB integration. Gaffer validators cover EXR curves,
per-device beauty identity, backend comparison and scale invariance.

The boundary fix and cumulative-depth FLOAT conversion pass the 1024x768
production repeats, including memory and spill-I/O measurements. Final
supported scene-matrix coverage passes. The repeated triangle scans use constant scratch;
their cost on complex volume boundaries still needs testing.
Final mixed-scene coverage passes; final Gaffer review remains the M8 sign-off
gate. Deep RGB is deferred.

Fresh named-grid overlap qualification exposed a clipped voxel-start arithmetic
disagreement. Grid initialization now reconciles the selected cell with incoming
and outgoing integer-plane times. CPU/CUDA regression cases include both ray
directions and starts one DOUBLE step before/at/after the crossing, preserving
short positive-length intervals. Final CPU/CUDA renderer qualification passes
11 accepted VDB scenes and nine safe rejections per device under
`builds/validation/m8-release-qualified/native-default-colour/`. The corrected
default-colour matrix and release-scale scattering repeats pass. See
[release qualification](M8_RELEASE_VALIDATION.md).
