# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-3b are accepted. Phase 3c passes its checks and is at its review stop.
Object indices survive device capture, GPU readback, spill and staged host reads.
Reconstruction ignores the indices; output and error budgets are unchanged.
Strict payload/headers: 81/81 identical. Both numeric settings also match on
both fixed cases. Nine CTests, three-mode CPU/CUDA matrices, both boundary
suites and independent CPU/CUDA cubic/object-index tests pass.
The 24-byte event increases temporary storage; staging remains within 32 MiB.
Phase 4 will report lane-written bytes separately from copied bytes.
No full production render has started.

Evidence: `builds/validation/landscape-cloud/optimization-phase3c/after/phase-results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase3c/after/performance/1e-3/native_vdb_review.gfr`.
