# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: calibrated small-fixture beauty and retained regression pass; replacement production is running.**
Fresh standalone install destinations, manifest and hashes pass; all 1,059
protected files remain unchanged. Old standalone binaries are superseded as
directed; reference outputs remain preserved. Production has the timing fix,
no snapshot/debug patch, and the accepted [toolchain](BUILDING.md).

Regression: strict81/81, numeric30/30, OptiX65/65, nine CTests, CPU beauty
exactness, native beauty-source proof and 75 kernel resource records pass.
Added 15 CUDA and 15 OptiX small-scene controls, 20 ordinary controls per backend.
Both calibrated beauty gates pass. Retained regression completed in 721.844 s
(12.03 min), reusing completed renders and rejection evidence. A shared TEMP/TMP
directory startup error was fixed and tested before any production render began;
the guarded resume preserves the failure record. The authorized replacement
OptiX run2 and fresh five ordinary/four seed references follow. Full-frame extra
controls still require approval.

[Calibration/regression evidence](builds/validation/landscape-cloud/optimization-phase9/production-timing-calibration-pass.json),
[install/hash proof](builds/validation/landscape-cloud/optimization-phase9/production-timing-build.json).
Large artifacts remain on D:. Stop on failure; no Gaffer before gates pass.

[Support matrix](src/deep/RELEASE_MATRIX.md), [M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
