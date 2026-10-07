# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-6 and 6a accepted.

Controlled native-config export replay: Phase 6 -> current, IDs off
109.80 -> 108.75 s; IDs on 154.47 -> 138.08 s. Outputs and sampled raw-camera
CSVs are identical. No build slowdown reproduced; no performance fix needed.
[Report](builds/validation/landscape-cloud/optimization-phase6a/export-recheck-native/results.json).

6b is host-only: preflight admits holdout; the output-driver API carries an
optional holdout ID manifest. GPU capture/beauty sources remain unchanged.
Six old-build fixture rejections and the fixed-case before baseline are recorded.
Implementation is awaiting build and qualification; no Phase 7/full-resolution run.
Large validation outputs/TEMP stay on D:.
