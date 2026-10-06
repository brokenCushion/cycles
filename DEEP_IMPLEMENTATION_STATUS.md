# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phase 0 is accepted: both builds, nine CTests, CPU/CUDA compatibility matrices,
boundary suites and K=5 landscape checks pass. Strict payload and deterministic
headers are identical under the user-approved run-metadata exclusion. Results
are in Section 6 of the plan.

Phase 1 baseline is accepted under the reproduced-state rule: pixel (489,34)
matches all checked passes in Phase 0 deep-off control 3 with population 4.
Banded spill implementation and its regression checks are in progress.

Checkpoint: `59963b932`. The beauty majorant experiment is isolated on
`codex/volume-majorant-determinism` (`9a017f055`) and removed here (`44b44477e`).
Deep RGB remains deferred. No Phase 9 production run before Phases 0–8 pass
and the user confirms.
