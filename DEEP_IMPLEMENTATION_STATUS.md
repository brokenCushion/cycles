# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: scheduler timing diagnostic complete; stopped on CUDA count failure.**
Both preserved production deep outputs pass the deep gates; production beauty
qualification remains incomplete. Fix `47a8dffb0` excludes GPU capture from scheduler
timing. Native beauty-source proof and the small117x50 batch test pass.

The isolated snapshot+fix full-frame runs all use identical 68 batches and 64 adaptive
checkpoints. Both OptiX off/on comparisons have zero image-wide count mismatches;
the original eight pixels now match counts, with target raw differences ≤3 ULP.
CUDA has five other count mismatches, each 16 fewer samples with deep on, despite
identical schedules. This fails the required zero-mismatch criterion. The original
eight CUDA targets still match counts, with raw differences ≤2 ULP. Noisy alpha is
exact throughout. Cause of the five CUDA mismatches is uninvestigated.

[Full diagnostic measurements, raw passes and preservation proof](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-fix.json).
[OSL MNEE register rename audit and rebuilds](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-module-check.json).
[Preserved production results](builds/validation/landscape-cloud/optimization-phase9/production-results.json).
Large artifacts: `D:/CyclesDeepScratch/optimization-phase9/scheduler-timing-fix-20261010/`.
All 36 managed originals and 28 qualified executable/module hashes are preserved.
No retry, further investigation, production rebuild, regression or Gaffer review.
Stopped after step 3 for user review; monitor removed.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Snapshot patch is diagnostic only; production unchanged.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
