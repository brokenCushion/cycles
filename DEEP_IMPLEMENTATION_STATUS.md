# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9 stopped: CUDA raw fallback calibration has 5 controls; policy requires 20.**
Fresh standalone install destinations, manifest and hashes pass; all 1,059
protected files remain unchanged. Old standalone binaries are superseded as
directed; reference outputs remain preserved. Production has the timing fix,
no snapshot/debug patch, and the accepted [toolchain](BUILDING.md).

Regression: strict81/81, numeric30/30, OptiX65/65, nine CTests, CPU beauty
exactness, native beauty-source proof and 75 kernel resource records pass.
Elapsed 26.66 minutes. Small CUDA case has zero count mismatches, passing
bias and identical denoised beauty. Three raw normal fallback pixels fit the
existing unit-scale ULP floor, but qualification needs 20 ordinary controls.
Only five were authorized; no further renders or analysis launched.
OptiX beauty stage and replacement production run2 have not started.

[Stop evidence](builds/validation/landscape-cloud/optimization-phase9/production-timing-regression-stop.json),
[beauty report](builds/validation/landscape-cloud/optimization-phase9/production-timing-cuda-beauty.json),
[install/hash proof](builds/validation/landscape-cloud/optimization-phase9/production-timing-build.json).
Large artifacts remain on D:. Stop for user decision; no Gaffer before gates pass.

[Support matrix](src/deep/RELEASE_MATRIX.md), [M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
