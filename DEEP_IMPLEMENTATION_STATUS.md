# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-6b accepted.
Phase 7 qualification passed; awaiting review before Phase 8.

Default regression: 587.66 s including owned-output cleanup
(baseline 996.57 s). Nine CTests, strict 81/81 + numeric 30/30,
CPU beauty exact, source proof and 75 unchanged resources pass. Renderer unchanged.
CUDA beauty is a separate optional stage; SDK/Gaffer reader parity passes.

1021 historical sample CSVs replaced by fully verified ZIPs;
45.17 GiB allocated space saved. Fourteen obsolete
validation reports archived in Git history. Core footprint and run command:
[README](src/deep/README.md). [Results](builds/validation/landscape-cloud/optimization-phase7/phase-results.json).
Gaffer: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:. Phase 8/full-resolution Phase 9 not launched.
