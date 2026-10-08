# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a stopped at the OptiX small-landscape beauty bias gate.**
[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0. AO/Bevel deep alpha checks pass; ray-traced opacity rejects.
111/111 compiler audit cases pass; staged references match 81 strict + 30 numeric.
All 65 OptiX/CUDA alpha pairs pass the unified cross rule and own oracles.
CPU beauty exact; beauty sources and 75 resources unchanged; nine CTests pass.

CUDA beauty: 32 PASS. OptiX: 20 PASS, 1 FAIL, 11 pending. Small strict noisy green
bias is 1.06980727e-9, above the unchanged 3-SE limit 1.06187651e-9;
all 940 pixels reproduce a count-matched raw state. Opaque-foreground (0,5)
resolves with six bit-identical snapshot diagnostic renders. Diagnostic-only
beauty changes are absent from the deep branch and restored out of its overlay.
No reference promotion; same-build/backend reruns remain pending.
[Results](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).

Connected candidate review: `D:/CyclesDeepScratch/regression-tools/phase8a_candidate_review.gfr`.
Old references/default configuration retained. Phase 8b/8c/9 remain unlaunched.
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
