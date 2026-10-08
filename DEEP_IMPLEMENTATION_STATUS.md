# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7, 8a and 8b accepted.

**Phase 8c in qualification.** Fixed-step OSL/opt-in volume capture is implemented;
method, step rule and the stated/proven error split are written in the EXR.
[Fresh baseline](builds/validation/landscape-cloud/optimization-phase8c/baseline.json).

Accepted 8b CPU/OptiX OSL scalar surface
capture: 30/30 mode/backend cases, 26/26 atomic rejections, native alpha/oracles,
CPU exact beauty, 15/15 OptiX beauty and same-build OSL identity pass.
SVM output unchanged: strict 81/81 + numeric 30/30; OptiX 65/65.
Full regression, nine CTests, 64/64 SVM GPU beauty, unchanged beauty sources
and 75 kernel resource records pass. Five native-variation pixels resolve by
the existing isolated snapshot rule; the deep branch has no beauty patch.
[Results and benchmarks](builds/validation/landscape-cloud/optimization-phase8b/phase-results.json).

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. [Support matrix](src/deep/RELEASE_MATRIX.md)
records the case-specific native texture waiver; every own bound remains intact.
Connected Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase8b_review.gfr`.
Large evidence/TEMP stay on D:. 8c acceptance checks are pending; Phase 9 remains unlaunched.
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
