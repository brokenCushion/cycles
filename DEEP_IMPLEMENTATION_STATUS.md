# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9 stopped: run2 passes; all-samples run1 fails one calibrated raw-beauty channel.**
Fresh standalone install destinations, manifest and hashes pass; all 1,059
protected files remain unchanged. Old standalone binaries are superseded as
directed; reference outputs remain preserved. Production has the timing fix,
no snapshot/debug patch, and the accepted [toolchain](BUILDING.md).

Regression: strict81/81, numeric30/30, OptiX65/65, nine CTests, CPU beauty
exactness, native beauty-source proof and 75 kernel resource records pass.
Added the approved 15 full-frame controls: 20 ordinary controls/four seeds now
qualify run2, including `(978,81)`. The conditional all-samples render completed;
both outputs pass deep oracle, exterior, cuts and native sample populations.
Both beauty count, bias and denoised gates pass. Run1 Normal.Z at `(992,78)`
differs by 0.00950998, above its calibrated allowance 0.00894315. Phase9 is not
accepted. No retry, further investigation or Gaffer presentation.
Retained regression completed in 721.844 s (12.03 min), without repeating
completed renders. Harness repairs preserved prior outputs and changed no gates.

[Calibration/regression evidence](builds/validation/landscape-cloud/optimization-phase9/production-timing-calibration-pass.json),
[20-control results and stop evidence](builds/validation/landscape-cloud/optimization-phase9/twenty-controls-stop.json),
[install/hash proof](builds/validation/landscape-cloud/optimization-phase9/production-timing-build.json).
Large artifacts remain on D:. Stop on failure; no Gaffer before gates pass.

[Support matrix](src/deep/RELEASE_MATRIX.md), [M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
