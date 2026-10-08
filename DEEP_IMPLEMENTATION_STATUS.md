# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a is stopped, unqualified.** OptiX SVM capture is implemented;
the landscape's native shader pipeline fails linking even in a diagnostic
build with all deep OptiX host code disabled. Beauty sources and all 75 CUDA
resource records are unchanged; nine CTests pass. One OptiX volume probe
matches CUDA exactly and passes its independent alpha oracle.

Full matrices/boundaries, fresh 81/81 + 30/30 identity, CUDA/OptiX beauty
and fixed-case after measurements remain pending. Saved probe raw passes also
lack the required denoiser inputs/counts; the beauty validator rejects them.
[Evidence and pending checks](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).
No Phase 8b/8c or full-resolution Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is restored
verbatim; [13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:.
