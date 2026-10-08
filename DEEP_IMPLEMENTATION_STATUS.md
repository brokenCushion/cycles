# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a accepted.** OptiX SVM, including native
AO/Bevel outside opacity dependencies, is qualified alongside CPU/CUDA.
[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0. Independent oracles/depth cuts and CUDA/OptiX beauty policy pass.
CPU beauty exact; beauty sources and 75 kernel records unchanged; nine CTests pass.
Same-build identity: CPU/CUDA 81/81 strict + 30/30 numeric;
OptiX 41 strict + 24 numeric. Separate references promoted; old ones retained.
Cross build/backend flattened alpha <=1e-4; other cross differences informational.
[Results](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).

Connected Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase8a_review.gfr`.
Large evidence/TEMP stay on D:.

**Phase 8b qualification is in progress.** CPU/OptiX OSL surface fixtures:
30/30 mode/backend checks and 26/26 atomic rejection checks pass, including
native alpha/oracles, CPU exact beauty, calibrated OptiX beauty and rerun identity.
The user-approved texture waiver records pristine difference 9.01580e-4 beside
deep difference 9.01609e-4; own native-alpha errors stay below 9.8e-8.
[OSL results](builds/validation/landscape-cloud/optimization-phase8b/osl-surfaces-qualified.json).
Final SVM identity/regression, GPU beauty and fixed-case measurements are running.
Connected OSL review: `D:/CyclesDeepScratch/regression-tools/phase8b_review.gfr`.
Phase 8c/9 remain unlaunched.
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
