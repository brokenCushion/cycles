# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–5 accepted; Phase 6 unaccepted.

IDs export (`df3d3d060`): 179.58 -> 3.88 s on 587x250x4 / 1e-3,
46.3x faster, 1.72x IDs-off. Five existing IDs-on outputs whole-file identical;
matched IDs-off identical; nine prior CTests pass. Renderer/budgets unchanged.

Final CUDA raw policy recorded in plan Section 2 and implemented in validators.
Existing realistic117 1e-4/all fails: albedo at (99,7) reaches 0.1613 SE (>0.1),
and 15/5,850 pixels need statistical fallback (0.2564% >0.1%). No thresholds changed.
Every channel's bias check passes; noisy RGB at (99,7) passes. CPU stays exact.
Unit checks pass; saved-input full-image policy replay records this failure.

Stopped before three remaining realistic pairs, fresh 81+30 identity replay,
and final full regression replay. No 6a/6b/Phase 9 started.
Result: `builds/validation/landscape-cloud/optimization-phase6/review/final-raw-policy-existing.json`.
