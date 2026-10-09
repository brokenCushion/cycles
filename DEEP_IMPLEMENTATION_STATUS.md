# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-8 accepted.

**Phase 8c accepted. Phase 9 production launch is authorized.** Adaptive shader evaluation
passes 18 fixtures and 31 explicit/atomic rejections, h/64/finer-step checks and
CPU/OptiX repeated-curve identity. Midpoint convergence is order 2.
Native identity is 81/81 strict, 30/30 numeric and OptiX 65/65; nine CTests,
CPU beauty exactness and unchanged beauty source/75-resource proofs pass.
The user-approved accumulated-term floor resolves Normal.X at file pixel (22,8).
The historical step-2 audit, all 64 CUDA/OptiX beauty targets and the fresh
30-case OSL surface identity/oracle/beauty replay pass. No native render repeated.
[Measurements](builds/validation/landscape-cloud/optimization-phase8c/final-results.json)
and the plan's Section 6 retain both original and completed evidence. Gaffer:
`D:/CyclesDeepScratch/regression-tools/phase8c_final_review.gfr`. Phase 9 run 1 rendered and passed oracle, exterior, Gaffer cuts and native-population checks.
Both deep runs and nine beauty references completed; oracle/exterior/cuts/populations pass.
**Phase 9 stopped:** beauty validator `KeyError: four_ulp` on an empty same-count
reference set. Stored counts have 460/439 unresolved pixels; no retry or review.
[Production results](builds/validation/landscape-cloud/optimization-phase9/production-results.json).
[Run 1 checks](builds/validation/landscape-cloud/optimization-phase9/run1-checks.json).

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Large evidence/TEMP stay on D:.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
