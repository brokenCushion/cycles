# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–5 accepted; Phase 6 unaccepted.

IDs export (`df3d3d060`): 179.58 -> 3.88 s, 46.3x faster, 1.72x IDs-off.
Five existing IDs-on EXRs and matched IDs-off remain whole-file identical;
nine prior CTests pass. Renderer/deep budgets unchanged. CPU stays exact.

Final calibrated CUDA policy replaces guesses in plan Section 2 and validators.
Realistic117 1e-4/all fails at (99,7): albedo ratios 0.153–0.161 exceed
31-control leave-one-out maxima 0.132–0.140. Fallback count passes (15 <=21);
all 12 image-bias channels pass. No new renders or policy relaxation.
Unit checks and complete saved-input calibration replay verify the stop.

Stopped as a possible deep-to-beauty effect, without revising the final policy.
Three remaining realistic pairs, fresh 81+30 identity and final full regression
replay remain pending. No 6a/6b/Phase 9 started.
Result: `builds/validation/landscape-cloud/optimization-phase6/review/calibrated-raw-policy-existing.json`.
