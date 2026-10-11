# Deep output validation policy

This is the final approved policy extracted from Section 2 of the completed
optimization plan. Organization has changed; gates, thresholds and references
have not. The full decision history is recoverable through the
[archive index](ARCHIVED_REPORTS.md).

Phases 0–9 are accepted. [Production evidence](evidence/README.md) records both
full-resolution OptiX outputs, measurements, build identity and the final
root-cause resolution. [Support](SUPPORT.md) defines the qualified scope.
Use the [regression guide](../../src/deep/README.md#regression-command) to run checks.

## Required proof

- Each output passes independent oracles and depth cuts within its EXR header
  bound. Positive surface z tolerance approximates depth-band interiors;
  combined alpha and curves outside those bands retain their checks.
- CPU deep-on/off beauty is exact within the same build. Beauty sources remain
  unchanged, with all 75 common kernel resource records checked.
- Strict/numeric identity references belong to a recorded build and backend.
  The accepted replay is 81/81 strict, 30/30 numeric and 65/65 OptiX SVM,
  plus nine CTests and the regression command.
- GPU-side changes also require the CUDA/OptiX beauty policy below, independently
  for each backend with compatible controls. Host-only proof and scope are
  preserved in that policy.
- Deep capture remains a side channel: do not change beauty kernels, sampling
  or RNG. Deep GPU kernel/readback/spill time is excluded from beauty scheduler
  timing inputs by the qualified host integration fix.
- Capacity, cancellation, invalid data and publication failures are explicit.
  Never change a gate, threshold or reference to make a failure pass.

## Same-build strict identity

Strict mode must reproduce today's behaviour (`--deep-error strict`). Strict
identity means bit-identical deep payload (sample counts, Z, ZBack, A and any
other data channels) and deterministic headers. Exclude only run metadata:
`cycles:beautyIdentity` and attributes recording paths, dates or run IDs.
Data-describing headers remain mandatory, including `deepError`, `deepSamples`,
`deepScope`, manifests, channel types and compression. The comparator uses an
explicit run-metadata allowlist; unknown attributes are compared, not guessed.
`tools/compare_deep_identity.py` checks every encoded count/pixel chunk and
every deterministic header byte; files are never rewritten for comparison.

## Cross-build and cross-backend comparisons

Every comparison across different builds, toolchains or backends uses one rule:

1. Each side must pass its own gates: independent oracle/depth cuts within its
   EXR header bounds, same-build/backend beauty policy and rerun identity.
   CPU deep-on/off beauty remains exact; CUDA/OptiX use the policy below.
2. Maximum cross-side flattened-alpha difference must be <= 1e-4 per case.
   Native-backend waiver (user decision, 2026-10-09): waive this bound only
   when pristine Blender without deep reproduces a beauty-alpha difference
   of the same size between those backends AND each backend's deep flattened
   alpha matches its own beauty alpha within the existing header bound.
   Report pristine beauty and deep differences together. All other cases
   retain 1e-4. Known case: OSL texture-driven opacity can differ between CPU
   and OptiX by up to ~1e-3, as in native Blender (CPU OpenImageIO textures
   versus OptiX Cycles images). Own oracles, beauty and identity stay mandatory.
3. Cross-side curves, depth shifts and deep sample counts are informational.
   Byte/curve identity applies only within the same build and backend.

This replaces all previous cross-build/backend curve ceilings, depth-window
acceptance rules and strict backend equality requirements. Independent bounds
and beauty gates remain unchanged. OptiX hardware intersections compute surface
t differently from CUDA BVH2; near-coincident hits can change discrete outcomes
across compilers. The opaque-foreground comparison therefore passes: flattened
alpha identical, both own oracle maxima 1.49e-8; curve/depth differences recorded.

The recorded clang-cl 20.1.8 / NVCC 12.8.61 (CUDA 12.8.0) pairing, matching
the buildbot, gets separate CPU/CUDA references after its one-time audit and
new-build qualification. Keep this pairing and retain cl.exe/previous NVCC
references for history. Record all 81 strict + 30 numeric cross-build cases and
the 33x17 raw-capture diagnostic. The new build must pass nine CTests and the
regression command, including same-build identity, CPU exact beauty and the
CUDA/OptiX beauty policy. Promote only after those gates pass, then replay
81/81 + 30/30 identity. Candidate references may be staged for qualification;
do not replace the accepted default configuration beforehand.

## CUDA/OptiX raw beauty policy

This supersedes the historical Phase 0 CUDA raw envelope/search policy and
the uncalibrated 0.1-sigma / 0.1% guesses. Do not revise this policy again.
CPU stays exact equality and the primary proof, together with unchanged
beauty-source hashes and kernel resource records. No beauty kernel change.
CUDA/OptiX pixel beauty qualification is required only for GPU-side changes
(kernels, GPU scheduling or buffers): Phase 6b if capture changes, 8 and 9.
Phase 8a applies this same policy independently to CUDA and OptiX, with
controls from the matching backend; never use CUDA controls for OptiX.
For host-only phases 1, 3a, 6, 6a, 6b (when capture is unchanged) and 7,
unchanged beauty-source hashes,
all 75 common CUDA kernel resource records and CPU exact equality are the
required proof. Phase 6a is host-only (user decision); its saved CUDA pixel
comparisons are informational, never a blocker. Do not revisit this scope.

1. A reproduced state passes unchanged: all checked raw passes match ONE
   unchanged-beauty pool render within 4 FLOAT ULP, with the same sample count.
2. Calibrate step 2 from compatible deep-off renders, never from deep-on.
   For every pool render, leave it out and compare it against the rest with
   precisely the same whole-state/count match. For fallback pixels, measure
   each pass/channel's absolute nearest same-count difference / sigma_pixel,
   using the same four distinct-seed deep-off controls. sigma_pixel is their
   pixel sample SD / 2. Each channel's allowed absolute difference is
   max(calibrated channel limit * sigma_pixel, step-2 ULP floor).
   User decision (2026-10-09): denoising normal and albedo average per-sample
   components bounded by 1, so their step-2 floor is 4 FLOAT ULP of 1.0
   (4.76837158203125e-7), even when cancellation makes the average near zero.
   Noisy colour and other passes retain 4 FLOAT ULP of the nearest same-count
   reference value. Zero sigma uses this floor. Step 1 remains unchanged. Pixels
   without a same-count reference use the separate count-mismatch rule below;
   exclude them from matched raw fallback calibration and bias. Nonfinite input
   fails. Seed-varied counts may differ only for
   estimating sigma. Record each control's fallback count and channel maxima.
   Deep-on's fallback count must be <= the largest leave-one-out count;
   every channel must obey the absolute bound above. Record ratios, ULP floors
   and absolute limits. Calibration requires at least 20 compatible ordinary
   controls for that fixture; with fewer, step-2 results are informational
   only and cannot qualify a GPU-changing phase. Report deep-off min/median/max
   beside deep-on; report every fallback pixel/channel. Do not revisit these
   user-approved consistency and minimum-control rules.
3. Image-wide bias test (final user decision, 2026-10-08): a channel fails
   only when abs(mean signed difference) > 3 standard errors AND exceeds
   1 FLOAT ULP of that channel's image-mean reference value. Subtract the
   MEAN of each pixel's same-count pool references (not nearest); compute
   signed mean and sample SD / sqrt(matched pixel count) of these paired
   residuals. The reference image mean averages those same per-pixel reference
   means. Record both limits, the reference mean and its FLOAT ULP. Nonfinite
   data fail. CPU exact equality is unchanged.

   Count-mismatch rule (user decision, 2026-10-09): report missing same-count
   references separately, never as a validator exception. For each of the five
   ordinary controls, count unmatched pixels against the other four. For
   deep-on, count against each four-control subset and average the five counts.
   This mean must not exceed the maximum control leave-one-out count.
   Direction uses one observation per deep-on pixel unmatched against all five,
   compared with their modal sample count. Controls use the other four's mode;
   exclude 2-2 ties (and any other modal ties), reporting exclusions. Report the
   fraction taking fewer samples, the control fraction range, and count-difference
   histograms. Direction fails only if deep-on is outside that range AND its
   two-sided binomial test against 50% has p < 0.001. Matched-count pixels retain
   all existing raw gates, including the 20-control requirement for statistical
   fallback. If either count gate fails, stop and report without investigation
   or additional renders. This calibration uses five controls, independently
   of the matched raw fallback calibration.

4. Root-cause resolution (user decision, 2026-10-07): a pixel flagged by the
   calibrated rule is resolved if deep-on and deep-off in the separate
   majorant-snapshot diagnostic build agree on ALL checked raw passes there.
   Agreement uses the unchanged reproduced-state rule: each diagnostic deep-on
   matches ONE diagnostic deep-off across all channels within 4 FLOAT ULP and
   identical accepted counts. Report bit identity separately, never claim it
   when only this existing agreement rule passes. Verify six independent
   renders, diagnostic executable and matching beauty/camera/sampling case.
   Record both target and diagnostic deep capture options; this resolves beauty
   only and never substitutes for each target's deep-alpha qualification. Keep the
   patch diagnostic-only and out of both the deep branch and ordinary pool.
   Preserve the original calibrated flags/results and record each resolution.
   Unresolved pixels or any unchanged bias/count gate failure stop work for
   investigation; this adds no numerical threshold or renderer change.

User's statistical note: under exchangeability, exceeding 31 leave-one-out
maxima occurs roughly 1/32 per independent measure. Twelve channels plus a
count give multiple opportunities; albedo R/G/B describe one correlated
event. A single flag is therefore not itself proof of a deep-to-beauty effect.

The pool accumulates compatible ordinary controls across phases; verify source
and executable identities. Calibration uses no new renders where controls and
the four seed estimates already exist (31 controls for realistic117). Reuse
those estimates for remaining pairs of the same case/settings. If calibration
needs four seeds for a new case, create them once, never an open-ended search.
The historical K=5 envelopes remain diagnostic evidence; the denoised
explanation rule is unchanged. A deep-on result outside the calibrated range
in a GPU-changing phase first uses the diagnostic root-cause step, then
stops if unresolved; host-only phases use the proof stated above.
CPU/deep budgets unchanged.

## Denoised beauty

Use K = 5 ordinary deep-off runs. Denoised beauty: the envelope is the
image-wide maximum over all pairs of those runs, never a per-pixel envelope. Retain qualified
ordinary-run evidence across phases with unchanged beauty kernels.
Report the exact-saved-input GPU OIDN repeat difference separately; if
OIDN alone reproduces the variation, denoised pixels whose raw inputs pass
the unchanged raw gate are explained (user decision, 2026-10-07).
It is not a pass/fail gate by itself, BUT any denoised difference outside
the envelope must be explained by locating the pixels and showing which
input pass differs. If a denoiser input pass differs beyond the raw gate,
that is a real deep-to-beauty leak and must be fixed.

## Shader-evaluated volume checks

Initialize every field passed to shader evaluation, on every device. Deep grid
and texture evaluation uses the deterministic interpolation whose expectation
beauty samples stochastically; beauty evaluation stays unchanged. Repeating the
same step produces byte-identical curves on CPU and OptiX.

Demonstrate midpoint convergence against an independent CPU integral of the
same shader on selected captured rays at h/64 or finer, without an event cap.
For adaptive step doubling, compare optical depth at s and s/2, reusing
evaluations. Accept when T_prefix × |tau_s - tau_s/2| is within the interval's
share of the device allowance; otherwise subdivide. Capacity or step-count
exhaustion fails explicitly. The explicit volume step is the starting/maximum
step.

Numeric error E assigns E/2 to stepping (stated, not proven) and E/2 to bounded
representation/fitting/publication. EXR headers identify the method, actual
world-unit steps, selection rule and allowances. Features narrower than the
finest evaluated step can be missed. Each fixture must stay within its header
bound against a 4x-finer fixed-step reference. Native analytic VDB paths retain
same-build/backend byte identity.

## Accepted production exception

The user approved one existing snapshot+timing-fix diagnostic off/on pair for
the all-samples production flag at (992,78), replacing the general six-render
requirement for that flag only. It required identical accepted sample counts
and all 12 raw channels within the recorded floors: normal/albedo 4 ULP of 1,
other channels 4 ULP of the reference. The pair passed, also satisfying the
stricter reference-value reproduced-state rule. It resolves beauty only; both
production outputs separately passed their deep-alpha gates. Original flags
and resolution reports remain separate in the [evidence](evidence/README.md).

## Validation workflow

Accumulate all compatible ordinary controls; never select a favorable pool.
Verify source/executable identity and matching camera, sampling and backend.
Small policy-required control/seed renders (about five minutes or less) are
pre-authorized; report additions. Full-resolution renders and renderer,
gate, threshold or reference changes require specific authorization.
Pure parser/import/report measurement repairs may be fixed and tested; rerun
only the affected analysis on preserved outputs and report the repair.

Keep large EXRs, samples CSVs, spill, TEMP/TMP and caches on D:. Only small
JSON/text summaries belong under builds/. Never delete through junctions.
A failed renderer or gate stops work without retry or further investigation
unless the user explicitly authorizes it.
