# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a stopped at strict small-landscape depth-window comparison.**
Pairing: clang-cl 20.1.8 / NVCC 12.8.61 (CUDA 12.8.0), OptiX 8.0.0,
native precompiled modules; [toolchain](BUILDING.md).
All 25 retained cases pass the corrected rules; surface_ao needs at most 3 ULP.
Audit coverage: 106/111, with 105 passing; CPU/CUDA matrices and both boundary
suites pass. Small-strict (47x20/16) fails at 379/940 pixels: four-ULP-envelope
residual max 0.05626202 > 1e-6 at (26,7). Some pixels still fail at a diagnostic
64-ULP window. Flattened alpha is identical; both own oracle/depth cuts pass.
[Case report](builds/validation/landscape-cloud/optimization-phase8a/toolchain-audit.json),
[window diagnostic](builds/validation/landscape-cloud/optimization-phase8a/strict-landscape-depth-window.json).

Nine CTests, unchanged beauty sources, all 75 accepted CUDA resource records
and all tested CPU beauty/raw pairs pass. No baseline promoted. Five audit
cases, identity replay, full regression and OptiX/GPU beauty/speed qualification
remain pending. AO/Bevel fixtures/stage prepared. No Phase 8b/8c or Phase 9 run.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is retained;
[13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:; legacy references remain unchanged.
