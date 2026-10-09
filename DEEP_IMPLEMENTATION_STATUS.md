# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9 stopped for user review.** Both production deep outputs and all nine
beauty references completed. Independent oracles, exterior alpha, Gaffer depth
cuts and native populations pass. The repaired beauty validator reports a
calibrated count-rate failure: all-samples mean 488, prefix64 mean 470.6,
control maximum 329. Direction passes for both. The approved isolated snapshot
experiment completed: all three off leave-one-out counts are 0; each on render
has 8 unmatched pixels against three controls and every two-control subset.
This is outside the diagnostic off range, so the user's majorant-race attribution
condition is not met. Matched-count raw/bias checks, further investigation and
Gaffer presentation remain stopped. Production binaries/source and gates are
preserved; no acceptance decision has been made.
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
