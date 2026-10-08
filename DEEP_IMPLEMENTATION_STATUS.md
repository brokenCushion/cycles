# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a is stopped, unqualified: mandatory CUDA identity failed.**
Deep Blender and standalone use clang-cl 20.1.8, CUDA 12.8.0 / NVCC 12.8.61
and OptiX 8.0.0 with native precompiled modules; see [toolchain](BUILDING.md).
The first CUDA strict case (denoised_volume, 33x17/four samples) changes
114,297 -> 114,304 deep samples. Counts differ at 14 pixels; A/Z/ZBack differ
at 96 pixels. All deterministic headers match. Whole-curve difference
2.16944e-7 is informational: CUDA byte identity remains mandatory.
[CUDA evidence](builds/validation/landscape-cloud/optimization-phase8a/clangcl-cuda-identity-precheck.json).

CPU deep-on/off beauty and stored passes are exact in the initial precheck;
beauty sources and all 75 accepted CUDA resource records match. The user
permits separate CPU compiler references only after oracle/depth-cut and
rounding checks. No references were changed. Full replay, CPU re-baselining,
OptiX AO/Bevel and GPU beauty qualification remain pending; no further renders
were launched after the CUDA failure. No Phase 8b/8c or Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is retained;
[13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:.
