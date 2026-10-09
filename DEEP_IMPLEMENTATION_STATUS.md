# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7, 8a and 8b accepted.

**Phase 8c is stopped at the final CUDA beauty gate.** Adaptive shader evaluation
passes 18 fixtures and 31 explicit/atomic rejections, h/64/finer-step checks and
CPU/OptiX repeated-curve identity. Midpoint convergence is order 2.
Native identity is 81/81 strict, 30/30 numeric and OptiX 65/65; nine CTests,
CPU beauty exactness and unchanged beauty source/75-resource proofs pass.
CUDA small-landscape 1e-4 flags Normal.X at file pixel (22,8): 1.12e-8 difference
versus 9.76e-9 calibrated allowance. Bias and noisy RGBA pass. No gate changed.
42 subsequent GPU beauty targets and the fresh OSL surface replay remain unrun.
[Measurements](builds/validation/landscape-cloud/optimization-phase8c/final-results.json)
and the plan's Section 6 retain the failure. Connected Gaffer review:
`D:/CyclesDeepScratch/regression-tools/phase8c_review.gfr`. Phase 9 is unlaunched.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Large evidence/TEMP stay on D:.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
