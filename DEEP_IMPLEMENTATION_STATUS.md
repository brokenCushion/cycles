# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-6 accepted.

Phase 6a is implemented; qualification resumed with the approved host-only proof.
On the rendered sloped16/1024 fixture, CPU IDs-off/on surface samples fall
58.7%; IDs-on EXR bytes fall 41.5%. Flattened alpha is exact, exterior error
is below 2.11e-9, and CPU beauty is exact. Nine CTests, beauty source hashes
and 75 common CUDA resource records pass. Saved CUDA flags were re-evaluated without renders: four normal pixels pass
the reference ULP floor; one albedo pixel is informational (five controls).
Section 2 records the user's final gate scope and calibration rules.

The eight Phase 6 landscape pairs are the recorded before baseline; fresh
landscape measurements and 81/81 + 30/30/full regression replay are pending.
Large artifacts/TEMP: `D:/CyclesDeepScratch/optimization-phase6a/`.
Small reports: `builds/validation/landscape-cloud/optimization-phase6a/`.
No Phase 6b or full-resolution production run launched.
