# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: replacement deep output passes; stopped on incomplete full-frame beauty calibration.**
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
the guarded resume preserves the failure record. Replacement OptiX run2 and all
five ordinary/four seed references finished. Deep oracle/cuts pass; count, bias
and denoised beauty pass. Raw beauty has 4,411 fallback pixels and needs 20
ordinary full-frame controls; only five are authorized. One pixel exceeds the
five-control provisional channel range, which is informational until calibration
is complete. No extra renders, investigation or Gaffer review; stopped for review.

[Calibration/regression evidence](builds/validation/landscape-cloud/optimization-phase9/production-timing-calibration-pass.json),
[production stop and metrics](builds/validation/landscape-cloud/optimization-phase9/replacement-production-stop.json),
[install/hash proof](builds/validation/landscape-cloud/optimization-phase9/production-timing-build.json).
Large artifacts remain on D:. Stop on failure; no Gaffer before gates pass.

[Support matrix](src/deep/RELEASE_MATRIX.md), [M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
