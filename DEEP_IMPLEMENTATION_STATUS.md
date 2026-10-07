# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–5 accepted; Phase 6 awaits review.

IDs export optimization (`df3d3d060`): 179.58 -> 3.88 s on 587x250x4 / 1e-3,
46.3x faster, 1.72x matched IDs-off. Five existing IDs-on EXRs remain whole-file
identical, as does matched IDs-off. Nine CTests pass. Budgets/gates unchanged.

Original realistic raw beauty at (99,7) remains a policy review item. Separate
majorant-snapshot diagnostic makes its noisy RGB identical across all six runs,
but one ON run fails image-wide at (5,5); two pass. The patch is absent from the
deep branch and managed beauty source; this K=3 evidence is not qualification.

Stopped as requested before three remaining realistic pairs and final regression
replay. No 6a/6b/Phase 9 started.
Results: `builds/validation/landscape-cloud/optimization-phase6/review/review-results.json`.
