# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a: new-build qualification in progress.**
clang-cl 20.1.8 / NVCC 12.8.61 (CUDA 12.8.0), OptiX 8.0.0, native modules;
[toolchain](BUILDING.md). Final toolchain policy is recorded in Section 2.
All 111 cross-build cases pass own oracle/depth cuts and the flattened-alpha
sanity bound; cross-build curve differences are informational.
[Audit](builds/validation/landscape-cloud/optimization-phase8a/toolchain-audit.json).
Candidates are staged; identity reruns, full regression and CUDA/OptiX beauty
qualification with AO/Bevel remain pending. No baseline promoted.
Beauty sources and 75 CUDA resources unchanged; CPU beauty/raw exact;
nine CTests pass. No Phase 8b/8c/9 started.

[M8 release record](src/deep/M8_RELEASE_VALIDATION.md),
[13 archived reports](src/deep/ARCHIVED_REPORTS.md),
[core map/regression](src/deep/README.md).
Latest qualified review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:; old references remain unchanged.
