# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-3a are accepted. Phase 3b passes acceptance checks and is at its review stop.
No-expansion compression, exact cubic fallback and bounded opaque tails are implemented.
Strict payload/headers: 81/81 identical. Nine CTests, CPU/CUDA matrices in all
three modes, both boundary suites and independent cubic/no-expansion oracles pass.
Fixed-case capture, readback and spill gates all pass; fitting breakdown is in
plan Section 6. The larger 1e-4 EXR grows about 5% and requires more host fitting.
Raw beauty gates are unchanged. Saved-input CUDA OIDN repeat difference is zero.
No Phase 3c or full production render has started.

Evidence: `builds/validation/landscape-cloud/optimization-phase3b-rework/after/phase-results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase3b-rework/after/performance/1e-3/native_vdb_review.gfr`.
