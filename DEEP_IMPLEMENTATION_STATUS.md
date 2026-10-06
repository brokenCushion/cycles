# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-3a are accepted. Phase 3b is implemented but not accepted.
Its bound, tests and measured results are in plan Section 6.
Independent exact-cubic CPU/CUDA tests and nine CTests pass.
Small strict identity passes (1/81 checked); alpha/depth-cut checks pass.
Small 1e-4 fails the existing denoised-beauty gate (75 unexplained pixels).
Capture time and readback/spill bytes regress. Qualification stopped.
The larger fixed case, compatibility matrices and boundaries remain unrun.
No Phase 3c or full production run; no gates or tolerances were relaxed.

Evidence: `builds/validation/landscape-cloud/optimization-phase3b/halt-results.json`.
Candidate Gaffer review: `builds/validation/landscape-cloud/optimization-phase3b/after/small/1e-3/native_vdb_review.gfr`.
