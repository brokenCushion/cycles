# Cycles deep output: current state

The approved [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record.
[Peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phase 0 checkpoint: `59963b932`. The beauty majorant experiment is isolated on
`codex/volume-majorant-determinism` (`9a017f055`) and removed here (`44b44477e`).
CUDA K=5 qualification: `7efb85198`. Both landscapes pass raw-input, accepted
population, alpha and depth checks. Nine CTests, 14 CPU / 4 CUDA compatibility
cases, and both 33-render / 17-rejection boundary suites pass.

Landscape and compatibility deep EXRs are byte-identical. Boundary sample data
is bit-identical, but whole files differ in run-specific beautyIdentity headers.
Literal boundary byte identity conflicts with timestamp-dependent beauty pairing.
The user decision on that contract is pending; Phase 0 is not marked fully accepted. Measurements and the historical denoised-pixel investigation are in
Section 6 of the plan. Phase 1 has not started.

M8 remains open. No production render starts before prerequisite phases pass
and the user confirms. Deep RGB remains deferred.
