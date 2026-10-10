# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: fresh standalone installation repair authorized; regression pending.**
Fix `47a8dffb0` excludes deep GPU capture from beauty scheduler timing.
Snapshot+fix OptiX off/on/on have identical schedules, zero count mismatches,
and resolve the original eight pixels. CUDA ordinary controls reproduce the
five-pixel cluster: new-three leave-one-out counts 5/0/0; both deep-on runs
have zero unmatched pixels. This is ordinary CUDA nondeterminism.

[CUDA count evidence](builds/validation/landscape-cloud/optimization-phase9/cuda-repeatability.json).
[Timing diagnostic](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-fix.json).
[PTX register rename audit](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-module-check.json).

Both builds succeeded. Standalone installation wrote executables into the old
Phase8c location despite the new prefix; the expected new executable was absent
and preservation checks failed. The old standalone cycles/test executable hashes
changed. Generated sources and GPU caches were restored; recorded Blender and
diagnostic hashes remain preserved. No regression or replacement render started.
[Failure evidence](builds/validation/landscape-cloud/optimization-phase9/production-timing-build-failure.json).
The user designated those two old binaries superseded, not recoverable, rebuildable
from `0c6821277` with a different hash; preserved reference outputs remain the gates.
Fresh standalone configure uses the new install prefix and checks every generated
install destination before installing. Then the authorized regression, replacement
OptiX run2 and fresh five ordinary/four seed controls continue. Stop on any failure.
Both preserved production deep outputs pass deep gates; beauty remains pending.
No retries or Gaffer review before all production gates pass.

Build/regression: `D:/CyclesDeepScratch/optimization-phase9/scheduler-timing-production-20261010/`.
Large artifacts/TEMP stay on D:. Existing production and diagnostic installs
remain preserved. Snapshot patch is diagnostic only.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
