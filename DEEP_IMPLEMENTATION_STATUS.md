# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-4 are accepted. Phase 5 passes its checks and is at its review stop.
Deep sample limits retain the first N accepted beauty cameras, including misses.
N=0 preserves payload/headers; positive N records its effective maximum in EXR.
Beauty sampling is unchanged. Oracles read the limit from the header and compare
that prefix; sampling differences against all cameras are informational.
Strict 81/81; numeric 30/30 identical. Nine CTests, all-mode CPU/CUDA matrices,
both boundary suites, CUDA lifecycle and extra cap=1 matrices pass. Deep64 with
128 beauty samples passes alpha/depth cuts and raw/denoised beauty isolation.
Phase 6 and the full-resolution production run have not started.

Evidence: `builds/validation/landscape-cloud/optimization-phase5/after/phase-results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase5/after/prefix128/Phase5_SampleCount_Review.gfr`.
