# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7, 8a and 8b accepted.

**Phase 8c stopped at an acceptance failure.** Shader-evaluated volumes are
implemented, with EXR method/step/error metadata. The textured CPU 1e-4 fixture
at step 0.005 differs from the 4x-finer reference by 0.003713, above header 0.0001.
The difference exists before fitting; root cause remains to be established.
Both own reconstruction oracles pass and CPU beauty remains exact. Four
constant CPU/OptiX mode cases and eight atomic rejections pass.
[Partial results](builds/validation/landscape-cloud/optimization-phase8c/phase-results.json).

Native identity (81/81 + 30/30, OptiX 65/65), full regression, nine CTests,
landscape and remaining OSL volume qualification are pending for this build.
The acceptance failure stopped the queue. No gate was relaxed.
Connected Gaffer diagnostic: `D:/CyclesDeepScratch/regression-tools/phase8c_failure_review.gfr`.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. [Support matrix](src/deep/RELEASE_MATRIX.md).
Large evidence/TEMP stay on D:. Phase 9 remains unlaunched.
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
