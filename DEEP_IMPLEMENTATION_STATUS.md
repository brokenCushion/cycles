# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a stopped at a strict surface toolchain difference (25/111 audited).**
Pairing: clang-cl 20.1.8 / NVCC 12.8.61 (CUDA 12.8.0), OptiX 8.0.0,
native precompiled modules; [toolchain](BUILDING.md).
The corrected numeric rule is recorded: both builds pass their own oracles;
cross-build numeric differences are informational. CPU adaptive_volume 1e-4
passes. Twenty-four audited cases pass; strict surface_ao fails the retained
1e-6 cross-build ceiling with a 0.357442 difference at (14,12).
Its surface depths shift one FLOAT ULP; a cut between the shifted steps sees
the large difference. Final flattened-alpha difference is only 4.75330e-7.
Both own oracles/depth cuts pass; all tested CPU beauty/raw pairs are exact.
[Case report](builds/validation/landscape-cloud/optimization-phase8a/toolchain-audit.json),
[surface diagnostic](builds/validation/landscape-cloud/optimization-phase8a/strict-surface-toolchain-difference.json).

Nine CTests, unchanged beauty sources and all 75 accepted CUDA resource records
pass. [Raw CUDA spill diagnostic](builds/validation/landscape-cloud/optimization-phase8a/raw-capture-toolchain.json)
is complete. No baseline promoted. Remaining 86 audit cases, identity replay,
full regression, OptiX AO/Bevel, GPU beauty and speed measurements are pending.
No Phase 8b/8c or Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is retained;
[13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:; legacy references remain unchanged.
