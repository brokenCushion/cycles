# Shared deep capture records

Architecture patch 2, 2026-09-19. CPU surface/volume traversal, CUDA surface
traversal, host capture and temporary spill storage now share the plain data
definitions in `kernel/deep/types.h`. The capture class is named `deep::Capture`;
it covers opaque surfaces, transparent surfaces and homogeneous volumes.

## Contract

- `KernelDeepEvent` carries an explicit surface/volume kind, front/back depth,
  local surface alpha and integrated volume optical depth. The inactive value
  must be zero. Surfaces have equal depths; volumes have positive extent.
- `KernelDeepResult` carries status, event count and failure reason. Only
  COMPLETE with no error is accepted. COMPLETE with zero events is a camera
  miss. SKIPPED contributes neither a sample nor an empty chain. EMPTY, ACTIVE,
  FAILED and malformed results cannot be reconstructed or published.
- CUDA records retain pixel/sample identity and independently measured adaptive
  population. Completion is consumed after queue synchronization. No pointers,
  STL objects or thread-local dynamic allocations enter the shared format.
- Spill storage contains the same result and event types at checked fixed
  offsets. Explicit zero initialization means EMPTY. Duplicate identities and
  incomplete adaptive populations still fail finalization.
- The temporary spill file is process-local scratch, not a portable persistence
  or resume format. Output EXR channels and reconstruction semantics are unchanged.

## Storage tradeoff

This patch establishes meaning, not the final efficient GPU layout. An event
is 20 bytes, a result is 12 bytes, and the existing fixed 64-event CUDA record
is now 1,308 bytes (previously 532). The 512-lane host/device buffers each occupy
669,696 bytes, totaling 1,339,392. Spill preflight reserves 2 MiB for staging,
scratch and I/O, plus its existing scanline/reconstruction allowance. In-memory
raw storage and spill disk stride are `12 + 20 * max(1, event_capacity)` bytes
per camera sample; adaptive population storage is additional.

These are capture payload sizes, not total renderer memory. No speed or register
usage improvement is claimed. Configured-capacity event planes, queue ordering,
and current resource measurements belong to architecture patch 3. CUDA volumes
and other backends remain unsupported.

## Validation

The Release renderer rebuild/install succeeds. All seven CTest groups pass
(10.55 seconds). Generated reports and Gaffer review files live under
`builds/validation/typed-records/`.
The existing surface, adaptive, depth-of-field, motion and CPU volume acceptance
scripts retain their numerical gates.

The capture tests additionally cover complete misses versus skipped lanes,
unfinished/failed status rejection, malformed kinds and inactive fields,
surface-only rejection of volume events, exact byte budgets, duplicate sample
identities and identical in-memory/spill reconstruction.

CPU SVM/OSL surfaces: nine scenes and eight rejection cases pass. CPU volumes:
nine scenes and twelve rejection/preservation cases pass. Maximum volume
Gaffer/raw-ledger cut error is `4.0410239277033355e-7`; the independent geometry
error is `1.2843861837419368e-6`, within its separate `2e-6` gate. Volume beauty
is identical with deep enabled/disabled. All 31 matching raw CSV files across
the previous native-output surface/volume baselines are byte-identical.

The fresh `volume/m8_cpu_volume_review.gfr` is open in Gaffer with live
DeepToPointCloud readers, blue Z/orange ZBack clouds, and VolumeDepthCut controls.
These are points reconstructed from the new EXR, not points sampled from meshes.
The saved graph reloads with 135,974 points in each cloud.

CUDA on RTX 3080: eleven surface fixtures and four rejection cases pass,
including 64 layers, exact capacity, overflow and unsupported shaders. Maximum
exported-curve error is `2.60770320892334e-8`, CPU/CUDA depth difference is
`1.9073486328125e-6`, and deep-on/off beauty difference is `1.86264514923096e-9`
(within the existing `2e-6` accumulation tolerance). These render timings include
JIT compilation and are not isolated performance measurements.

Camera-feature regressions also pass without changing acceptance thresholds:

| Group | Scene cases | Maximum exported-curve error |
| --- | ---: | ---: |
| CPU adaptive (including OSL) | 8 | 2.60770320892334e-8 |
| CUDA adaptive | 7 | 2.60770320892334e-8 |
| CPU/CUDA/OSL depth of field | 17 | 2.88709998130798e-8 |
| CPU rigid motion (including OSL) | 12 | 2.81610572083757e-8 |
| CUDA rigid motion | 11 | 2.88709998130798e-8 |

Adaptive CPU/CUDA sample populations agree, including skipped lanes. Adaptive
and motion beauty differences are zero in these runs. Maximum DOF beauty
difference is `5.24520874023438e-6`, below the existing 64-contribution FLOAT
accumulation bound (`64 * 2^-23`, about `7.63e-6`); this is not a byte-equality
claim for CUDA beauty. Existing adaptive depth-convergence limitations and M7
mesh-edge backend precision limitations remain unchanged.

Reproduction uses the existing `validate_transparency_gaffer.py`,
`validate_volume_capture_gaffer.py`, `validate_cuda_gaffer.py`,
`validate_adaptive_gaffer.py`, `validate_dof_gaffer.py`, and
`validate_motion_gaffer.py` scripts with `builds/install-m6/cycles.exe`.
Gaffer's launcher is under `builds/build-gaffer/gaffer-1.7.2.0-windows/bin/`;
the CUDA compiler environment is supplied by `builds/build-m6/run-cuda.cmd`.
The generated `builds/validation/typed-records/run-validation.ps1` records the
eight invocations and separate logs. Native tests use:

```powershell
cmake --build builds/build-m6 --target install --config Release --parallel 2
ctest --test-dir builds/build-m6 -C Release --output-on-failure
```
