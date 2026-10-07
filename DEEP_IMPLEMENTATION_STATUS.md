# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-3c are accepted. Phase 4 passes its checks and is at its review stop.
Numeric capture uses bounded flat event/density ranges and two pinned queues.
Events are 20 bytes; the object index shares the kind word. Strict retains its
retry path. GPU output, spill and error bounds preserve existing data.
Strict: 81/81 identical; numeric: 30/30 identical. Both fixed-case beauty gates,
nine CTests, all-mode CPU/CUDA matrices, both boundary suites and CUDA
cancellation/error cleanup pass. Staging stays within 32 MiB; GPU peak below 8192 MiB.
Phase 5 and full-resolution production validation have not started.

Evidence: `builds/validation/landscape-cloud/optimization-phase4/after/phase-results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase4/after/performance/1e-3/native_vdb_review.gfr`.
