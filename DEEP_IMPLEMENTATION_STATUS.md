# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-3a are accepted. Phase 3b rework is in qualification, not accepted.
The plan records no-expansion compression, exact cubic fallback and bounded
opaque-tail clipping. Raw beauty gates remain unchanged.
Independent CPU/CUDA cubic/no-expansion tests and nine CTests pass.
The previous denoised check passes with the retained image-wide K-run envelope.
Identical saved input passes denoised twice with CUDA OIDN give max difference 0.
Fixed-case performance, full 81/81 strict identity and regressions remain pending.
No Phase 3c or full production run.

Evidence: `builds/validation/landscape-cloud/optimization-phase3b-rework/`.
