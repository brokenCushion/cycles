# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0 and 1 are accepted. Phase 2 acceptance checks pass: native CUDA BVH
boundary candidates reduce 587x250/four-sample capture from 71.65 to 33.28 s
(2.15x); export remains 32.94 s. All 81 strict payload/header comparisons,
nine CTests, CPU14/CUDA4 matrices, both boundary suites and both landscapes pass.
CPU beauty is exact; CUDA follows the approved reference-pool rule.
Profiling, resource costs and the resolved pixel are in plan Section 6.
Gaffer review: `builds/validation/landscape-cloud/optimization-phase2/after/performance/deep/native_vdb_review.gfr`.

Next: Phase 3 error budgets and device compression, with a separate CPU
curve-fitting time breakdown before/after. Stop for phase review first.
Deep RGB is deferred. No Phase 9 production run before Phases 0–8 pass
and the user confirms. The beauty majorant experiment remains isolated on
`codex/volume-majorant-determinism`.
