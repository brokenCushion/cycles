# Cycles deep output: current state

The [optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`.

Phases 0-4 are accepted. Phase 5 implementation is accepted; its realistic
landscape comparison passes and is at its review stop.
Deep sample limits retain the first N accepted beauty cameras, including misses.
N=0 preserves payload/headers; positive N records its effective maximum in EXR.
Beauty sampling is unchanged. Oracles read the limit from the header and compare
that prefix; sampling differences against all cameras are informational.
Strict 81/81; numeric 30/30 identical. Nine CTests, all-mode CPU/CUDA matrices,
both boundary suites, CUDA lifecycle and extra cap=1 matrices pass. Original
max1024 adaptive / GPU OIDN passes alpha/depth cuts and raw/denoised beauty
isolation for all and deep64.
117x50 with original max1024 adaptive / GPU OIDN: all cameras versus deep64
measured; timings, storage, full-image accepted populations and full-resolution
estimates are in plan Section 6. Phase 6/full-resolution work has not started.

Realistic results: `builds/validation/landscape-cloud/optimization-phase5/realistic117/results.json`.
Regressions: `builds/validation/landscape-cloud/optimization-phase5/after/phase-results.json`.
Gaffer: `builds/validation/landscape-cloud/optimization-phase5/realistic117/Phase5_Realistic117_Review.gfr`.
