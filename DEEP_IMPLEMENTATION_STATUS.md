# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a is stopped, unqualified: strict CPU legacy identity failed.**
Both deep builds now use clang-cl 20.1.8, CUDA 12.8.0 and OptiX 8.0.0 with
native precompiled modules. Pristine AO rendering passes; deep qualification
stopped before CUDA/OptiX controls and full regression. Three deep headers now
spell their FLOAT-to-double conversions explicitly for Clang's existing checks.
Beauty sources/flags and every gate remain unchanged.

CPU half_precision (32x32/four samples): deep-on/off beauty and all stored
passes are exact; beauty-source hash and all 75 resource records match the
accepted baseline. Legacy strict samples change 118,775 -> 118,777, with count
changes at 44 pixels; deterministic headers match. Whole-curve transmittance
difference is 6.86099e-7, but this does not satisfy strict byte identity.
[Evidence and pending checks](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).
Full 81/81 + 30/30 replay, nine CTests, matrices/boundaries, GPU beauty and
after measurements remain pending. The raw-pass harness fix is retained.
No Phase 8b/8c or full-resolution Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is restored
verbatim; [13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:.
