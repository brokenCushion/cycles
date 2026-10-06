# Cycles deep output: current state

The approved [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record.
[Peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Development branch: `codex/landscape-cloud-compatibility`.

Phase 0 cleanup is committed (`44b44477e`); acceptance failed on the small
landscape's raw CUDA beauty gate. Difference: 2.38418579e-7; independently
measured repeat envelope: 1.78813934e-7. Denoised beauty is exactly identical.
Deep EXR bytes, alpha and depth checks pass. Phase 1 has not started.

Both builds, nine CTests, 14 CPU / 4 CUDA compatibility cases, and both
33-render / 17-rejection boundary suites pass. Measurements and identity
limitations are recorded in Section 6 of the plan.

Checkpoint `59963b932` preserves the previous work. The removed beauty-sampling
experiment lives on `codex/volume-majorant-determinism` (`9a017f055`).
Deep output remains Z/ZBack/A, with separate beauty; RGB is deferred.
M8 is open. Production rendering requires the prerequisite phases to pass
and explicit user confirmation.
