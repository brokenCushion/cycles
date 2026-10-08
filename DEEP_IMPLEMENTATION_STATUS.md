# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a stopped at OptiX/CUDA strict depth comparison.**
clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0, OptiX 8.0.0, native modules;
[toolchain](BUILDING.md). Final compiler policy is recorded in Section 2.
111/111 compiler audit checks pass; maximum flattened-alpha difference 4.75348e-7.
New matrix identity 13/13 strict + 26/26 numeric passes. Fifteen OptiX volume
pairs match CUDA exactly. Opaque foreground strict fails the unchanged
unshifted curve gate: 0.750000015 > 1e-6, 486/561 pixels; flattened alpha equal.
Both own oracle maxima 1.49011612e-8; all flagged pixels fit an informational
three-ULP depth window. [Diagnosis](builds/validation/landscape-cloud/optimization-phase8a/optix-depth-diagnostic.json).

384 matching-toolchain deep-off controls complete (AO/Bevel render successfully).
Full CUDA/OptiX beauty policy, boundaries/landscapes, deep AO/Bevel and remaining
identity replay are pending. CPU beauty exact, beauty sources and 75 resources
unchanged; nine CTests pass. No baseline promoted or Phase 8b/8c/9 started.
[Results](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).

[M8 release record](src/deep/M8_RELEASE_VALIDATION.md),
[13 archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:; old references remain unchanged.
