# Cycles deep output: current state

The approved [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record.
[Peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Development branch: `codex/landscape-cloud-compatibility`.

Phase 0 is in progress. Checkpoint `59963b932` preserves the previous work.
The volume-majorant snapshot experiment is isolated in
`codex/volume-majorant-determinism` (`9a017f055`) and removed here.
Deep output remains Z/ZBack/A, with separate beauty. Deep RGB is deferred.

The Phase 0 baseline passes nine CTests, the 14 CPU / 4 CUDA compatibility
matrices, both 33-render / 17-rejection boundary suites, and the small landscape.
The 587x250/four-sample baseline passes alpha and depth checks but fails
denoised beauty isolation. Before/after evidence belongs in Section 6 of
the plan and `builds/validation/landscape-cloud/optimization-phase0`.

M8 is open. Full-resolution production rendering requires Phases 0-5 to pass
and explicit user confirmation for Phase 6. The interrupted original-settings
run remains unqualified; its partial output is preserved.
