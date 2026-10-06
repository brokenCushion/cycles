# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0–2 are accepted. Phase 3a acceptance checks pass: shared host error
setting/budgets, headers, validators and surface reduction/coalescing.
Strict retains all 81 payload/header identities. Nine CTests, all three-mode
CPU14/CUDA4 matrices, both boundary suites and both fixed cases pass.
CPU fitting breakdowns, storage and accuracy are in plan Section 6.
Gaffer review: `builds/validation/landscape-cloud/optimization-phase3a/after/performance/1e-4/native_vdb_review.gfr`.

Stop for 3a review. Next, 3b must document and explain the device compression
bound before implementation, with independent exact-cubic CPU/CUDA tests.
3c carries object indices without changing output and has its own review stop.
Deep RGB is deferred. No Phase 9 production run before Phases 0–8 pass
and the user confirms.
