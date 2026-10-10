# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `deep`. **Phases 0–9 accepted.**

Both production landscape outputs qualify: OptiX 1175x500, original1024 adaptive,
GPU OIDN, IDs, error1e-3, default z1e-4; all samples and prefix64. Run1's sole
calibrated raw flag `(992,78)` is resolved by the approved existing snapshot+
timing-fix diagnostic pair: identical352 sample counts and all12 raw channels
within recorded floors, also passing reference-value4ULP. Bit identity is not
claimed. Snapshot remains diagnostic-only; original reports are preserved.

Regression passes81/81 strict,30/30 numeric,65/65 OptiX, nine CTests, CPU beauty
exactness, native beauty-source proof and75 kernel resource records. Production
uses the host timing fix, no snapshot/debug patch, and the accepted
[toolchain](BUILDING.md); protected files remain unchanged. Superseded standalone
binaries remain recorded as directed; golden outputs are preserved.

[Acceptance, metrics and actual production paths](builds/validation/landscape-cloud/optimization-phase9/phase9-acceptance.json),
[root-cause diagnostic](builds/validation/landscape-cloud/optimization-phase9/run1-root-cause-pair.json),
[connected Gaffer review](builds/validation/landscape-cloud/optimization-phase9/gaffer-production-review.json),
[historical calibration stop](builds/validation/landscape-cloud/optimization-phase9/twenty-controls-stop.json),
[install/hash proof](builds/validation/landscape-cloud/optimization-phase9/production-timing-build.json).
The saved graph links both actual production deep EXRs, matching beauty, depth
cuts and camera-based DeepToPointCloud previews. Opened for final user review.
Large artifacts remain on D:. No further renders or feature work planned.

[Support matrix](src/deep/RELEASE_MATRIX.md), [M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
