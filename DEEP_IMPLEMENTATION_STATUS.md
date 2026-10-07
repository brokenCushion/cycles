# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–5 accepted; Phase 6 in qualification.

IDs export (`df3d3d060`): 179.58 -> 3.88 s, 46.3x faster, 1.72x IDs-off.
Five existing IDs-on EXRs and matched IDs-off whole-file identical; nine prior
CTests pass. Renderer/deep budgets unchanged. CPU stays exact.

Calibrated flag at realistic117 (99,7) resolved by approved diagnostic root-cause
step: all raw channels match one snapshot off-control within existing 4 ULP,
counts exact. RGB exact; albedo/normal/depth differ by 1–3 ULP, not bit-identical.
Original calibrated evidence retained; all bias channels pass. Patch stays
diagnostic-only. No new diagnostic renders or threshold changes.

Completing original oracle/IDs qualification, three remaining realistic pairs,
fresh 81+30 identity and full regression replay. Stop before 6a for Phase 6 review.
Evidence: `builds/validation/landscape-cloud/optimization-phase6/review/root-cause-raw-policy-existing.json`.
