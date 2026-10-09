# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: CUDA count isolation complete; stopped for review.** Both production deep outputs and all nine
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
The authorized full-frame CUDA off/on pair completed with zero count mismatches
over 587,500 pixels. At the eight targets, raw differences are at most 2 FLOAT ULP;
alpha is exact. The repeated count effect is OptiX-specific in this experiment.
No-build inspection found queue-private launch parameters and a private deep SBT;
no beauty overwrite was identified. Deep time changes beauty batch boundaries on
both devices, suggesting a scheduling/accumulation route, but the root cause is
unproven. No rebuild, state audit, bisect or fix followed. Production and snapshot
hashes remain unchanged. Stopped before any fix; qualification/Gaffer remain pending.
[CUDA counts, every raw pass and source inspection](builds/validation/landscape-cloud/optimization-phase9/cuda-count-isolation.json).
Large outputs: `D:/CyclesDeepScratch/optimization-phase9/cuda-count-diagnostic-20261010/`.
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
