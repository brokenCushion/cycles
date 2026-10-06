# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phase 0 is accepted: both builds, nine CTests, CPU/CUDA compatibility matrices,
boundary suites and K=5 landscape checks pass. Strict payload and deterministic
headers are identical under the user-approved run-metadata exclusion. Results
are in Section 6 of the plan.

Phase 1 is accepted: sequential band export, nine CTests, CPU/CUDA matrices,
boundary suites and both fixed landscapes pass. Strict data/headers are unchanged.
587x250 export: 49.26 -> 34.52 s; spill reads: 3.85 -> 0.60 GB (1.15x amplification).
The small case exports in 8.30 s vs 7.05 s; no speedup is claimed there.
Phase 2 is next, after the phase summary is reviewed.

Checkpoint: `59963b932`. The beauty majorant experiment is isolated on
`codex/volume-majorant-determinism` (`9a017f055`) and removed here (`44b44477e`).
Deep RGB remains deferred. No Phase 9 production run before Phases 0–8 pass
and the user confirms.
