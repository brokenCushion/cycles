# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: full-frame CUDA isolation pair running.** Both production deep outputs and all nine
beauty references completed. Independent oracles, exterior alpha, Gaffer depth
cuts and native populations pass. The repaired beauty validator reports a
calibrated count-rate failure: all-samples mean 488, prefix64 mean 470.6,
control maximum 329. Direction passes for both. The approved isolated snapshot
experiment completed: all three off leave-one-out counts are 0; each on render
has 8 unmatched pixels against three controls and every two-control subset.
This is outside the diagnostic off range. The user approved state auditing and
CPU/CUDA/OptiX crop comparisons. Step 1 identified the same eight cloud-over-castle
pixels in every on render. The debug-only audit enlarged metadata from 32 to
8,228 bytes and failed the existing GPU staging reservation assertions; the
comparison queue did not start. No retry or root-cause fix. Generated source/cache
originals and both production/snapshot installs were restored and hash-verified.
The user authorized one full-frame CUDA off/on pair with the existing snapshot
install, original sampling and prefix64/IDs/error1e-3/8192 MiB settings. If the
eight target pixels match, inspect OptiX without building. If CUDA reproduces the
effect, one diagnostic-only rebuild may add an external state snapshot buffer
and capture-only borders; metadata remains 32 bytes. No root-cause fix before review.
Current queue/large outputs: `D:/CyclesDeepScratch/optimization-phase9/cuda-count-diagnostic-20261010/`.
No new build or production change; monitor every 30 minutes.
Steps 2–3, conditional bisect, qualification and Gaffer review remain pending.
[Crop sampling check](builds/validation/landscape-cloud/optimization-phase9/crop-sampling-check.json).
[Investigation evidence](builds/validation/landscape-cloud/optimization-phase9/state-investigation-step1.json).
[Production results](builds/validation/landscape-cloud/optimization-phase9/production-results.json),
[run 1 count gate](builds/validation/landscape-cloud/optimization-phase9/deep-all-beauty-count-policy.json),
[run 2 count gate](builds/validation/landscape-cloud/optimization-phase9/deep-64-beauty-count-policy.json).
[Snapshot diagnostic](builds/validation/landscape-cloud/optimization-phase9/majorant-count-diagnostic.json).
No production render repeated; no diagnostic retries or extra cases.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Large evidence/TEMP stay on D:.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
