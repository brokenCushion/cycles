# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0–5 are accepted. Phase 6 (deepID) is implemented but **not qualified**.
Optional UINT `id` and its object-name manifest preserve overlapping objects.
Strict + IDs is unsupported and explicitly rejected before rendering. Nine
CTests pass, including surface/volume preflight rejection and overflow counts.
The installed standalone rejection creates no EXR. Capacities and gates are unchanged.

Numeric qualification stopped on the first 47x20x16 CUDA IDs-on case at 1e-4:
per-object FLOAT publication error 1.36069e-6 exceeds its 1.16864e-6 allowance.
No interval-cap failure occurred; no EXR was published. Remaining requested
numeric pairs and the final full regression replay are pending.
Earlier IDs-off evidence remains strict 81/81 and numeric 30/30 identical;
known-overlap exact UINT selection and its connected Gaffer graph passed.

**Stopped under the failed-acceptance rule, before 6a/6b.** Phase 9 requires
separate user confirmation; run all samples then 64, at 1e-3, TEMP/TMP on D:.
Results: `builds/validation/landscape-cloud/optimization-phase6/results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase6/known-overlap/final-CPU/known_overlap/deep/Phase6_Known_ID_Review.gfr`.
