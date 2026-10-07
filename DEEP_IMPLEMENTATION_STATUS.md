# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-6 and 6a accepted.
6b qualification passed; awaiting review before Phase 7.

Controlled Phase 6 -> 6a export replay: IDs off 109.80 -> 108.75 s;
IDs on 154.47 -> 138.08 s. Payload/headers and sampled camera CSVs identical;
no slowdown reproduced, no renderer fix needed.

6b admits native object/material holdout opacity and marks IDs with a checked
manifest subset. GPU/beauty sources and 75 resources unchanged; CPU beauty exact.
60 holdout cases, nine CTests, full matrices/boundaries/lifecycle and
strict 81/81 + numeric 30/30 identity pass. Bounds/limits unchanged.
[Results](builds/validation/landscape-cloud/optimization-phase6b/phase-results.json).
Gaffer: `D:/CyclesDeepScratch/optimization-phase6b/holdout_review.gfr`.
Large outputs/TEMP stay on D:. No Phase 7/full-resolution run launched.
