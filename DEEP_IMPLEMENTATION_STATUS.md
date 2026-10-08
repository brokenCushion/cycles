# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7, 8a and 8b accepted.

**Phase 8c is in progress.** Initialized, deterministic shader evaluation passes
CPU/OptiX same-step identity and the native beauty source/75-resource proofs.
The independent uncapped CPU h/64 oracle confirms midpoint order 2.
[Convergence](builds/validation/landscape-cloud/optimization-phase8c/deterministic-convergence.json).
Adaptive step doubling is building; complete volume qualification, native
81/81 + 30/30 + OptiX 65/65, regression and Gaffer presentation remain pending.
No gate or total error bound changed. Phase 9 remains unlaunched.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Large evidence/TEMP stay on D:.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
