# Deep output optimization plan

Status: approved direction (2026-10-06). The user is not a renderer developer and
has accepted these recommendations as the plan of record. Implement phases in
order. Each phase is one or more focused commits with before/after measurements.

## 1. Why

Capture is correct and well validated. The problem is data volume: every camera
sample stores every VDB cell it crosses (up to 8192 records x 52 bytes) at
near-lossless precision (~1e-7 total transmittance error), and all reduction
happens on the host after rendering.

Measured on the full landscape (`builds/validation/landscape-cloud/original-resolution-CUDA/CUDA/deep-ledger-bounded-AgX/process.log`):

| Stage | Measurement |
| --- | --- |
| Beauty only | ~400 s |
| Deep capture | ~13,100 s render; the 70 `Deep CUDA capture` log lines sum to ~11,800 s of `capture_readback_seconds` (kernel wait + copy) |
| GPU readback | ~13.6 GB per render pass, ~3,450 synchronous batches per pass |
| Export | 165,570 s (46 h), 4 workers |
| Spill | `spill_file_bytes=254,184,856,420`, `spill_read_bytes=4,601,353,434,020` (18x read amplification, ~28 MB/s) |

Root causes, in code:

1. **Spill layout vs. read order.** `Capture::store_record` (`src/deep/capture.cpp`)
   appends events to one tmpfile in render order (sample-major). Export reads
   pixel-major (`Capture::reconstruct_volume_pixel` -> `volume_sample`), so each
   pixel's ~1024 samples are scattered across 254 GB, read through 16 x 64 KiB
   pages, and every read holds the single `mutex_` (`volume_sample`), serializing
   all export workers on I/O.
2. **GPU batch loop** (`PathTraceWorkGPU::capture_deep_tiles`,
   `src/integrator/path_trace_work_gpu.cpp`): enqueue -> copy -> `synchronize()`
   per batch, no overlap; readback size is `max(lane count) x batch_size`
   (plane layout) so one dense lane inflates the copy; overflowing lanes are
   retraced at 64 -> 256 -> 1024 -> 4096 -> 8192 slots; only ~10% of launched
   lanes are active (records ~1M vs skipped ~8.4M per pass).
3. **Brute-force boundary pairing.** `deep_volume_object` (`src/kernel/deep/volume.h`)
   tests every triangle of the volume object, in double, for every crossing:
   O(crossings x triangles) per ray per sample. Not yet measured.
4. **Near-lossless tolerance everywhere.** `volume_density_error = 1e-7`,
   `volume_reconstruction_error = 5e-8`, `volume_coefficient_error = 4e-8`
   (`src/deep/volume.h`). With 1024 samples the balanced tree gives each pair
   merge ~2.5e-9. Interval count scales ~1/sqrt(tolerance) under the existing
   curvature bound, so this drives interval counts, the 65,536-interval
   overflow, spill size and export time.
5. **Deep uses every beauty sample.** Deep rays are analytic (noise-free); only
   subpixel position, DOF and motion vary. 1024 accepted samples is far more than
   alpha needs.

## 2. Rules for this work

- Do not change beauty kernels or beauty sampling. Deep must remain a
  side-channel. (See Phase 0 for the existing violation.)
Phase 8c user decisions: initialize all deep shader inputs on every device.
Deep grid/texture evaluation uses the native deterministic interpolation
whose expectation beauty samples stochastically; beauty evaluation is unchanged.
Same-step CPU/OptiX curves must repeat byte-identically. Before adaptive
control, demonstrate deterministic midpoint convergence against an independent
CPU integral of the same shader on selected captured rays at h/64 or finer,
with no event cap. If convergence fails, stop. Adaptive step doubling remains
stated, not proven: unresolved features narrower than evaluated steps can be missed.

- Keep the numerical contract explicit: every approximation has a stated
  absolute transmittance bound, and the bounds sum to the user-visible setting.
  Shader-evaluated volumes (8c) explicitly state, rather than prove, E/2 for
  midpoint stepping; E/2 bounds representation/fitting/publication. EXR headers
  identify this distinction, actual world-unit steps and the selection rule.
- Strict mode must reproduce today's behaviour (`--deep-error strict`). Strict
  identity means bit-identical deep payload (sample counts, Z, ZBack, A and any
  other data channels) and deterministic headers. Exclude only run metadata:
  `cycles:beautyIdentity` and attributes recording paths, dates or run IDs.
  Data-describing headers remain mandatory, including `deepError`, `deepSamples`,
  `deepScope`, manifests, channel types and compression. The comparator uses an
  explicit run-metadata allowlist; unknown attributes are compared, not guessed.
  `tools/compare_deep_identity.py` checks every encoded count/pixel chunk and
  every deterministic header byte; files are never rewritten for comparison.
- Measure before and after every phase on the same fixed cases (Section 4).
  Report numbers; do not claim speedups without them.
- If a phase cannot meet its acceptance criteria, stop and report. Do not
  redefine gates or thresholds to pass.
- Documentation: replace, don't append. Keep `DEEP_IMPLEMENTATION_STATUS.md` to a
  short current-state summary; put measurements in one table per phase in this
  file's companion results section (Section 6), not as narrative.
- Never call GPU-thread heap allocation, STL, or files from kernels (existing
  `PEER_DEEP_OUTPUT_REQUIREMENTS.md` still applies).

### OptiX toolchain and conditional fallback (user decision, 2026-10-08)

Pristine `749518deb2f0` renders the AO landscape with clang-cl, CUDA 12.8.0
and OptiX 8.0.0 using native precompiled modules. The earlier MSVC failures
used CUDA 12.8.1 and runtime PTX; compiler and compilation path both changed.
Do not change beauty sources or Cycles flags. Switch the deep builds to the
matching release-style toolchain, then qualify Phase 8a before dropping the
restriction. If no supported deep toolchain works, retain the agreed fallback:
OptiX excludes shader-raytrace (AO/Bevel), rejects explicitly and directs to
CUDA; Phase 9 uses CUDA. Deep builds are switched; qualification continues under the final recorded
toolchain re-baseline policy below.

### Unified cross-comparison rule (final user decision, 2026-10-08)

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

### Final CUDA/OptiX raw beauty rule (user decisions, 2026-10-08)

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

## 3. Phases

### Phase 0 - Checkpoint and scope cleanup

1. Commit the current uncommitted work on `codex/landscape-cloud-compatibility`
   as a WIP checkpoint (49 files, ~2,000 lines) so nothing else is lost.
2. Move the beauty-sampling change out of the deep branch:
   - Files: `src/kernel/integrator/shade_volume.h`
     (`volume_majorant_optical_depth` reading `pass_volume_majorant_snapshot`),
     `src/kernel/film/volume_guiding_denoise.h` (snapshot write),
     `PASS_VOLUME_MAJORANT_SNAPSHOT` in `src/kernel/types.h`, and its plumbing in
     `src/scene/film.cpp`, `src/scene/pass.cpp`, `src/kernel/data_template.h`.
   - Save it as its own branch/patch (e.g. `codex/volume-majorant-determinism`)
     as a possible upstream Cycles determinism fix. Revert it on the deep branch.
3. Change the beauty gate for deep runs: "deep must not change beauty" means
   deep-on vs deep-off differences lie within the ordinary-repeat envelope
   measured with the same executable (CPU: exact equality still required).
   The one-pixel 448 vs 464 adaptive-sample difference was reproduced with deep
   disabled; it is Cycles GPU nondeterminism, not a deep defect.

   Historical CUDA gate definition (user decision, 2026-10-06; superseded
   for raw passes by the final Section 2 rule; denoised policy unchanged):
   - Envelope: render K = 5 ordinary deep-off runs. For each pass, the envelope
     is the maximum per-pixel absolute difference over all pairs of those runs.
     A single pair underestimates GPU run-to-run variation.
   - Raw (noisy) passes: each deep-on pixel must be within
     `max(envelope, 4 ULP of that pixel's value)` of at least one deep-off run
     with the same accepted sample count (keep the existing matched-count rule;
     unknown counts fail). 4 ULP is float accumulation-order noise.
   - Every pass the denoiser consumes (noisy beauty, denoising albedo, normal,
     depth, sample count) is checked the same way, not only the beauty.
   - Denoised beauty: the envelope is the image-wide maximum over all pairs
     of K ordinary deep-off runs, never a per-pixel envelope. Retain qualified
     ordinary-run evidence across phases with unchanged beauty kernels.
     Report the exact-saved-input GPU OIDN repeat difference separately; if
     OIDN alone reproduces the variation, denoised pixels whose raw inputs pass
     the unchanged raw gate are explained (user decision, 2026-10-07).
     It is not a pass/fail gate by itself, BUT any denoised difference outside
     the envelope must be explained by locating the pixels and showing which
     input pass differs. If a denoiser input pass differs beyond the raw gate,
     that is a real deep-to-beauty leak and must be fixed.
   - The 587x250 baseline showed denoised difference 0.407 vs envelope 0.0091.
     Phase 0 is accepted only after this is explained with the method above on
     the post-Phase-0 build.

   Reproduced-state rule (user decision, 2026-10-06, after Phase 1 baseline):
   GPU runs sometimes take a different discrete outcome at a pixel (a camera
   sample hits a different surface: albedo/depth/noisy all jump together). No
   fixed K can be guaranteed to contain every such outcome, so a K-run envelope
   alone does not terminate.
   - Reference pool: every ordinary deep-off render of the same scene and
     settings made with a renderer whose beauty kernels are unchanged from the
     current build (verify with the recorded executable hash, or with source
     identity of everything outside `src/deep`, `src/kernel/deep` and the deep
     host/scheduling code). Pools accumulate across phases; keep the files.
   - A deep-on pixel that fails the K-run gate passes if ALL of its checked
     input passes match one single pool render at that pixel within 4 ULP
     (same accepted sample count). The match must be one render, not a mix.
   - If no pool render matches, render up to 20 additional deep-off controls,
     adding each to the pool, and re-check. Only a pixel still unmatched after
     that is a failure to investigate as a possible deep-to-beauty leak.
   - Report every pixel resolved this way (pixel, passes, matching pool run).
   - CPU stays exact equality; it is the primary proof that deep does not alter
     beauty. The CUDA check is a regression guard.
   - Host-only phases (1 and 7) change no kernel or GPU scheduling code. For
     them, the CUDA beauty gate runs once at the end of the phase, not as a
     before-implementation blocker.

Acceptance: build passes, nine CTests pass, small landscape (47x20/max16) deep
and beauty checks pass with the new gate.

### Phase 1 - Banded spill layout (no numerical change)

Goal: export reads each spilled byte about once, sequentially, without a global
lock.

Design:
- Split spill storage into row bands. Default `rows_per_band = ceil(height / 64)`
  (one append file + one fixed-offset index per band). Each band owns its file
  handles, page caches and mutex. Keep the existing `SpillRecord`/completion
  semantics per band (EMPTY vs COMPLETE, duplicate detection, finalize checks).
- `store_record` routes to the band of `y`. Capture writes stay append-only.
- Export processes rows in order. When it enters a band, it reads that band's
  event file sequentially once and builds an in-memory per-pixel index
  (pixel -> list of (sample, offset)) or directly per-pixel record vectors.
  Charge this to `--deep-memory-mb`.
- If a band does not fit in the budget, re-bucket it sequentially into per-row
  files (second level), then process row by row. Never fall back to random
  per-sample reads.
- Export workers read from memory; remove `mutex_` from the export read path.
- Release/delete each band's files when its rows are written (frees disk early).
- Windows: check the stdio open-file limit (`_setmaxstdio`) or use native
  handles; 64 bands x 2 files is fine with the default 512.
- Keep in-memory (non-spill) mode for reference tests.

Tests:
- Extend `capture_test.cpp`: random batch order across bands, adaptive
  populations, misses, partial final blocks, duplicate detection across bands,
  I/O failure preserving previous EXR (existing publication tests).
- Assert `spill_read_bytes <= 1.25 x spill_file_bytes` for a multi-band case
  (log both numbers, as today).

Acceptance: byte-identical deep EXR vs. pre-change build on the small landscape
and the CPU/CUDA compatibility matrices; read amplification <= 1.25x;
report export time on the 587x250x4 landscape case before/after.

### Phase 2 - Profile, then fix boundary pairing

1. Profile the deep kernel on the 587x250x4 landscape case (Nsight Systems for
   timeline, Nsight Compute for `deep_surface`). Report: kernel time vs. sync
   wait vs. copy vs. host spill; time in `deep_volume_object` triangle loop vs.
   grid traversal; triangle counts of each volume bound mesh
   (`tools/inspect_blender_deep_scene.py` can be extended).
2. If the triangle loop is significant, replace the per-crossing full scan with
   a BVH-accelerated per-object query (Cycles `scene_intersect_local` style, or
   the existing volume all-hit traversal filtered to the object) to get
   candidates for `[cursor, clip_end]`, then refine candidates with the existing
   double-precision `deep_volume_triangle` and keep the existing tie/ordering
   rules. If the query is capacity-limited, shrink `tmax` and repeat; never drop
   a crossing.

Acceptance: CPU/CUDA boundary suites (`boundary-final-*`, 33 render cases +
17 rejections) pass unchanged; deep EXR byte-identical; measured capture
speedup reported.

### Phase 3 - Error tolerance setting + per-ray compression on device

This is the RenderMan `deepshadowerror` idea: a user tolerance on absolute
transmittance error, enforced by single-pass Lokovic-Veach style compression.
Keep the existing exponential-interval representation (it matches the
OpenEXR deep volumetric convention); do not switch to piecewise-linear T.

3a. Setting (host path only; stop for review after results)
- Add `float error` to `DeepSettings` (`src/session/deep.h`). Default `1e-3`.
  Range `[strict, 1e-2]`. `strict` = today's constants exactly.
- Plumb through standalone (`--deep-error <float|strict>` in
  `src/app/cycles_standalone.cpp`) and Blender (`deep_error` property via
  `tools/prepare_blender_deep.py` patches; document in
  `BLENDER_DEEP_INTEGRATION.md`).
- Non-strict writes the effective value as `cycles:deepError`. Strict keeps the
  existing `cycles:maxTransmittanceError` only, preserving deterministic headers.
  Validators read the published bound (new attribute, or legacy in strict).
- Budget split (document in `src/deep/volume.h`): the FLOAT export/coefficient
  allowance is a precision floor and stays fixed. Split the remainder
  `E - export_floor` between device per-ray compression (50%) and host
  mixture fitting + reduction (50%), replacing the hard-coded constants. Relax
  the `tolerance > 1e-3` guards in `volume.cpp` to the new maximum.
- Update Gaffer validators (`src/deep/validate_*_gaffer.py`) to compare against
  the header tolerance.
- One knob, not several: the existing optional surface reduction
  (`reduce_surface` in `src/deep/exr_writer.cpp`, hard-coded `1e-3` via
  `image.reduction_error` in `src/app/deep_output.cpp`) and the export
  coalescing allowance must also derive from `DeepSettings::error`, inside the
  same documented budget split. Strict keeps today's values.

3b. Device per-ray compression (separate review stop)
- Before coding 3b, document the error-bound derivation in `volume_grid.h`,
  summarize it to the user, then add independent exact-cubic integration tests.
- No expansion: every ray emits at most its strict record count. Keep an
  original exact cubic cell unchanged when its curvature cannot fit the
  allowance; charge its fitting to the reserved host density allowance.
  Only merge consecutive cells when a linear segment replaces at least two
  original records. No cell subdivision on the device.
- Split the density allowance between device capture and host fitting, with
  explicit arithmetic reserves. Host preflight counts volume objects and
  passes `min(volume_objects, DEEP_MAX_MEDIA)` through kernel data;
  `eps_object = eps_ray / min(volume_objects, DEEP_MAX_MEDIA)`.
  Product telescoping bounds overlapping media by the sum of object bounds;
  convex camera averaging preserves the ray bound.
- Early termination is required: when an object's own transmittance is at
  most its allowance, emit an opaque surface event, stop that object and clamp
  later traversal to that depth. Charge the tail to its object allowance;
  retain the no-expansion rule and conservative depth rounding.
- Strict mode keeps today's cubic records and host path untouched.

3c. deepID-ready data (separate review stop; no output change; groundwork for Phase 6)
- Add the object index to every captured event (surface hits and volume
  intervals) in `kernel/deep/types.h`, with updated layout asserts, GPU buffers
  and spill format. The kernel already traverses one object at a time
  (`deep_volume_object`), so per-ray compression must run per object stream and
  never merge across objects.
- Host reconstruction ignores the index for now (output identical), but the
  data path must carry it end to end so Phase 6 is output work only.

Acceptance:
- Strict: everything byte-identical to Phase 2.
- `1e-3` and `1e-4`: accepted-camera oracle and Gaffer depth cuts pass at the
  header tolerance on the CPU/CUDA compatibility matrices and the small
  landscape; independent tests in `vdb_grid_test.cpp`/`vdb_grid_cuda_test.cu`
  compare compressed curves to the exact cubic integration within `eps_ray`.
- At both fixed cases and both numeric settings, GPU readback bytes, spill
  bytes and capture time must each be at most accepted Phase 3a values.
  Tests assert per-ray records <= strict and check the independent cubic oracle.
- Include CPU curve-fitting time separately from spill reads, reconstruction and
  EXR serialization in both fixed cases before/after; measure strict / 1e-4 /
  1e-3. The Phase 1 scratch result suggests fitting dominates export; quantify it.
- Report on 587x250x4 landscape: readback bytes, spill bytes, deep samples in
  EXR, EXR size, capture and export time, for strict / 1e-4 / 1e-3.

### Phase 4 - GPU batch loop

With per-ray output now small and bounded, rework `capture_deep_tiles`:
- Remove the 64->8192 retry ladder for compressed mode (keep it for strict).
- Replace plane layout with a flat output buffer: each lane writes to its own
  bounded slot range, plus a per-batch counter or prefix sum so only written
  bytes are copied back.
- Double-buffer records/events and use async copies so the next batch's kernel
  overlaps the previous batch's copy and host spill. Synchronize only before
  consuming a buffer. Preserve cancellation/failure semantics.
- Pack kind into the low two event bits and object index into the upper thirty.
  Assert the 20-byte ABI and reject scene object counts above the 30-bit range.
- Larger launches: process all active work of the tile set per launch where the
  staging budget allows, rather than ~2,700-lane batches with ~10% active.

Acceptance: deep EXR byte-identical to Phase 3 for the same settings; CUDA
cancellation/error-injection suites pass; capture time and GPU peak memory
reported (stay within the existing 8192 MiB device gate).
Report both **bytes actually written by lanes** and **bytes copied**, before/after,
so compression savings are distinguishable from plane padding and copy overhead.

### Phase 5 - Deep sample count setting

- Add `int samples` to `DeepSettings`: `0` = all accepted beauty samples
  (default, current behaviour). Positive N = capture deep only for the first N
  accepted samples of each pixel (the sampler prefix is stratified).
- Kernel/host: skip capture for `sample >= N`; per-pixel population becomes
  `min(accepted, N)`. Size `Capture` by `min(samples, N)` (large memory saving).
- Plumb `--deep-samples` and Blender `deep_samples`; write `cycles:deepSamples`
  to the EXR header. Validators use the same subset for the accepted-camera
  oracle. N=0 omits the new header to preserve legacy identity; positive N
  records min(N, beauty maximum). Its curve bound applies to the retained prefix.
- Recommended for heavy volume scenes such as the landscape: `64`.

Acceptance: `0` byte-identical to Phase 4; N=64 passes alpha oracle, depth
cuts and beauty gate; report edge-alpha difference vs. N=all on the small
landscape as information (not a gate).

### Phase 6 - deepID

Placed before the production run (Phase 9) so that one landscape run covers the final
pipeline, and so the per-sample data format changes only once (Phase 3c).

Standard production deep compositing uses a per-sample object/instance ID
(per-object holdouts, isolating one cloud, DeepCryptomatte-style selection).
- deepID is a per-sample channel in the deep EXR, next to Z/ZBack/A. It is NOT
  a Cycles render pass or AOV: passes/AOVs are flat (one value per pixel) and
  live in the render buffers, while one deep pixel holds samples from several
  objects at different depths. Do not add a `PassType` or use the AOV system.
- Source: the object index already carried end to end since Phase 3c.
- Output: a UINT channel named `id`, plus a manifest in the EXR header mapping
  id -> object name. Use the same name hashing as Cycles Cryptomatte so deep and
  flat Cryptomatte selections refer to the same objects.
- Host setting: `DeepSettings::ids` (`--deep-ids`, Blender `use_deep_ids`
  checkbox beside the other deep settings). Off by default (larger files).
- With ids on, reconstruction, reduction and averaging only merge samples with
  the same ID. Overlapping media of different objects become separate,
  overlapping deep samples instead of summed extinction.
- Support decision: strict exists only to reproduce legacy output, which had no
  IDs. Reject strict + IDs at preflight: `--deep-ids requires a numeric
  --deep-error; strict mode reproduces legacy output without IDs`. This changes
  the support matrix, not any capacity or error gate; test the rejection.
- Publication decision: measure each object's FLOAT rounding error before
  coalescing (choose the better existing cumulative projection/direct rounding).
  Require their sum <= `budget.publication`; otherwise report the actual pixel,
  object count and largest contributors. Distribute remaining headroom plus
  the coalescing proposal allocation by volume interval count. Accept only
  proposals whose measured error fits their share of remaining headroom; recheck
  the final sum <= the same publication budget. The coalescing proposal allowance
  does not enlarge the final total. Header error, effective error, all total
  bounds and IDs-off arithmetic are unchanged. Test dense volume + many exact
  hard surfaces: equal split fails but measured allocation passes the same total.
- Numeric IDs-on qualification: small 47x20x16, performance 587x250x4, and
  realistic 117x50 with original 1024 adaptive/GPU OIDN, at both 1e-3 and 1e-4.
  Realistic uses deep-samples 0 and 64. Compare combined alpha against matching
  IDs-off within the header bound, and report sample counts, EXR bytes, export
  time and spill. Replay the full regression set. On any numeric interval-cap
  failure, stop and report the pixel and per-object interval counts; keep 65,536.

Acceptance: ids off is byte-identical to Phase 5. With ids on: combined alpha
(after deep flatten/merge) matches ids off within the header tolerance on the
compatibility matrices and small landscape; Gaffer and the OpenEXR reader load
the overlapping samples correctly; selecting one object's id isolates it
(checked on an overlap fixture with known per-object alpha).

#### 6a - Same-object surface depth merging (from MoonRay `deep_z_tolerance`)

High sample counts give one surface many distinct deep samples per pixel (each
camera ray hits a sloped or curved surface at a slightly different depth).
Merge them, as MoonRay does, using the object index carried since Phase 3c.
This works whether or not the `id` channel is written.
- Merge only surface samples with the same object index and the same facing,
  whose depths lie within a relative depth tolerance of each other. Never merge
  across objects, across volume intervals, or across a gap occupied by another
  object's sample.
- A merged group becomes one sample spanning `[z_min, z_max]` (ZBack = z_max;
  a single-depth group stays a hard surface with Z = ZBack). Its alpha is the
  group's combined alpha, so transmittance in front of `z_min` and behind
  `z_max` is unchanged; only depths inside the span are approximated.
- This is a depth-domain approximation, separate from the transmittance error
  budget. Document it as such and record it in the header
  (`cycles:deepZTolerance`).
- Setting: `--deep-z-tolerance` (Blender `deep_z_tolerance`), relative to
  depth. Default `1e-4` in non-strict modes (5 cm at 500 m); `0` disables.
  Strict forces 0.
- Acceptance: strict 81/81 and z-tolerance 0 numeric 30/30 identical. With the
  default, transmittance outside every merged span matches the unmerged output
  within the header error, and full-flatten alpha is unchanged. Report surface
  sample count and EXR size reduction on the 117x50 / max-1024 landscape and a
  sloped high-sample surface fixture.

#### 6b - Holdout objects (RenderMan and MoonRay render matte objects into deep)

`src/session/deep.cpp` currently rejects scenes containing holdout objects
("holdout, shadow catcher and caustics are unsupported"), so many production
scenes fail outright.
- Accept the object holdout flag and the Holdout shader. In deep, holdout
  geometry contributes its normal camera opacity (it occludes what is behind
  it). The beauty already renders it with zero alpha and colour, so
  DeepRecolor-style compositing yields a correct deep holdout.
- Do not change beauty or the existing holdout pass. Shadow catcher and caustics
  stay rejected with their existing messages.
- With `--deep-ids`, mark holdout objects in the manifest so compositors can
  identify them.
- Acceptance: fixtures with an object holdout and a material holdout, in front
  of and inside a volume, on CPU and CUDA. Deep alpha matches the same scene
  with the holdout replaced by an ordinary opaque object; beauty gate vs.
  deep-off; strict 81/81 for existing suites.

Stop after 6, 6a and 6b each.

### Phase 7 - Single regression command and repo hygiene

Goal: make every future change cheap to verify, so effort goes into code rather
than evidence writing.
- One entry point (e.g. `ctest -L deep` plus `tools/run_deep_regression.py`)
  that renders a small fixed set of golden scenes (surface, transparency,
  homogeneous, VDB, overlap, camera-inside, adaptive, DOF, motion, small
  landscape), runs the alpha oracle and depth-cut checks, and prints one
  pass/fail table. Target: under 15 minutes on this machine.
- Gaffer remains optional for interactive review; numerical checks should run
  through the OpenEXR Python bindings without launching Gaffer.
- Default renders/large outputs and TEMP/TMP are owned by
  `D:/CyclesDeepScratch/regression/<run-id>/`; only small reports stay under
  `builds/`. Delete its own intermediates after PASS, unless `--keep`; retain
  failed evidence. Never delete through historical junction folders.
- Include strict 81/81, numeric 30/30, CPU beauty equality, source hashes and
  75 kernel resource records. CUDA beauty is a separate optional stage for
  GPU-side changes; host-only phases use the Section 2 proof.
- Inventory historical sample CSVs; retain anything needed uncompressed.
  User approved replacing unneeded CSVs only after full-byte archive verification,
  and archiving 13 historical validation Markdown reports in Git history;
  retain the approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md).

- Consolidate the many `validate_*_gaffer.py` scripts into that harness where
  they overlap. Archive (Git history) the per-milestone `*_VALIDATION.md`
  evidence files; keep `RELEASE_MATRIX.md`, this plan, and one short status page.
- Keep the Cycles core footprint small for future rebases onto newer
  Cycles/Blender: list every non-`deep/` file touched (`kernel/types.h`
  `KernelShader` field, `kernel/util/nanovdb.h` accessor, `surface_shader.h`
  template parameter, film/pass plumbing) in `src/deep/README.md` with the
  reason for each.

### Phase 8 - OptiX backend and OSL shaders

#### 8a - OptiX with native (SVM) shaders

Most NVIDIA users render with OptiX; today enabling deep forces the slower CUDA
backend for the whole render. The visibility chain is mostly BVH traversal, so
RT cores should also speed up capture itself.
- Add the `deep_surface` kernel to the OptiX module using the existing shared
  kernel code; OptiX `scene_intersect`/`scene_intersect_volume` paths already
  exist in Cycles.
- Run the native VDB grid code under OptiX with the same CUDA double-precision
  qualification (`volume_grid.h` currently gates on `__KERNEL_CUDA__`).
- Acceptance: CPU/CUDA/OptiX matrices; each backend passes its own header
  oracle/depth cuts and beauty gate. Cross flattened alpha <=1e-4 under Section 2.

Stop after 8a (OptiX with SVM shaders, as above) before starting 8b/8c.

#### 8b - OSL surface shaders (CPU and OptiX)

Requested by a studio developer: scenes whose materials are written in OSL must
work with deep on the GPU. In Cycles, GPU OSL runs only on OptiX, which is why
this belongs here. Today `src/session/deep.cpp` rejects GPU OSL ("CUDA deep
supports native SVM only") and allows only restricted CPU OSL fixtures.
- OptiX OSL shaders are compiled into the OptiX pipeline and reached through
  its callables. Build the deep capture program into that same pipeline so the
  deep visibility chain can evaluate OSL surface shaders. Do not change how
  beauty compiles or calls OSL.
- Deep needs only scalar transparency from surfaces. Keep the existing runtime
  checks: non-grey transparency, cache misses and invalid values fail
  explicitly.
- Replace node-graph allowlisting for OSL with a compile-time query of each OSL
  shader group (OSL exposes what a group uses): reject shaders whose
  transparency could differ between the deep chain and beauty, such as `trace()`,
  ray-type queries other than camera, path-dependent attributes (ray depth,
  ray length), or unknown attributes. Each rejection names the shader and the
  reason. Constant, texture and noise-driven transparency is allowed.
- CPU OSL support grows to match, because CPU is the reference for OptiX OSL.
- Acceptance: new OSL fixtures (constant, textured and noise transparency,
  mixed OSL/SVM scenes, each rejection case) pass on CPU and OptiX. OptiX deep
  alpha matches CPU within the header tolerance. Beauty gate vs. OSL deep-off on
  both devices. Strict mode remains 81/81 for existing SVM suites.

Stop after 8b before starting 8c.

#### 8c - OSL volume shaders

Phase 8b accepted. 8c preserves analytic/SVM payloads and deterministic headers
(81 strict + 30 numeric, OptiX 65, landscape fixtures). Only OSL volumes or
`--deep-volume-shader-eval` select evaluation; no implicit SVM fallback.
Numeric error is required: E/2 is the stated (not proven) adaptive-stepping
allowance, E/2 bounds capture representation, fitting and publication. Header
method, starting/maximum material steps, step rule and both allocations are explicit.
The world-unit step is the minimum voxel edge across objects sharing a material;
an explicit `--deep-volume-step` caps it and is required without a grid.
Qualification measures each fixture against a 4x-finer step without loosening E.


The analytic VDB path reads the density grid directly and needs a node graph
that `deep.cpp` can prove is "density x constant". An OSL volume shader cannot be
analysed that way, so OSL volumes need the general shader-evaluation path
(previously listed under "Later"; it moves here).
- Evaluate the real volume shader with `PATH_RAY_EXTINCTION` using adaptive midpoint
  step doubling (coarse versus two half integrals, reusing evaluations)
  along each medium interval and integrate extinction (same visibility chain,
  no beauty state or RNG changes, bounded per-thread scratch).
- Step size: from the object's grid voxel size when the shader reads a grid,
  otherwise from a new explicit setting (`--deep-volume-step`, Blender
  `deep_volume_step`). Write it to the EXR header.
- This path's error is stated, not proven: document that it depends on step
  size versus how fast the shader varies. Validate against a 4x-finer-step
  reference render; report the measured difference. Keep the analytic VDB path
  as the fast path whenever the graph qualifies (SVM or OSL-free).
- The same path also serves SVM volume graphs that today fail pattern matching.
  Keep it opt-in for those (`--deep-volume-shader-eval`) so existing analytic
  results are unchanged.
- Acceptance: OSL constant, textured and grid-reading volumes on CPU and
  OptiX; within the stated tolerance of the finer-step reference; OptiX vs CPU
  within header tolerance; beauty gate; existing analytic suites unchanged.

### Phase 9 - Landscape production run (last)

Phases 0-8 accepted; user confirmed launch on 2026-10-09. Use OptiX;
CUDA fallback is permitted only for an OptiX startup failure, which must be
reported. Stop on any other failure/process stoppage; never retry automatically.

Original landscape: 1175x500, max 1024 adaptive, GPU OIDN, numeric error 1e-3,
default z-tolerance 1e-4. Run `--deep-samples 0 --deep-ids`, then
`--deep-samples 64 --deep-ids`, followed by five ordinary deep-off controls and
four seed-varied deep-off controls. No additional production renders requested.
TEMP/TMP, caches and large outputs stay under D:/CyclesDeepScratch.
D: has approximately 1.17 TB free; the Phase 5 all-samples spill estimate is
181 GB. Record executable SHA/toolchain and automatic sleep/hibernate settings
before launch. The Phase 5 beauty estimate scales startup cost 100x and is an
upper bound: earlier full-resolution beauty measured about seven minutes.

Report render+capture, export, peak host/GPU memory, spill, EXR size, deep
sample count, independent accepted-camera oracle/depth cuts, beauty policy and
Phase 5 estimate comparison. Same-capture z=0 validation publication uses the
existing diagnostic hook; report its export overhead separately. Keep the
approved surface-span exterior check. Notify after run 1 completes. Create a
connected Gaffer review only after all gates pass; stop for acceptance review.

### Not recommended now: deep RGB

Compositing deep alpha with the flat beauty (Nuke DeepRecolor) already covers
the main use, holding out CG or FX inside clouds. True deep colour needs path
contributions attributed to depth for scattering volumes: a large research
project. Revisit only if a concrete comp need appears that DeepRecolor cannot
handle.

### Later

- General volume shader fallback: moved into Phase 8c.
- Subpixel coverage masks (MoonRay's OpenDCX output, from DreamWorks): an 8x8
  coverage mask and surface flags per deep sample, so separately rendered
  elements merge correctly at edges. This fixes the known scalar-alpha merge
  limitation stated in `src/deep/README.md` (two half-covered elements can
  combine to 0.5 or 1). Compositors need the OpenDCX Nuke plugins, so do this
  only if edge problems appear in production deep merges. Capture would record
  each camera sample's subpixel position; reconstruction would keep per-mask
  coverage instead of averaging it away.
- Resumable capture (persist band files + manifest) only if long runs remain
  after Phases 1-5.

## 4. Fixed measurement cases

Use the same cases before/after every phase:

| Case | Purpose |
| --- | --- |
| Small landscape 47x20, max 16 adaptive, CUDA | correctness smoke, all gates |
| Landscape 587x250, 4 samples, CUDA (`review-profile` settings) | performance |
| CPU/CUDA compatibility matrices (14 CPU / 4 CUDA) | regression |
| Boundary suites `boundary-final-CPU/CUDA` | regression |
| Nine CTests | regression |

Log for each: capture time, `capture_readback_seconds`, `readback_bytes`,
spill file/read/write bytes, export time, EXR bytes, total deep samples, peak
host working set, device-wide GPU peak.

## 5. Expected outcome (to be verified, not promised)

- Phase 1: export becomes sequential I/O + CPU fitting; read amplification
  from 18x to ~1x.
- Phase 3 at 1e-3: large reductions in readback, spill and EXR size (curve
  pieces scale ~1/sqrt(tolerance); 1e-7 -> 1e-3 is ~100x looser in sqrt terms).
- Phase 5 at 64 samples: ~16x less capture work and storage for 1024-sample
  renders.

## 6. Results

### Phase 0 - accepted

Checkpoint: `59963b932`. Snapshot experiment: `codex/volume-majorant-determinism`
(`9a017f055`). Removal/build: `44b44477e`. K=5 input gate: `7efb85198`.
Both builds, the K=5 landscape checks and regressions pass. Strict identity
passes under the user-approved Section 2 definition: 66 boundary renders,
13 rendered matrix cases and both landscapes. Evidence: `k5/strict-identity.json`.
The explicit strict setting arrives in Phase 3; capture/export use the unchanged
legacy strict path. Phase 1 begins with fresh fixed-case measurements.

Single measurements, not speedup claims. Before controls used K=2; after uses
the approved K=5, one global envelope per pass, count matching and four FLOAT
ULP. Raw after differences are maxima against the closest count-matched
reference. Evidence: `builds/validation/landscape-cloud/optimization-phase0/k5`.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 | Capture | 5.675599 s | 5.804744 s | `7efb85198` |
| 47x20 / max16 | Capture wait + readback | 4.562987 s | 4.647866 s | `7efb85198` |
| 47x20 / max16 | GPU readback | 372,191,232 bytes | 372,191,232 bytes | `7efb85198` |
| 47x20 / max16 | Spill stored | 13,006,224 bytes | 13,006,224 bytes | `7efb85198` |
| 47x20 / max16 | Spill read | 34,173,856 bytes | 34,436,000 bytes | `7efb85198` |
| 47x20 / max16 | Spill written | 14,805,816 bytes | 14,805,816 bytes | `7efb85198` |
| 47x20 / max16 | Export | 8.25049 s | 8.1713 s | `7efb85198` |
| 47x20 / max16 | EXR size | 4,445,714 bytes | 4,445,714 bytes | `7efb85198` |
| 47x20 / max16 | Deep samples | 561,793 | 561,793 | `7efb85198` |
| 47x20 / max16 | Peak process working set | 5,634,203,648 bytes | 5,553,156,096 bytes | `7efb85198` |
| 47x20 / max16 | Device-wide GPU peak | 5,013 MiB | 5,217 MiB | `7efb85198` |
| 47x20 / max16 | Max oracle error | 2.20131377e-07 | 2.20131377e-07 | `7efb85198` |
| 47x20 / max16 | Max depth-cut error | 6.95131077e-07 | 6.95131077e-07 | `7efb85198` |
| 47x20 / max16 | Raw beauty difference | 1.1920929e-07 | 1.1920929e-07 | `7efb85198` |
| 47x20 / max16 | Raw ordinary-repeat envelope | 1.1920929e-07 | 3.57627869e-07 | `7efb85198` |
| 47x20 / max16 | Denoised beauty difference | 0 | 0 | `7efb85198` |
| 47x20 / max16 | Denoised ordinary-repeat envelope | 0 | 0.00544679165 | `7efb85198` |
| 47x20 / max16 | Beauty / oracle / depth gates | PASS / PASS / PASS | PASS / PASS / PASS | `7efb85198` |
| 587x250 / 4 | Capture | 71.598867 s | 71.649003 s | `7efb85198` |
| 587x250 / 4 | Capture wait + readback | 68.6113 s | 68.6475 s | `7efb85198` |
| 587x250 / 4 | GPU readback | 7,177,170,944 bytes | 7,177,011,200 bytes | `7efb85198` |
| 587x250 / 4 | Spill stored | 508,375,136 bytes | 508,375,136 bytes | `7efb85198` |
| 587x250 / 4 | Spill read | 3,850,327,984 bytes | 3,878,118,896 bytes | `7efb85198` |
| 587x250 / 4 | Spill written | 564,666,368 bytes | 564,666,368 bytes | `7efb85198` |
| 587x250 / 4 | Export | 50.1401 s | 49.0674 s | `7efb85198` |
| 587x250 / 4 | EXR size | 394,273,944 bytes | 394,273,944 bytes | `7efb85198` |
| 587x250 / 4 | Deep samples | 52,189,081 | 52,189,081 | `7efb85198` |
| 587x250 / 4 | Peak process working set | 5,566,656,512 bytes | 5,750,951,936 bytes | `7efb85198` |
| 587x250 / 4 | Device-wide GPU peak | 5,090 MiB | 5,227 MiB | `7efb85198` |
| 587x250 / 4 | Max oracle error | 2.20799077e-07 | 2.20799077e-07 | `7efb85198` |
| 587x250 / 4 | Max depth-cut error | 8.2280344e-07 | 8.2280344e-07 | `7efb85198` |
| 587x250 / 4 | Raw beauty difference | 4.76837158e-07 | 4.76837158e-07 | `7efb85198` |
| 587x250 / 4 | Raw ordinary-repeat envelope | 4.76837158e-07 | 0.0171391964 | `7efb85198` |
| 587x250 / 4 | Denoised beauty difference | 0.407463074 | 0.0090982914 | `7efb85198` |
| 587x250 / 4 | Denoised ordinary-repeat envelope | 0.00909805298 | 0.596702576 | `7efb85198` |
| 587x250 / 4 | Beauty / oracle / depth gates | FAIL / PASS / PASS | PASS / PASS / PASS | `7efb85198` |
| 47x20 / max16 | Full deep EXR bytes | Reference | Identical SHA-256 | `7efb85198` |
| CPU compatibility | Cases | 14/14 PASS | 14/14 PASS | `7efb85198` |
| CPU compatibility | Summed case wall time | 82.516 s | 91.719 s | `7efb85198` |
| CPU compatibility | Rendered deep EXR bytes | Reference | 9/9 identical | `7efb85198` |
| CPU boundary | Renders / expected rejections | 33 / 17 PASS | 33 / 17 PASS | `7efb85198` |
| CPU boundary | Deep sample-offset / Z / ZBack / A bits | Reference | 33/33 identical | `7efb85198` |
| CPU boundary | Whole-file hash | Reference | 0/33 identical: beautyIdentity header differs | `7efb85198` |
| CUDA compatibility | Cases | 4/4 PASS | 4/4 PASS | `7efb85198` |
| CUDA compatibility | Summed case wall time | 56.719 s | 87.547 s | `7efb85198` |
| CUDA compatibility | Rendered deep EXR bytes | Reference | 4/4 identical | `7efb85198` |
| CUDA boundary | Renders / expected rejections | 33 / 17 PASS | 33 / 17 PASS | `7efb85198` |
| CUDA boundary | Deep sample-offset / Z / ZBack / A bits | Reference | 33/33 identical | `7efb85198` |
| CUDA boundary | Whole-file hash | Reference | 0/33 identical: beautyIdentity header differs | `7efb85198` |
| CPU boundary | Strict payload + deterministic headers | Reference | 33/33 identical | metadata decision / identity comparator |
| CUDA boundary | Strict payload + deterministic headers | Reference | 33/33 identical | metadata decision / identity comparator |
| Nine CTests | Result / wall time | 9/9 PASS / 17.28 s | 9/9 PASS / 11.79 s | `7efb85198` |
| 47x20 / max16 | Raw input violations / unmatched populations | Not checked per pass | 0 / 0 | `7efb85198` |
| 587x250 / 4 | Raw input violations / unmatched populations | Not checked per pass | 0 / 0 | `7efb85198` |
| 587x250 / 4 | Full deep EXR bytes | Reference | Identical SHA-256 | `7efb85198` |

Historical denoised R outlier: pixel (505,96), deep 51.48297882 vs ordinary
51.07551575, difference 0.40746307; all accepted counts were 4. Local raw inputs
match, but neighboring noisy G at (505,93) is 0.84282809496 vs 0.84282815456
(one FLOAT ULP; within the 4.76837158e-7 raw envelope). All 87 historical
outliers have nearby noisy/albedo/normal differences, recorded in
`k5/historical-oidn-neighbor-inputs.json`. OIDN filters spatial neighborhoods;
input amplification is an inference, not an isolated causal replay. Depth was
also checked but is not used to explain OIDN output. The post-cleanup K=5
ordinary denoised envelope is 0.59670258, deep-on difference 0.00909829, with
zero unexplained outliers and zero raw input violations. The earlier two-run
envelope underestimated ordinary variation; no renderer tolerance was changed.

Both boundary suites retain identical sample-offset / Z / ZBack / A bits.
Their run-specific `cycles:beautyIdentity` headers contain output paths and
beauty hashes. Beauty EXRs contain `capDate`: 32/33 CPU and 30/33 CUDA beauty
files differ only in this timestamp; the remaining cases also differ in beauty
pixels. Whole-file hashes therefore differ while strict identity passes. Evidence: `k5/boundary-beauty-timestamp-diagnostic.json`. No file was
modified, and the user-approved comparison excludes only run metadata as defined in Section 2. Rendered compatibility
files and both landscapes are whole-file identical. Rejection sentinels are
excluded from rendered-file counts. Suite times include control renders and
validation, and are not performance comparisons. Detailed resource metrics are
landscape-only. Wrong per-pixel-envelope diagnostics are retained separately;
they were corrected to the plan's global per-pass definition using saved EXRs.

### Phase 1 - accepted

Evidence: `builds/validation/landscape-cloud/optimization-phase1/{before,after}`.
Banded capture loads immutable export data sequentially; oversized bands use a
checked per-row re-bucket. GPU kernels/scheduling and reconstruction are unchanged.
All strict payload/header comparisons and regression gates pass.
Host-only follow-up reuses count-sized worker scratch and removes capacity-wide
clears, including re-bucketing. Evidence: the same root under `scratch`.
Baseline (489,34) matches every checked raw pass in Phase 0 deep-off control 3
within 4 FLOAT ULP, population 4 (Noisy R, Albedo R and Depth exceeded fresh K=5).
After runs needed no pool resolutions and no additional controls.
Validation tools: commit 65fcfd1d3.
Build/source provenance: `builds/validation/beauty-builds.json`.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 | Capture seconds | 5.793281 | 5.705214 | `135624361` |
| 47x20 / max16 | Capture wait + readback seconds | 4.660442 | 4.562865 | `135624361` |
| 47x20 / max16 | GPU readback bytes | 372191232 | 372191232 | `135624361` |
| 47x20 / max16 | Spill stored bytes | 13006224 | 13367184 | `135624361` |
| 47x20 / max16 | Spill read bytes | 33911712 | 13367184 | `135624361` |
| 47x20 / max16 | Spill written bytes | 14805816 | 13728144 | `135624361` |
| 47x20 / max16 | Export seconds | 7.05423 | 8.30383 | `135624361` |
| 47x20 / max16 | EXR bytes | 4445714 | 4445714 | `135624361` |
| 47x20 / max16 | Peak process working-set bytes | 5531406336 | 5555224576 | `135624361` |
| 47x20 / max16 | Device-wide GPU peak MiB | 5067 | 5107 | `135624361` |
| 47x20 / max16 | Deep samples | 561793 | 561793 | `135624361` |
| 47x20 / max16 | Max oracle error | 2.201313769045754e-07 | 2.201313769045754e-07 | `135624361` |
| 47x20 / max16 | Max depth-cut error | 6.95131076811073e-07 | 6.95131076811073e-07 | `135624361` |
| 47x20 / max16 | Beauty / alpha / depth | PASS / PASS / PASS | PASS / PASS / PASS | `135624361` |
| 587x250 / 4 | Capture seconds | 71.385669 | 71.729304 | `135624361` |
| 587x250 / 4 | Capture wait + readback seconds | 68.399 | 68.6604 | `135624361` |
| 587x250 / 4 | GPU readback bytes | 7177170944 | 7177170944 | `135624361` |
| 587x250 / 4 | Spill stored bytes | 508375136 | 522463136 | `135624361` |
| 587x250 / 4 | Spill read bytes | 3848873808 | 600218816 | `135624361` |
| 587x250 / 4 | Spill written bytes | 564666368 | 576484544 | `135624361` |
| 587x250 / 4 | Export seconds | 49.2628 | 34.5202 | `135624361` |
| 587x250 / 4 | EXR bytes | 394273944 | 394273944 | `135624361` |
| 587x250 / 4 | Peak process working-set bytes | 5538512896 | 5676564480 | `135624361` |
| 587x250 / 4 | Device-wide GPU peak MiB | 5175 | 5192 | `135624361` |
| 587x250 / 4 | Deep samples | 52189081 | 52189081 | `135624361` |
| 587x250 / 4 | Max oracle error | 2.2079907672709065e-07 | 2.2079907672709065e-07 | `135624361` |
| 587x250 / 4 | Max depth-cut error | 8.228034402701923e-07 | 8.228034402701923e-07 | `135624361` |
| 587x250 / 4 | Beauty / alpha / depth | PASS (pool control 3) / PASS / PASS | PASS / PASS / PASS | `135624361` |
| Nine CTests | Regression | 9/9 PASS | 9/9 PASS | `135624361` |
| CPU compatibility | Regression | 14/14 PASS | 14/14 PASS | `135624361` |
| CUDA compatibility | Regression | 4/4 PASS | 4/4 PASS | `135624361` |
| CPU boundary | Regression | 33 renders / 17 rejections PASS | 33 renders / 17 rejections PASS | `135624361` |
| CUDA boundary | Regression | 33 renders / 17 rejections PASS | 33 renders / 17 rejections PASS | `135624361` |
| 587x250 / 4 at (489,34) | Noisy R: difference / allowed bound | 0.017139196395874023 / 9.5367431640625e-07; pool control 3 PASS | PASS fresh K=5; no pool resolution | `135624361` |
| 587x250 / 4 at (489,34) | Albedo R: difference / allowed bound | 0.13999241590499878 / 2.384185791015625e-07; pool control 3 PASS | PASS fresh K=5; no pool resolution | `135624361` |
| 587x250 / 4 at (489,34) | Depth: difference / allowed bound | 0.105224609375 / 0.000244140625; pool control 3 PASS | PASS fresh K=5; no pool resolution | `135624361` |
| 47x20 / max16 | Spill read amplification | 2.607345x | 1.000000x | `135624361` |
| 587x250 / 4 | Spill read amplification | 7.570932x | 1.148825x | `135624361` |
| Strict identity | Payload + deterministic headers | PASS | 81/81 PASS | `135624361` |

| 47x20 / max16, scratch reuse | Export seconds | 8.30383 | 7.98998 (-3.8%) | `b602877dc` |
| 587x250 / 4, scratch reuse | Export seconds | 34.5202 | 32.9664 (-4.5%) | `b602877dc` |
| Scratch reuse | Strict payload + deterministic headers | 81/81 PASS | 81/81 PASS | `b602877dc` |
| Scratch reuse | Regression set | 9 CTests; CPU14/CUDA4; each boundary33+17; both landscapes PASS | All PASS; CPU beauty exact; CUDA K=5 PASS | `b602877dc` |

### Phase 2 - accepted

CUDA boundary pairing uses native BVH candidates, refined by the unchanged
double-precision triangle test and tie rules. Small bounds and other backends
retain the scan. Stack overflow fails explicitly. No beauty kernel or sampling
change; no approximation was introduced.

Baseline: `optimization-phase2/baseline.json` (qualified `b602877dc`). Clean
build/results: `optimization-phase2/after/phase-results.json` (`f6f9316d8`).
Evidence paths below are relative to `builds/validation/landscape-cloud/`.
Nsight Systems: `optimization-phase2/{nsys,nsys-after}/timeline.nsys-rep`,
`stats.csv`, `deep-only-timeline.json`. Counter access passed; no restart needed.
Nsight Compute: `optimization-phase2/ncu-stratified/deep-surface.ncu-repz` and
`source-attribution.json`. Fourteen invocations spanning the image/retry sequence
(including the longest launch) indicate 64.9% triangle work versus 6.7% grid
traversal. These are warp-residency PC shares weighted by launch duration,
not exclusive function timers or a whole-frame census. Clocks/caches were not
controlled. Kernel and host wait overlap; do not add them. Residual copy-API
wait subtracts GPU copy duration from API elapsed time and mostly waits for
queued kernels. Correlated deep-only copies account for all 7,177,170,944 bytes.

Actual renderer bound counts (`optimization-phase2/scene-inventory-measured.json`):
`Plane.030`: 22; `water2`: 2296; `cloud_01_variant_0000`, `cloud_02_variant_0000`,
`cloud_04_variant_0000`, `cloud_05_variant_0000`, `cloud_06_variant_0000`: 12 each;
`Cylinder.003`, `.005`, `.006`, `.007`, `.008`, `.009`, `.010`, `.011`, `.012`,
`.014`, `.015`: 124 each. Counts come from the updated renderer scene, not
viewport estimates; five other cloud objects are excluded from this render.

CPU beauty remains exact. The CUDA performance pixel (489,34) fails the fresh
K=5 envelope for noisy colour, albedo and depth but matches **all** checked
passes exactly in one retained ordinary render, Phase 0 K=5 performance off-3,
with four samples. It passes the approved reproduced-state rule; no new
controls or relaxed thresholds were needed. Small CUDA beauty passes K=5.
Both landscapes have zero unexplained denoised outliers.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 | Unprofiled capture seconds | 5.72722 | 2.892841 | `f6f9316d8` |
| 47x20 / max16 | Export seconds | 7.98998 | 7.98948 | `f6f9316d8` |
| 47x20 / max16 | Capture wait + readback seconds | 4.595921 | 1.7916789999999998 | `f6f9316d8` |
| 47x20 / max16 | GPU readback bytes | 372191232 | 372191232 | `f6f9316d8` |
| 47x20 / max16 | Spill stored bytes | 13367184 | 13367184 | `f6f9316d8` |
| 47x20 / max16 | Spill read bytes | 13367184 | 13367184 | `f6f9316d8` |
| 47x20 / max16 | Spill written bytes | 13728144 | 13728144 | `f6f9316d8` |
| 47x20 / max16 | EXR bytes | 4445714 | 4445714 | `f6f9316d8` |
| 47x20 / max16 | Peak process working-set bytes | 5525803008 | 5581307904 | `f6f9316d8` |
| 47x20 / max16 | Device-wide GPU peak MiB | 4980 | 4892 | `f6f9316d8` |
| 47x20 / max16 | Spill read amplification | 1 | 1 | `f6f9316d8` |
| 47x20 / max16 | Deep samples | 561793 | 561793 | `f6f9316d8` |
| 47x20 / max16 | Max oracle error | 2.201313769045754e-07 | 2.201313769045754e-07 | `f6f9316d8` |
| 47x20 / max16 | Max depth-cut error | 6.95131076811073e-07 | 6.95131076811073e-07 | `f6f9316d8` |
| 47x20 / max16 | Beauty / alpha / depth | PASS / PASS / PASS | PASS / PASS / PASS | `f6f9316d8` |
| 587x250 / 4 | Unprofiled capture seconds | 71.654783 | 33.279297 | `f6f9316d8` |
| 587x250 / 4 | Export seconds | 32.9664 | 32.9423 | `f6f9316d8` |
| 587x250 / 4 | Capture wait + readback seconds | 68.6366 | 30.32968 | `f6f9316d8` |
| 587x250 / 4 | GPU readback bytes | 7177170944 | 7177170944 | `f6f9316d8` |
| 587x250 / 4 | Spill stored bytes | 522463136 | 522463136 | `f6f9316d8` |
| 587x250 / 4 | Spill read bytes | 600218816 | 600218816 | `f6f9316d8` |
| 587x250 / 4 | Spill written bytes | 576484544 | 576484544 | `f6f9316d8` |
| 587x250 / 4 | EXR bytes | 394273944 | 394273944 | `f6f9316d8` |
| 587x250 / 4 | Peak process working-set bytes | 5583224832 | 5564792832 | `f6f9316d8` |
| 587x250 / 4 | Device-wide GPU peak MiB | 5053 | 4967 | `f6f9316d8` |
| 587x250 / 4 | Spill read amplification | 1.1488251986452112 | 1.1488251986452112 | `f6f9316d8` |
| 587x250 / 4 | Deep samples | 52189081 | 52189081 | `f6f9316d8` |
| 587x250 / 4 | Max oracle error | 2.2079907672709065e-07 | 2.2079907672709065e-07 | `f6f9316d8` |
| 587x250 / 4 | Max depth-cut error | 8.228034402701923e-07 | 8.228034402701923e-07 | `f6f9316d8` |
| 587x250 / 4 | Beauty / alpha / depth | PASS / PASS / PASS | PASS / PASS / PASS | `f6f9316d8` |
| 587x250 / 4 | Nsight deep kernel seconds | 66.259845126 | 28.063985331 | `f6f9316d8` |
| 587x250 / 4 | Deep kernel launches | 810 | 810 | `f6f9316d8` |
| 587x250 / 4 | Nsight capture wait + readback seconds | 68.7772 | 30.53258 | `f6f9316d8` |
| 587x250 / 4 | Deep-only device-to-host copy seconds | 2.208866966 | 2.189934185 | `f6f9316d8` |
| 587x250 / 4 | Copy API residual wait seconds | 66.502660044 | 28.281092466 | `f6f9316d8` |
| 587x250 / 4 | Explicit stream synchronization seconds | 0.012228238 | 0.010263851 | `f6f9316d8` |
| 587x250 / 4 | Host spill seconds | 1.2507890000000002 | 1.076664 | `f6f9316d8` |
| 14 source-sampled invocations | Triangle / grid estimated share | 64.9% / 6.7% | BVH enabled for CUDA bounds >32 triangles | `f6f9316d8` |
| sm_86 deep kernel | Registers / local bytes per thread | 168 / 7312 | 168 / 8000 (+688 bytes) | `f6f9316d8` |
| sm_86 deep kernel | Compiler spill store / load bytes | 80 / 76 | 80 / 76 | `f6f9316d8` |
| 75 common beauty kernels | Changed driver attribute sets | 0 | 0 | `f6f9316d8` |
| Nine CTests | Regression | 9/9 PASS | 9/9 PASS | `f6f9316d8` |
| CPU / CUDA compatibility | Regression | 14/14 / 4/4 PASS | 14/14 / 4/4 PASS | `f6f9316d8` |
| CPU and CUDA boundaries, each | Regression | 33 renders + 17 rejections PASS | 33 renders + 17 rejections PASS | `f6f9316d8` |
| Both landscapes | Strict payload + deterministic headers | 81/81 PASS | 81/81 PASS; profiled render also identical | `f6f9316d8` |
| 587x250 / 4, pixel (489,34) | CUDA reproduced state | Qualified reference pool retained | All checked passes exactly match Phase 0 K=5 performance off-3; 4 samples | `f6f9316d8` |

### Phase 3a - acceptance checks passed; stop for review

Host path only. `--deep-error`/Blender `deep_error` default to `1e-3`; zero in
Blender or `strict` on the CLI preserves legacy arithmetic and headers.
Non-strict headers record the effective FLOAT value. Validators read the EXR
bound. The budget comment in `src/deep/volume.h` reserves the fixed `1e-6`
publication floor and divides the remainder between per-ray fitting and host
mixture/reduction/coalescing. Surface reduction uses the same setting.
No device compression, early termination or object-index change is included.

Baseline (`195082afa`): `optimization-phase3a/baseline/results.json`.
Final source (`91507b29f` + timing correction `3660cc334`):
`optimization-phase3a/after/phase-results.json`. Paths are relative to
`builds/validation/landscape-cloud/`. Fitting, read and writer counters include
accepted-camera diagnostics; CSV writing and other overhead also contribute to
export wall time. Worker seconds overlap across threads and must not be added
as frame time. The final diagnostic decoder scope was corrected; its baseline
counter is not comparable. Other baseline stage counters remain valid.

After values below are **strict; 1e-4; 1e-3**, in that order. GPU readback and
spill traffic are unchanged: this stop changes host fitting only. Strict has
no new deterministic attributes. CPU beauty remains exact; CUDA uses the
unchanged K=5/reference-pool gate. All three tolerances pass both matrices,
both fixed cases, and their header-driven accepted-camera/depth-cut checks.

| Case | Metric | Before (strict) | After (strict; 1e-4; 1e-3) | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 | Capture / export s | 2.89726 / 8.00247 | 2.916723 / 8.07911; 2.901321 / 3.75926; 2.906456 / 3.72078 | `3660cc334` |
| 47x20 / max16 | Capture wait + readback s | 1.799462 | 1.811925; 1.812391; 1.804305 | `3660cc334` |
| 47x20 / max16 | GPU readback bytes | 372191232 | 372191232 (all modes) | `3660cc334` |
| 47x20 / max16 | CPU density / mixture fit worker-s | 2.56656 / 21.769 | 2.54351 / 21.6479; 0.286971 / 1.08179; 0.210884 / 0.440205 | `3660cc334` |
| 47x20 / max16 | Record reads / spill staging worker-s | 0.0059921 / 0.003966 | 0.0058093 / 0.0043051; 0.0043145 / 0.0039092; 0.0042945 / 0.0038541 | `3660cc334` |
| 47x20 / max16 | Quantize-coalesce / EXR serialization worker-s | 2.39928 / 0.0823963 | 2.40605 / 0.0806107; 0.117966 / 0.0129347; 0.0433572 / 0.0093205 | `3660cc334` |
| 47x20 / max16 | Spill stored / read / written bytes | 13367184 / 13367184 / 13728144 | 13367184 / 13367184 / 13728144 (all modes) | `3660cc334` |
| 47x20 / max16 | EXR bytes | 4445714 | 4445714; 592943; 341785 | `3660cc334` |
| 47x20 / max16 | Ledger/decode worker-s | Outer baseline scope omitted diagnostic decode | 0.637327; 0.0502123; 0.0330517 | `3660cc334` |
| 47x20 / max16 | Peak working-set bytes / device-wide GPU MiB | 5563379712 / 4752 | 5674639360 / 4733; 5587841024 / 4733; 5633286144 / 4730 | `3660cc334` |
| 47x20 / max16 | Deep samples | 561793 | 561793; 86914; 51451 | `3660cc334` |
| 47x20 / max16 | Max camera-oracle / depth-cut error | 2.201314e-07 / 6.951311e-07 | 2.201314e-07 / 6.951311e-07; 2.569492e-05 / 3.237709e-07; 0.0002488094 / 1.203699e-07 | `3660cc334` |
| 47x20 / max16 | Beauty / alpha / depth | PASS / PASS / PASS | PASS / PASS / PASS (all modes) | `3660cc334` |
| 587x250 / 4 | Capture / export s | 33.26628 / 33.4208 | 33.45382 / 33.1163; 33.26009 / 3.57575; 33.50932 / 2.30676 | `3660cc334` |
| 587x250 / 4 | Capture wait + readback s | 30.29766 | 30.4741; 30.28743; 30.5585 | `3660cc334` |
| 587x250 / 4 | GPU readback bytes | 7177170944 | 7177170944; 7177170944; 7176744960 | `3660cc334` |
| 587x250 / 4 | CPU density / mixture fit worker-s | 78.7117 / 243.776 | 78.1137 / 240.967; 4.68609 / 13.7718; 2.36626 / 7.35157 | `3660cc334` |
| 587x250 / 4 | Record reads / spill staging worker-s | 0.189819 / 0.215888 | 0.195007 / 0.216738; 0.145597 / 0.21593; 0.145244 / 0.216106 | `3660cc334` |
| 587x250 / 4 | Quantize-coalesce / EXR serialization worker-s | 144.306 / 7.1261 | 142.496 / 7.11506; 7.00059 / 0.984645; 2.39941 / 0.434441 | `3660cc334` |
| 587x250 / 4 | Spill stored / read / written bytes | 522463136 / 600218816 / 576484544 | 522463136 / 600218816 / 576484544 (all modes) | `3660cc334` |
| 587x250 / 4 | EXR bytes | 394273944 | 394273944; 56378732; 21488669 | `3660cc334` |
| 587x250 / 4 | Ledger/decode worker-s | Outer baseline scope omitted diagnostic decode | 1.16247; 0.422173; 0.390535 | `3660cc334` |
| 587x250 / 4 | Peak working-set bytes / device-wide GPU MiB | 5565448192 / 4826 | 5538795520 / 4807; 5571813376 / 4806; 5696569344 / 4807 | `3660cc334` |
| 587x250 / 4 | Deep samples | 52189081 | 52189081; 6487505; 2994249 | `3660cc334` |
| 587x250 / 4 | Max camera-oracle / depth-cut error | 2.207991e-07 / 8.228034e-07 | 2.207991e-07 / 8.228034e-07; 4.533494e-05 / 1.562399e-07; 0.0004034792 / 2.446518e-08 | `3660cc334` |
| 587x250 / 4 | Beauty / alpha / depth | PASS / PASS / PASS | PASS / PASS / PASS (all modes) | `3660cc334` |
| Strict identity | Payload + deterministic headers | 81/81 PASS | 81/81 PASS (strict) | `3660cc334` |
| Nine CTests | Regression | 9/9 PASS | 9/9 PASS | `3660cc334` |
| CPU14 / CUDA4 matrices | Regression | All PASS | All PASS (all modes) | `3660cc334` |
| Each CPU/CUDA boundary suite | Regression | 33 renders + 17 rejections PASS | 33 renders + 17 rejections PASS (strict) | `3660cc334` |
| Weak surface steps | Samples / max absolute error | 96 / 0.0009872007 | 96 / 0.0009872007; 768 / 8.751264e-05; 96 / 0.0009872007 | `3660cc334` |

No pixel required a reproduced-state pool resolution.

### Phase 3b - accepted

Implementation: `e1bebb202`; stronger fallback/tail assertions: `17d3bd411`.
No expansion; exact cubic fallback; bounded per-object opaque tails.
Device and host each receive half the density allowance. Objects split
the device allowance using the preflight scene count (capped at 64);
1/4 of device error covers representation/arithmetic, 3/4 curve/tail error.
Strict payload and deterministic headers: **81/81 identical**. Nine CTests,
all three-mode CPU14/CUDA4 matrices and both 33-case/17-rejection boundary
suites pass. Independent CPU/CUDA no-expansion/cubic oracles pass.
All fixed-case performance gates pass; no threshold or raw gate changed.

The denoised envelope is image-wide over ordinary K-run pairs and retains
qualified unchanged-beauty evidence. The earlier 1e-4 difference and
retained envelope both equal 0.005446791648864746. Identical saved input
passes denoised twice with CUDA OIDN: max difference 0;
this does not explain ordinary-render variation. All 75 common CUDA kernel
resource signatures remain identical; deep local storage is 8272 B
(3a: 8000 B), with 168 registers and unchanged occupancy recommendation.

Before: accepted 3a. After: this rework. Capture excludes compiler warm-up.
Host timings are aggregate worker seconds, not additive frame wall time.
The small 1e-3 timing was repeated once without the overlapping unit compile;
the original measurement is retained in `after-nonisolated`.
Evidence: `builds/validation/landscape-cloud/optimization-phase3b-rework/after/phase-results.json`;
`kernel-resources.json`, `oidn-repeat/results.json` and `retiming.json` in its parent.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 / strict | Capture s | 2.916723 | 2.889183 | `e1bebb202` |
| 47x20 / max16 / strict | Export wall s | 8.07911 | 7.9882 | `e1bebb202` |
| 47x20 / max16 / strict | GPU readback bytes | 372,191,232 | 372,191,232 | `e1bebb202` |
| 47x20 / max16 / strict | Spill stored bytes | 13,367,184 | 13,367,184 | `e1bebb202` |
| 47x20 / max16 / strict | Spill read / written bytes | 13,367,184 / 13,728,144 | 13,367,184 / 13,728,144 | `e1bebb202` |
| 47x20 / max16 / strict | Host read / staging worker s | 0.0058093 / 0.0043051 | 0.0061356 / 0.0039168 | `e1bebb202` |
| 47x20 / max16 / strict | Density / mixture fitting worker s | 2.54351 / 21.6479 | 2.58352 / 21.8789 | `e1bebb202` |
| 47x20 / max16 / strict | Decode / quantize / EXR worker s | 0.637327 / 2.40605 / 0.0806107 | 0.658482 / 2.46469 / 0.0768989 | `e1bebb202` |
| 47x20 / max16 / strict | Deep samples | 561,793 | 561,793 | `e1bebb202` |
| 47x20 / max16 / strict | EXR bytes | 4,445,714 | 4,445,714 | `e1bebb202` |
| 47x20 / max16 / strict | Max uncompressed camera oracle error | 2.20131377e-07 | 2.20131377e-07 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Capture s | 2.901321 | 2.216438 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Export wall s | 3.75926 | 3.73822 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | GPU readback bytes | 372,191,232 | 215,568,384 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Spill stored bytes | 13,367,184 | 9,163,356 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Spill read / written bytes | 13,367,184 / 13,728,144 | 9,163,356 / 9,524,316 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Host read / staging worker s | 0.0043145 / 0.0039092 | 0.0040376 / 0.0027392 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Density / mixture fitting worker s | 0.286971 / 1.08179 | 0.265886 / 0.963152 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Decode / quantize / EXR worker s | 0.0502123 / 0.117966 / 0.0129347 | 0.0404556 / 0.123236 / 0.0134707 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Deep samples | 86,914 | 88,699 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | EXR bytes | 592,943 | 609,496 | `e1bebb202` |
| 47x20 / max16 / 1e-4 | Max uncompressed camera oracle error | 2.56949157e-05 | 2.39682242e-05 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Capture s | 2.906456 | 2.224591 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Export wall s | 3.72078 | 3.55014 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | GPU readback bytes | 372,191,232 | 199,434,240 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Spill stored bytes | 13,367,184 | 8,183,848 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Spill read / written bytes | 13,367,184 / 13,728,144 | 8,183,848 / 8,544,808 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Host read / staging worker s | 0.0042945 / 0.0038541 | 0.0039235 / 0.0027959 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Density / mixture fitting worker s | 0.210884 / 0.440205 | 0.183858 / 0.374084 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Decode / quantize / EXR worker s | 0.0330517 / 0.0433572 / 0.0093205 | 0.0263485 / 0.0450083 / 0.0115115 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Deep samples | 51,451 | 53,113 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | EXR bytes | 341,785 | 357,145 | `e1bebb202` |
| 47x20 / max16 / 1e-3 | Max uncompressed camera oracle error | 0.000248809385 | 0.000240938097 | `e1bebb202` |
| 587x250 / max4 / strict | Capture s | 33.453818 | 33.676055 | `e1bebb202` |
| 587x250 / max4 / strict | Export wall s | 33.1163 | 33.1726 | `e1bebb202` |
| 587x250 / max4 / strict | GPU readback bytes | 7,177,170,944 | 7,177,170,944 | `e1bebb202` |
| 587x250 / max4 / strict | Spill stored bytes | 522,463,136 | 522,463,136 | `e1bebb202` |
| 587x250 / max4 / strict | Spill read / written bytes | 600,218,816 / 576,484,544 | 600,218,816 / 576,484,544 | `e1bebb202` |
| 587x250 / max4 / strict | Host read / staging worker s | 0.195007 / 0.216738 | 0.214757 / 0.202531 | `e1bebb202` |
| 587x250 / max4 / strict | Density / mixture fitting worker s | 78.1137 / 240.967 | 79.6647 / 242.23 | `e1bebb202` |
| 587x250 / max4 / strict | Decode / quantize / EXR worker s | 1.16247 / 142.496 / 7.11506 | 1.48819 / 143.538 / 7.01048 | `e1bebb202` |
| 587x250 / max4 / strict | Deep samples | 52,189,081 | 52,189,081 | `e1bebb202` |
| 587x250 / max4 / strict | EXR bytes | 394,273,944 | 394,273,944 | `e1bebb202` |
| 587x250 / max4 / strict | Max uncompressed camera oracle error | 2.20799077e-07 | 2.20799077e-07 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Capture s | 33.260089 | 26.516487 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Export wall s | 3.57575 | 3.85256 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | GPU readback bytes | 7,177,170,944 | 4,397,510,656 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Spill stored bytes | 522,463,136 | 358,081,036 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Spill read / written bytes | 600,218,816 / 576,484,544 | 437,199,436 / 413,133,844 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Host read / staging worker s | 0.145597 / 0.21593 | 0.146821 / 0.150126 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Density / mixture fitting worker s | 4.68609 / 13.7718 | 5.45923 / 17.4255 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Decode / quantize / EXR worker s | 0.422173 / 7.00059 / 0.984645 | 0.528353 / 7.10847 / 1.03382 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Deep samples | 6,487,505 | 6,813,973 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | EXR bytes | 56,378,732 | 59,210,585 | `e1bebb202` |
| 587x250 / max4 / 1e-4 | Max uncompressed camera oracle error | 4.53349437e-05 | 3.28872024e-05 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Capture s | 33.509321 | 26.219204 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Export wall s | 2.30676 | 2.18183 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | GPU readback bytes | 7,176,744,960 | 4,089,827,328 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Spill stored bytes | 522,463,136 | 319,349,160 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Spill read / written bytes | 600,218,816 / 576,484,544 | 398,116,680 / 374,707,800 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Host read / staging worker s | 0.145244 / 0.216106 | 0.134456 / 0.134174 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Density / mixture fitting worker s | 2.36626 / 7.35157 | 1.98848 / 6.94466 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Decode / quantize / EXR worker s | 0.390535 / 2.39941 / 0.434441 | 0.470341 / 2.38269 / 0.461234 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Deep samples | 2,994,249 | 3,076,802 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | EXR bytes | 21,488,669 | 22,398,061 | `e1bebb202` |
| 587x250 / max4 / 1e-3 | Max uncompressed camera oracle error | 0.000403479216 | 0.00031719342 | `e1bebb202` |

The larger 1e-4 EXR grows about 5% and host fitting increases; capture,
readback and spill improve in both numeric modes.

### Phase 3c - accepted

Implementation: `00c2fd7fd`; cross-object rejection test: `cb0208aef`.
Native object indices survive CPU/CUDA capture, strided readback, direct spill,
re-bucketing and staged host reads. Reconstruction ignores them; no new error
allowance or EXR channel. Strict payload and deterministic headers: **81/81 identical**.
Numeric identity: **30/30**, covering both fixed cases and all rendered matrix
cases at 1e-4/1e-3. Final-build smoke: 3/3 identical. Nine CTests, all three-mode
CPU14/CUDA4 matrices, both boundary suites and independent CPU/CUDA oracles pass.
The event is now 24 B (was 20 B); companion records remain 32 B. Batch capacity
changes from 64 to 62 to retain the 32 MiB staging reservation. Beauty source
comparison passes; the GPU host header uses the existing guarded-block rule.
Surface/homogeneous batches use 480 lanes, retaining their 2 MiB reservation
(`57f61ec02`). Matrices/boundaries use this final build; measured native-grid
code is unchanged. All 75 common CUDA resource signatures remain unchanged.

Before: six fresh runs with accepted 3b. Times exclude compiler warm-up.
Fitting and other host stages are aggregate worker seconds, not additive wall time.
Evidence: `builds/validation/landscape-cloud/optimization-phase3c/after/phase-results.json`;
fresh measurements: `before/`; source comparison and kernel resources in its parent.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 / strict | Capture (s) | 2.950011 | 2.842184 | `00c2fd7fd` |
| 47x20 / max16 / strict | Wait + readback (s) | 1.83937 | 1.742509 | `00c2fd7fd` |
| 47x20 / max16 / strict | GPU bytes copied | 372,191,232 | 387,364,096 | `00c2fd7fd` |
| 47x20 / max16 / strict | Spill stored / read / written (B) | 13,367,184.0 / 13,367,184.0 / 13,728,144.0 | 14,443,168.0 / 14,443,168.0 / 14,804,128.0 | `00c2fd7fd` |
| 47x20 / max16 / strict | Export (s) | 8.38273 | 8.0145 | `00c2fd7fd` |
| 47x20 / max16 / strict | Density / mixture fit (worker s) | 2.70522 / 22.8514 | 2.40496 / 20.3233 | `00c2fd7fd` |
| 47x20 / max16 / strict | Read / staging / ledger / quantize / serialize (worker s) | 0.0068113 / 0.0040885 / 0.678403 / 2.4811 / 0.0844292 | 0.0059449 / 0.004244 / 0.609406 / 2.27037 / 0.0809585 | `00c2fd7fd` |
| 47x20 / max16 / strict | Deep samples / EXR bytes | 561,793 / 4,445,714 | 561,793 / 4,445,714 | `00c2fd7fd` |
| 47x20 / max16 / strict | Peak host bytes / device-wide MiB | 5,585,858,560 / 6174 | 5,571,067,904 / 6183 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Capture (s) | 2.297569 | 2.236982 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Wait + readback (s) | 1.153981 | 1.1478093 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | GPU bytes copied | 215,568,384 | 223,319,040 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Spill stored / read / written (B) | 9,163,356.0 / 9,163,356.0 / 9,524,316.0 | 9,894,856.0 / 9,894,856.0 / 10,255,816.0 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Export (s) | 3.82664 | 3.82082 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Density / mixture fit (worker s) | 0.268856 / 0.969461 | 0.266714 / 0.95528 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Read / staging / ledger / quantize / serialize (worker s) | 0.0041683 / 0.0029566 / 0.0413897 / 0.122729 / 0.0139647 | 0.0040686 / 0.0030499 / 0.0414437 / 0.119347 / 0.0146214 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Deep samples / EXR bytes | 88,699 / 609,496 | 88,699 / 609,496 | `00c2fd7fd` |
| 47x20 / max16 / 1e-4 | Peak host bytes / device-wide MiB | 5,567,250,432 / 6172 | 5,603,799,040 / 6174 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Capture (s) | 2.270727 | 2.214799 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Wait + readback (s) | 1.1575605 | 1.1300219 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | GPU bytes copied | 199,434,240 | 205,986,816 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Spill stored / read / written (B) | 8,183,848.0 / 8,183,848.0 / 8,544,808.0 | 8,839,824.0 / 8,839,824.0 / 9,200,784.0 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Export (s) | 3.79466 | 3.73446 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Density / mixture fit (worker s) | 0.189328 / 0.376931 | 0.187253 / 0.370986 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Read / staging / ledger / quantize / serialize (worker s) | 0.0040472 / 0.0028103 / 0.0273725 / 0.045606 / 0.0103334 | 0.0039051 / 0.00289 / 0.027272 / 0.0444759 / 0.0106183 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Deep samples / EXR bytes | 53,113 / 357,145 | 53,113 / 357,145 | `00c2fd7fd` |
| 47x20 / max16 / 1e-3 | Peak host bytes / device-wide MiB | 5,693,763,584 / 6172 | 5,561,847,808 / 6174 | `00c2fd7fd` |
| 587x250 / max4 / strict | Capture (s) | 44.006672 | 34.166353 | `00c2fd7fd` |
| 587x250 / max4 / strict | Wait + readback (s) | 40.4466 | 31.11158 | `00c2fd7fd` |
| 587x250 / max4 / strict | GPU bytes copied | 7,177,170,944 | 7,695,868,544 | `00c2fd7fd` |
| 587x250 / max4 / strict | Spill stored / read / written (B) | 522,463,136.0 / 600,218,816.0 / 576,484,544.0 | 564,528,704.0 / 641,974,304.0 / 618,486,776.0 | `00c2fd7fd` |
| 587x250 / max4 / strict | Export (s) | 33.8851 | 32.9733 | `00c2fd7fd` |
| 587x250 / max4 / strict | Density / mixture fit (worker s) | 81.8269 / 247.658 | 77.6507 / 241.461 | `00c2fd7fd` |
| 587x250 / max4 / strict | Read / staging / ledger / quantize / serialize (worker s) | 0.224979 / 0.219892 / 1.5585 / 144.609 / 7.3259 | 0.222217 / 0.229087 / 1.50808 / 141.263 / 7.26714 | `00c2fd7fd` |
| 587x250 / max4 / strict | Deep samples / EXR bytes | 52,189,081 / 394,273,944 | 52,189,081 / 394,273,944 | `00c2fd7fd` |
| 587x250 / max4 / strict | Peak host bytes / device-wide MiB | 5,636,321,280 / 6291 | 5,563,826,176 / 6257 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Capture (s) | 26.698422 | 27.105229 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Wait + readback (s) | 24.00399 | 24.36521 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | GPU bytes copied | 4,397,510,656 | 4,743,196,416 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Spill stored / read / written (B) | 358,081,036.0 / 437,199,436.0 / 413,133,844.0 | 386,674,056.0 / 465,351,816.0 / 441,611,904.0 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Export (s) | 3.82545 | 3.85133 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Density / mixture fit (worker s) | 5.53602 / 17.584 | 5.39616 / 17.374 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Read / staging / ledger / quantize / serialize (worker s) | 0.147879 / 0.149887 / 0.536136 / 7.17478 / 1.04653 | 0.145314 / 0.162715 / 0.543304 / 7.10275 / 1.05699 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Deep samples / EXR bytes | 6,813,973 / 59,210,585 | 6,813,973 / 59,210,585 | `00c2fd7fd` |
| 587x250 / max4 / 1e-4 | Peak host bytes / device-wide MiB | 5,546,123,264 / 6276 | 5,551,501,312 / 6238 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Capture (s) | 26.246567 | 26.623797 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Wait + readback (s) | 23.64787 | 23.97672 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | GPU bytes copied | 4,089,827,328 | 4,411,939,840 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Spill stored / read / written (B) | 319,349,160.0 / 398,116,680.0 / 374,707,800.0 | 344,954,864.0 / 423,342,944.0 / 400,152,008.0 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Export (s) | 2.15671 | 2.18591 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Density / mixture fit (worker s) | 2.0061 / 7.02473 | 1.95087 / 6.96777 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Read / staging / ledger / quantize / serialize (worker s) | 0.137136 / 0.132579 / 0.48058 / 2.40472 / 0.450634 | 0.137164 / 0.152549 / 0.488909 / 2.38537 / 0.463073 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Deep samples / EXR bytes | 3,076,802 / 22,398,061 | 3,076,802 / 22,398,061 | `00c2fd7fd` |
| 587x250 / max4 / 1e-3 | Peak host bytes / device-wide MiB | 5,617,336,320 / 6243 | 5,767,208,960 / 6224 | `00c2fd7fd` |

### Phase 4 - accepted

Implementation: `6ad4e831f`; pinned buffers/drain: `7c4c67d5b`; compact density: `405df643b`.
Numeric capture counts once, assigns bounded flat event/companion ranges, and uses
two pinned buffers/queues. Copies contain emitted payload and required metadata.
Strict retains the plane retry path. Event kind occupies the low two bits;
object index occupies the upper thirty bits; event/spill ABI is 20 bytes.
No new approximation or beauty change. Strict **81/81**, numeric **30/30** identical.
Nine CTests, all-mode CPU/CUDA matrices, both boundary suites and CUDA lifecycle/
queued-DMA error cleanup pass. Both fixed-case raw/denoised beauty gates pass.

Before times/copied bytes: six fresh accepted-3c renders. Lane bytes are logical
event/companion stores, including discarded retries; exclude scratch/metadata.
Before lane bytes apply the original 24/32-byte ABI to counters from an instrumented
unchanged plane-schedule replay (`legacy-counter-check/`), byte-checked on all six cases.
After uses 20/32-byte stores. Copies include metadata; worker times are not wall time.
Evidence: `builds/validation/landscape-cloud/optimization-phase4/after/phase-results.json`.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 / strict | Capture / wait+readback (s) | 3.617905 / 2.222515 | 2.854133 / 1.747888 | `405df643b` |
| 47x20 / max16 / strict | Lane payload bytes actually written | 28,566,240 | 26,307,056 | `405df643b` |
| 47x20 / max16 / strict | GPU bytes copied | 387,364,096 | 360,742,784 | `405df643b` |
| 47x20 / max16 / strict | Spill stored / read / written (B) | 14,443,168 / 14,443,168 / 14,804,128 | 13,367,184 / 13,367,184 / 13,728,144 | `405df643b` |
| 47x20 / max16 / strict | Export (s) | 8.17157 | 8.51457 | `405df643b` |
| 47x20 / max16 / strict | Density / mixture fit (worker s) | 2.59887 / 22.6421 | 2.50605 / 21.6402 | `405df643b` |
| 47x20 / max16 / strict | Read / staging / ledger / quantize / serialize (worker s) | 0.0082026 / 0.0051755 / 0.718835 / 2.52159 / 0.0792859 | 0.0062315 / 0.0038644 / 0.642922 / 2.38462 / 0.0782297 | `405df643b` |
| 47x20 / max16 / strict | Deep samples / EXR bytes | 561,793 / 4,445,714 | 561,793 / 4,445,714 | `405df643b` |
| 47x20 / max16 / strict | Peak host bytes / device-wide MiB | 5,596,643,328 / 6282 | 5,579,268,096 / 6251 | `405df643b` |
| 47x20 / max16 / strict | Max accepted-camera oracle error | unchanged payload | 2.2013138e-07 | `405df643b` |
| 47x20 / max16 / 1e-4 | Capture / wait+readback (s) | 2.907643 / 1.485142 | 1.792865 / 0.6998938 | `405df643b` |
| 47x20 / max16 / 1e-4 | Lane payload bytes actually written | 16,628,312 | 8,441,436 | `405df643b` |
| 47x20 / max16 / 1e-4 | GPU bytes copied | 223,319,040 | 9,403,996 | `405df643b` |
| 47x20 / max16 / 1e-4 | Spill stored / read / written (B) | 9,894,856 / 9,894,856 / 10,255,816 | 9,163,356 / 9,163,356 / 9,524,316 | `405df643b` |
| 47x20 / max16 / 1e-4 | Export (s) | 3.81418 | 3.76972 | `405df643b` |
| 47x20 / max16 / 1e-4 | Density / mixture fit (worker s) | 0.272271 / 0.974862 | 0.258975 / 0.939619 | `405df643b` |
| 47x20 / max16 / 1e-4 | Read / staging / ledger / quantize / serialize (worker s) | 0.0054193 / 0.0034273 / 0.0503082 / 0.122717 / 0.0150086 | 0.0040802 / 0.0030258 / 0.0387229 / 0.119212 / 0.0133339 | `405df643b` |
| 47x20 / max16 / 1e-4 | Deep samples / EXR bytes | 88,699 / 609,496 | 88,699 / 609,496 | `405df643b` |
| 47x20 / max16 / 1e-4 | Peak host bytes / device-wide MiB | 5,715,320,832 / 6292 | 5,608,656,896 / 6261 | `405df643b` |
| 47x20 / max16 / 1e-4 | Max accepted-camera oracle error | unchanged payload | 2.3968224e-05 | `405df643b` |
| 47x20 / max16 / 1e-3 | Capture / wait+readback (s) | 2.827886 / 1.424176 | 1.758648 / 0.6794962 | `405df643b` |
| 47x20 / max16 / 1e-3 | Lane payload bytes actually written | 15,232,456 | 7,461,928 | `405df643b` |
| 47x20 / max16 / 1e-3 | GPU bytes copied | 205,986,816 | 8,424,488 | `405df643b` |
| 47x20 / max16 / 1e-3 | Spill stored / read / written (B) | 8,839,824 / 8,839,824 / 9,200,784 | 8,183,848 / 8,183,848 / 8,544,808 | `405df643b` |
| 47x20 / max16 / 1e-3 | Export (s) | 3.67868 | 3.56989 | `405df643b` |
| 47x20 / max16 / 1e-3 | Density / mixture fit (worker s) | 0.186181 / 0.369666 | 0.180998 / 0.373479 | `405df643b` |
| 47x20 / max16 / 1e-3 | Read / staging / ledger / quantize / serialize (worker s) | 0.0044256 / 0.0037826 / 0.0284737 / 0.0441312 / 0.0115226 | 0.0039858 / 0.0025839 / 0.0262299 / 0.0440699 / 0.0101053 | `405df643b` |
| 47x20 / max16 / 1e-3 | Deep samples / EXR bytes | 53,113 / 357,145 | 53,113 / 357,145 | `405df643b` |
| 47x20 / max16 / 1e-3 | Peak host bytes / device-wide MiB | 5,582,516,224 / 6280 | 5,565,992,960 / 6261 | `405df643b` |
| 47x20 / max16 / 1e-3 | Max accepted-camera oracle error | unchanged payload | 0.0002409381 | `405df643b` |
| 587x250 / max4 / strict | Capture / wait+readback (s) | 34.456641 / 31.4241 | 33.977451 / 31.00023 | `405df643b` |
| 587x250 / max4 / strict | Lane payload bytes actually written | 961,886,432 | 886,604,976 | `405df643b` |
| 587x250 / max4 / strict | GPU bytes copied | 7,695,868,544 | 7,165,876,672 | `405df643b` |
| 587x250 / max4 / strict | Spill stored / read / written (B) | 564,528,704 / 641,974,304 / 618,486,776 | 522,463,136 / 599,908,736 / 576,421,208 | `405df643b` |
| 587x250 / max4 / strict | Export (s) | 33.0363 | 33.1449 | `405df643b` |
| 587x250 / max4 / strict | Density / mixture fit (worker s) | 76.9261 / 241.783 | 76.8807 / 239.49 | `405df643b` |
| 587x250 / max4 / strict | Read / staging / ledger / quantize / serialize (worker s) | 0.222827 / 0.227663 / 1.50642 / 142.737 / 7.01567 | 0.215885 / 0.215081 / 1.2784 / 141.639 / 7.155 | `405df643b` |
| 587x250 / max4 / strict | Deep samples / EXR bytes | 52,189,081 / 394,273,944 | 52,189,081 / 394,273,944 | `405df643b` |
| 587x250 / max4 / strict | Peak host bytes / device-wide MiB | 5,655,724,032 / 6311 | 5,747,793,920 / 6325 | `405df643b` |
| 587x250 / max4 / strict | Max accepted-camera oracle error | unchanged payload | 2.2079908e-07 | `405df643b` |
| 587x250 / max4 / 1e-4 | Capture / wait+readback (s) | 27.271883 / 24.54671 | 11.590895 / 9.00449 | `405df643b` |
| 587x250 / max4 / 1e-4 | Lane payload bytes actually written | 635,631,432 | 329,905,036 | `405df643b` |
| 587x250 / max4 / 1e-4 | GPU bytes copied | 4,743,196,416 | 367,473,036 | `405df643b` |
| 587x250 / max4 / 1e-4 | Spill stored / read / written (B) | 386,674,056 / 465,351,816 / 441,611,904 | 358,081,036 / 430,818,316 / 411,423,916 | `405df643b` |
| 587x250 / max4 / 1e-4 | Export (s) | 3.83371 | 3.71005 | `405df643b` |
| 587x250 / max4 / 1e-4 | Density / mixture fit (worker s) | 5.3443 / 17.3344 | 5.27332 / 17.1181 | `405df643b` |
| 587x250 / max4 / 1e-4 | Read / staging / ledger / quantize / serialize (worker s) | 0.154539 / 0.167225 / 0.542736 / 7.04468 / 1.03599 | 0.146292 / 0.147274 / 0.517254 / 6.97906 / 1.0181 | `405df643b` |
| 587x250 / max4 / 1e-4 | Deep samples / EXR bytes | 6,813,973 / 59,210,585 | 6,813,973 / 59,210,585 | `405df643b` |
| 587x250 / max4 / 1e-4 | Peak host bytes / device-wide MiB | 5,611,040,768 / 6308 | 5,524,942,848 / 6337 | `405df643b` |
| 587x250 / max4 / 1e-4 | Max accepted-camera oracle error | unchanged payload | 3.2887202e-05 | `405df643b` |
| 587x250 / max4 / 1e-3 | Capture / wait+readback (s) | 26.907714 / 24.24201 | 11.581321 / 9.0634 | `405df643b` |
| 587x250 / max4 / 1e-3 | Lane payload bytes actually written | 580,095,952 | 291,173,160 | `405df643b` |
| 587x250 / max4 / 1e-3 | GPU bytes copied | 4,411,939,840 | 328,741,160 | `405df643b` |
| 587x250 / max4 / 1e-3 | Spill stored / read / written (B) | 344,954,864 / 423,342,944 / 400,152,008 | 319,349,160 / 392,123,160 / 372,743,904 | `405df643b` |
| 587x250 / max4 / 1e-3 | Export (s) | 2.21531 | 2.16513 | `405df643b` |
| 587x250 / max4 / 1e-3 | Density / mixture fit (worker s) | 1.94407 / 6.92961 | 1.93253 / 6.8879 | `405df643b` |
| 587x250 / max4 / 1e-3 | Read / staging / ledger / quantize / serialize (worker s) | 0.140339 / 0.158946 / 0.483878 / 2.377 / 0.465303 | 0.138195 / 0.145489 / 0.475867 / 2.36351 / 0.464082 | `405df643b` |
| 587x250 / max4 / 1e-3 | Deep samples / EXR bytes | 3,076,802 / 22,398,061 | 3,076,802 / 22,398,061 | `405df643b` |
| 587x250 / max4 / 1e-3 | Peak host bytes / device-wide MiB | 5,792,567,296 / 6310 | 5,699,452,928 / 6337 | `405df643b` |
| 587x250 / max4 / 1e-3 | Max accepted-camera oracle error | unchanged payload | 0.00031719342 | `405df643b` |

### Phase 5 - implementation accepted; realistic case review

Implementation: `6a9d4831d`; standalone checks: `2a1a0bebb`.
Native-prefix checks: `747c054e4`; adaptive count decoding: `2fde67322`.
`deep.samples` / `--deep-samples` / Blender `deep_samples` retain the first N
accepted cameras; allocation and adaptive populations use min(beauty, N).
Beauty sampling/kernels remain unchanged. N=0 retains legacy payload/headers;
positive N publishes the effective maximum in integer `cycles:deepSamples`.
Header-driven oracles compare the same prefix, including misses. Sampling
difference from all cameras is informational, outside the curve error bound.
Strict **81/81**, numeric **30/30** identical. Nine CTests, all-mode CPU/CUDA
matrices, both boundary suites, CUDA lifecycle and extra cap=1 matrices pass.
Standalone 0/1/2/64 and negative atomic rejection pass. Small landscape: 128
fixed beauty samples / deep64 passes raw/denoised beauty, alpha and depth cuts;
an independent exact 128-camera capture supplies the first-64 oracle.
CPU/CUDA adaptive fixtures configured for 64 stop at 16; deep64 retains 16.
The count-pass validator uses completed-tile normalization, not the configured
maximum. Saved cap=1 matrices also revalidate; raw beauty/error gates are unchanged.
Before: six fresh Phase 4 measurements; accepted Phase 4 regression evidence
is the unchanged-executable baseline. After: fresh renders/regressions.
Evidence: `builds/validation/landscape-cloud/optimization-phase5/after/phase-results.json`.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20 / max16 / strict / N=0 | Capture / wait+readback (s) | 2.92595 / 1.798059 | 2.854375 / 1.747414 | `6a9d4831d` |
| 47x20 / max16 / strict / N=0 | GPU copied (B) | 360,742,784 | 360,742,784 | `6a9d4831d` |
| 47x20 / max16 / strict / N=0 | Spill stored / read / written (B) | 13,367,184 / 13,367,184 / 13,728,144 | 13,367,184 / 13,367,184 / 13,728,144 | `6a9d4831d` |
| 47x20 / max16 / strict / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 8.38125 / 2.5578 / 22.0527 | 7.88391 / 2.54401 / 21.684 | `6a9d4831d` |
| 47x20 / max16 / strict / N=0 | Deep samples / EXR bytes | 561,793 / 4,445,714 | 561,793 / 4,445,714 | `6a9d4831d` |
| 47x20 / max16 / strict / N=0 | Host peak bytes / device-wide MiB | 5,569,204,224 / 6984 | 5,579,472,896 / 7010 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | Capture / wait+readback (s) | 1.818553 / 0.7169116 | 1.790431 / 0.7024353 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | GPU copied (B) | 9,403,996 | 9,403,996 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | Spill stored / read / written (B) | 9,163,356 / 9,163,356 / 9,524,316 | 9,163,356 / 9,163,356 / 9,524,316 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 3.72705 / 0.262987 / 0.948668 | 3.65294 / 0.262267 / 0.950865 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | Deep samples / EXR bytes | 88,699 / 609,496 | 88,699 / 609,496 | `6a9d4831d` |
| 47x20 / max16 / 1e-4 / N=0 | Host peak bytes / device-wide MiB | 5,552,443,392 / 6996 | 5,792,051,200 / 7020 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | Capture / wait+readback (s) | 1.791999 / 0.6924988 | 1.7664 / 0.6838288 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | GPU copied (B) | 8,424,488 | 8,424,488 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | Spill stored / read / written (B) | 8,183,848 / 8,183,848 / 8,544,808 | 8,183,848 / 8,183,848 / 8,544,808 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 3.85094 / 0.189834 / 0.365408 | 3.54628 / 0.183865 / 0.375794 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | Deep samples / EXR bytes | 53,113 / 357,145 | 53,113 / 357,145 | `6a9d4831d` |
| 47x20 / max16 / 1e-3 / N=0 | Host peak bytes / device-wide MiB | 5,632,786,432 / 6999 | 5,572,628,480 / 7020 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | Capture / wait+readback (s) | 34.765137 / 31.6784 | 34.453202 / 31.4221 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | GPU copied (B) | 7,165,876,672 | 7,165,876,672 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | Spill stored / read / written (B) | 522,463,136 / 599,908,736 / 576,421,208 | 522,463,136 / 599,908,736 / 576,421,208 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 33.047 / 77.3143 / 241.084 | 32.9055 / 78.19 / 240.456 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | Deep samples / EXR bytes | 52,189,081 / 394,273,944 | 52,189,081 / 394,273,944 | `6a9d4831d` |
| 587x250 / max4 / strict / N=0 | Host peak bytes / device-wide MiB | 5,594,886,144 / 7055 | 5,570,351,104 / 7085 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | Capture / wait+readback (s) | 11.767692 / 9.13198 | 11.574708 / 8.99999 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | GPU copied (B) | 367,473,036 | 367,473,036 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | Spill stored / read / written (B) | 358,081,036 / 430,818,316 / 411,423,916 | 358,081,036 / 430,818,316 / 411,423,916 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 3.77937 / 5.30241 / 17.1869 | 3.78521 / 5.38834 / 17.2705 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | Deep samples / EXR bytes | 6,813,973 / 59,210,585 | 6,813,973 / 59,210,585 | `6a9d4831d` |
| 587x250 / max4 / 1e-4 / N=0 | Host peak bytes / device-wide MiB | 5,650,636,800 / 7065 | 5,768,765,440 / 7097 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | Capture / wait+readback (s) | 11.733663 / 9.19359 | 11.575433 / 9.07161 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | GPU copied (B) | 328,741,160 | 328,741,160 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | Spill stored / read / written (B) | 319,349,160 / 392,123,160 / 372,743,904 | 319,349,160 / 392,123,160 / 372,743,904 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | Export / density fit / mixture fit (s; fits aggregate worker time) | 2.18527 / 1.94462 / 6.934 | 2.20517 / 1.96491 / 6.95764 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | Deep samples / EXR bytes | 3,076,802 / 22,398,061 | 3,076,802 / 22,398,061 | `6a9d4831d` |
| 587x250 / max4 / 1e-3 / N=0 | Host peak bytes / device-wide MiB | 5,601,046,528 / 7065 | 5,605,146,624 / 7096 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Capture (s) | 4.850937 | 3.852259 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | GPU copied (B) | 67,398,544 | 35,681,116 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Spill stored / read / written (B) | 65,473,424 / 65,726,384 / 68,361,104 | 32,793,436 / 32,793,436 / 34,237,276 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Export / density fit / mixture fit (s; fits aggregate worker time) | 28.1464 / 1.42005 / 3.63017 | 14.1351 / 0.718485 / 1.73813 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Deep samples / EXR bytes | 314,554 / 1,867,912 | 164,522 / 948,420 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Host peak bytes / device-wide MiB | 5,639,651,328 / 7021 | 5,541,052,416 / 7021 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Max exact-camera oracle error | 0.00022681347 | 0.00023323567 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Edge alpha max / mean difference (information) | reference | 5.9604645e-08 / 4.7095028e-08 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Cut z=488.322: fractional-edge alpha max / mean difference (518 pixels; information) | reference | 0.0015625581 / 7.5461934e-05 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Cut z=627.309: fractional-edge alpha max / mean difference (940 pixels; information) | reference | 5.9604645e-08 / 5.9604645e-08 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Cut z=766.296: fractional-edge alpha max / mean difference (934 pixels; information) | reference | 0.020476639 / 0.00045326461 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Cut z=905.282: fractional-edge alpha max / mean difference (545 pixels; information) | reference | 0.077348173 / 0.0027293571 | `6a9d4831d` |
| 47x20 / fixed128 / 1e-3; all vs N=64 | Cut z=1044.27: fractional-edge alpha max / mean difference (153 pixels; information) | reference | 0.028125048 / 0.007280516 | `6a9d4831d` |
| CPU / CUDA adaptive64 | Native accepted / deep retained cameras | configured maximum 64 | 16 / 16 on both devices | `2fde67322` |

Phase 6 qualification is complete and ready for acceptance review below.
No full-resolution production render was launched.

#### Phase 5 realistic landscape - review stop

Implementation accepted; requested realistic case now measured. Engine
`6a9d4831d`; validator `2fde67322`. Unchanged
prepared landscape, 117x50, original max1024 adaptive / threshold0.03 / min8,
GPU OIDN, CUDA, 24 threads, `deep-error=1e-3`, 8192 MiB deep host budget.
Five ordinary controls provide the unchanged CUDA beauty gate and timing median.
Capture overhead is a paired subtraction; the scheduler timer includes beauty,
capture and denoising. Reported render-call time additionally includes scene
synchronization and beauty/pass saving; process startup and external validation
are excluded. Render+capture is render-call time minus measured export.
Fit timers sum worker time and are not additive frame wall times. Bytes are exact.

| Measured metric | Deep samples 0 (all accepted) | Deep samples 64 |
| --- | --- | --- |
| Beauty-only render-call time (median five controls) | 31.354 s | 31.354 s |
| Render + capture (render-call time minus export) | 114.103 s | 40.952 s |
| Incremental capture overhead (deep-on minus beauty median) | 82.750 s | 9.598 s |
| Capture wait/copy timer | 79.043 s | 9.588 s |
| Export wall time | 111.980 s | 14.816 s |
| Density / mixture fit (aggregate worker s) | 13.620 / 98.336 | 1.666 / 9.886 |
| GPU copied bytes | 1,873,948,088 | 388,440,124 |
| GPU lane-written bytes | 1,599,948,216 | 185,147,964 |
| Spill stored / read / written bytes | 1,805,448,120 / 2,053,690,632 / 1,879,310,592 | 202,833,084 / 221,652,924 / 211,783,020 |
| EXR bytes | 43,733,868 | 7,312,462 |
| Deep output samples | 6,498,864 | 972,866 |
| Accepted beauty samples/pixel, min / median / max | 16 / 384.0 / 1024 | 16 / 384.0 / 1024 |
| Retained deep cameras/pixel, min / median / max | 16 / 384.0 / 1024 | 16 / 64.0 / 64 |
| Accepted-camera oracle maximum absolute error (81 pixels) | 0.00021626982 | 0.00022625082 |
| Measured peak host working set | 5.26 GiB | 5.35 GiB |
| Measured device-wide GPU peak | 5357 MiB | 5342 MiB |

Oracle: 81 diagnostic pixels, output versus accepted captured camera curves.
Device compression is covered by Phase 3b exact-cubic bounds/tests; this case
does not repeat a strict-device reference. Parallel checks cover the original
probes: a complete pixel and the 35-pixel serial prefix reproduce exact results.
The reported reconstruction error is distinct from the sampling difference
between all cameras and the first64. Native accepted-count distributions cover
all 5,850 pixels; retained deep cameras use min(native count, header limit).

| Sampling difference (information; alpha absolute) | Max / mean, all pixels | Fractional-edge max / mean |
| --- | --- | --- |
| Full flatten | 5.9604645e-08 / 1.2399804e-08 | n/a (no fractional-edge pixels) |
| Far cut z=489.182 | 0.0020380393 / 4.2245124e-05 | 0.0020380393 / 7.845523e-05 (3150 pixels) |
| Far cut z=625.614 | 1.5646219e-07 / 3.3117512e-08 | 1.5646219e-07 / 3.3117512e-08 (5850 pixels) |
| Far cut z=762.046 | 0.022850156 / 0.00024028422 | 0.022850156 / 0.00024424934 (5755 pixels) |
| Far cut z=898.478 | 0.064085126 / 0.00098350451 | 0.064085126 / 0.0017128516 (3359 pixels) |
| Far cut z=1034.91 | 0.056493849 / 0.00059562561 | 0.056493849 / 0.0042388654 (822 pixels) |

**1175x500 projection: ESTIMATE, not a render.** Exact pixel ratio
100.427350 (~100x); same hardware/settings and accepted-sample
distribution assumed. Render+capture, export and disk bytes scale linearly by
that ratio. This also scales fixed kernel/denoising overhead, so timing is a
simple extrapolation; improved occupancy can make it faster. Export includes
81 fixed diagnostic pixels; scaling that overhead by100 is conservative.
Full-resolution adaptive convergence, geometry coverage and compression may change these costs.

| Estimated metric | Deep samples 0 | Deep samples 64 |
| --- | --- | --- |
| Render + capture | 190.98 min | 68.55 min |
| Export | 187.43 min | 24.80 min |
| Combined render + capture + export | 378.42 min | 93.34 min |
| Spill disk stored | 181.316 GB | 20.370 GB |
| EXR size | 4.392 GB | 0.734 GB |
| Peak host: conservative budget envelope | 13.93 GiB | 13.93 GiB |
| Peak device-wide GPU estimate | 5925 MiB | 5910 MiB |

Memory is not multiplied by100: scene/path state and deep GPU batches are
fixed; host deep working storage stays bounded by the same 8 GiB budget.
Host envelope = measured beauty-only process peak + full 8 GiB deep budget
+ 1 KiB per added pixel for native buffers/denoising. GPU estimate = each
measured device-wide peak + that pixel allowance; other applications assumed
unchanged. The 1 KiB/pixel allowance is conservative modelling, not a measured
allocation bound. OS disk cache and other applications are excluded from host
working-set estimates. These memory projections do not qualify the full run.

Evidence: `builds/validation/landscape-cloud/optimization-phase5/realistic117/results.json`.
The Phase 5 projection was reviewed. Phase 6 results follow; no full-resolution
run was launched.


#### Phase 6 deepID - qualification complete; ready for acceptance review

Measured publication allocation: `d880054ba`; unchanged-output manifest borrowing:
`df3d3d060`; diagnostic raw policy: `0695723e0`, `b334f6b98`.
All eight fixed/realistic pairs pass combined alpha within the EXR header bound,
accepted-camera oracle, depth cuts and beauty. Strict + IDs remains rejected.
No capacity, numerical bound, bias/count gate or beauty kernel was changed.

Original small/performance paired captures are retained; optimized IDs timings
use whole-file-identical fast reruns. The original realistic 1e-4/all output
also uses its byte-identical fast rerun for export timing. Remaining three
realistic pairs are fresh, original max1024 adaptive / GPU OIDN, 117x50.
Spill is stored capture bytes; oracle error below is IDs-on.

| Case / error / deep cap | Combined-alpha max error | Deep samples off -> on | EXR bytes off -> on | Export s off -> on | Spill bytes off -> on | IDs-on max oracle error | Renderer commit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| small / 1e-4 / 0 | 2.5795151e-05 PASS | 88,699 -> 442,344 | 609,496 -> 3,680,249 | 3.857 -> 3.846 | 9,163,356 -> 9,163,356 | 5.7053008e-06 | `df3d3d060` |
| small / 1e-3 / 0 | 0.00026875283 PASS | 53,113 -> 160,773 | 357,145 -> 1,122,051 | 3.488 -> 3.479 | 8,183,848 -> 8,183,848 | 5.4626e-05 | `df3d3d060` |
| performance / 1e-4 / 0 | 3.5507321e-05 PASS | 6,813,973 -> 19,881,881 | 59,210,585 -> 167,918,116 | 3.820 -> 8.495 | 358,081,036 -> 358,081,036 | 1.5252144e-05 | `df3d3d060` |
| performance / 1e-3 / 0 | 0.00036298053 PASS | 3,076,802 -> 7,109,968 | 22,398,061 -> 63,661,043 | 2.174 -> 3.879 | 319,349,160 -> 319,349,160 | 0.00013339434 | `df3d3d060` |
| realistic117 / 1e-4 / 0 | 2.4554377e-05 PASS | 6,622,479 -> 9,829,984 | 44,986,754 -> 70,215,403 | 148.812 -> 288.764 | 2,013,298,248 -> 2,013,298,248 | 4.0245418e-06 | `df3d3d060` |
| realistic117 / 1e-4 / 64 | 2.4180777e-05 PASS | 1,111,434 -> 3,411,125 | 8,624,581 -> 27,125,148 | 17.425 -> 26.230 | 227,635,644 -> 227,635,644 | 4.4029672e-06 | `df3d3d060` |
| realistic117 / 1e-3 / 0 | 0.00025040133 PASS | 6,498,864 -> 7,410,538 | 43,733,868 -> 52,349,769 | 114.371 -> 161.843 | 1,805,448,120 -> 1,805,448,120 | 3.883482e-05 | `df3d3d060` |
| realistic117 / 1e-3 / 64 | 0.00025059752 PASS | 972,866 -> 1,644,724 | 7,312,462 -> 13,481,587 | 15.271 -> 18.211 | 202,833,084 -> 202,833,084 | 3.9885244e-05 | `df3d3d060` |

Fitting is aggregate worker time, not export wall time:

| Case / error / cap | Density fit off -> on (s) | Mixture fit off -> on (s) | Quantize/coalesce off -> on (s) |
| --- | --- | --- | --- |
| small / 1e-4 / 0 | 0.277 -> 0.265 | 0.982 -> 2.527 | 0.118 -> 0.395 |
| small / 1e-3 / 0 | 0.187 -> 0.191 | 0.380 -> 1.057 | 0.043 -> 0.141 |
| performance / 1e-4 / 0 | 5.450 -> 6.011 | 17.744 -> 45.684 | 6.954 -> 27.614 |
| performance / 1e-3 / 0 | 2.018 -> 2.221 | 7.183 -> 18.288 | 2.362 -> 9.193 |
| realistic117 / 1e-4 / 0 | 32.474 -> 35.142 | 267.005 -> 1155.850 | 2.327 -> 7.316 |
| realistic117 / 1e-4 / 64 | 3.852 -> 4.100 | 26.276 -> 93.628 | 0.735 -> 3.429 |
| realistic117 / 1e-3 / 0 | 14.477 -> 15.845 | 103.612 -> 432.373 | 1.959 -> 4.255 |
| realistic117 / 1e-3 / 64 | 1.750 -> 1.916 | 10.152 -> 35.288 | 0.389 -> 1.322 |

Fresh replay: strict payload/deterministic-header identity **81/81**; numeric
IDs-off identity **30/30**; **9/9 CTests**; CPU/CUDA compatibility matrices at
strict, 1e-4 and 1e-3; both boundary suites and CUDA host/session lifecycle PASS.
Numeric IDs-on matrices pass on both devices. All 26 combined-alpha matrix
comparisons, two known-overlap comparisons and four exact-UINT known-alpha
selections pass. Connected Gaffer selections/point clouds pass too.

Resolved statistical flags: original realistic117 IDs-on 1e-4/all at (99,7),
and fresh IDs-off 1e-4/64 at (96,39). Every diagnostic deep-on matches ONE
diagnostic deep-off across ALL raw channels within existing 4 ULP, exact counts.
The latter reuses the same beauty/camera/sampling case with diagnostic IDs-on/all
capture; target/diagnostic deep options are recorded. This resolves beauty only;
each target's alpha/oracle/reader checks pass independently. Original calibrated
flags and all unchanged bias tests are retained. No new diagnostic renders.

Validator optimization `6f6a50d2b`: identical whole-state/count duplicates prove
a reproduced match without rechecking all peers; all controls retain their
distribution entries and bias weights. Unique states use the existing check.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| performance587x250x4; 81 actual pixels, 76 controls | Leave-one-out matching substep (s) | 1.479451 | 0.021065 | `6f6a50d2b` |
| Same 6,156 held-out decisions | Exact decision identity | reference | 6,156/6,156 | `6f6a50d2b` |

This is a 70.23x substep speedup, not a render/full-validator speedup. Only the
unfinished strict performance validator was restarted; its completed EXR stayed
unchanged. The interruption is recorded separately from numerical gate results.
All 75 common beauty kernel resource records and managed beauty-source hashes
remain unchanged; the snapshot patch stays on its isolated diagnostic branch.

Evidence: `builds/validation/landscape-cloud/optimization-phase6/acceptance/`
`phase-results.json`, `identity.json`, `ids-comparisons.json`, `queue.json`,
`post-queue.json`, `calibration-shortcut.json`. Original measured/diagnostic/
profiling reports are preserved in the sibling `measured` and `review` folders.
Gaffer: `acceptance/known-ids-CPU/known_overlap/deep/Phase6_Known_ID_Review.gfr`;
`acceptance/realistic117/ids-1e-3-cap64/native_vdb_review.gfr`.
Stop here for Phase 6 acceptance review; no 6a/6b/Phase 9 work started.

#### Phase 6 review - snapshot diagnostic and IDs export performance

Pixel `(99,7)` crosses `cloud_01_variant_0000` (932 stored volume intervals).
Four independent-seed original adaptive renders give red SD 0.0202626 and
SE of their mean 0.0101313; the 0.000268996 discrepancy is 0.02655 SE.
Accepted populations are 208/416/912/416. This is an empirical between-seed
estimate, not a per-camera variance estimate or a replacement beauty gate.

Snapshot diagnostic: separate executable with `9a017f055`, three ordinary
deep-off and three deep-on/IDs-on renders, original max1024 adaptive / GPU OIDN,
117x50, error 1e-4. Capture-only skips post-render host export; all deep GPU work
remains enabled. The patch was never committed to the deep branch; managed
beauty sources were restored exactly. These K=3 comparisons are diagnostic,
not K=5 qualification or a policy change.

| Snapshot runs | Noisy RGB at (99,7) | Population there | Image-wide existing raw gate |
| --- | --- | --- | --- |
| Deep-off 1/2/3 | (0.138675958, 0.107834488, 0.102686219), identical | 416 | Reference controls |
| Deep-on/IDs 1 | Exactly identical to all three controls | 416 | FAIL at (5,5), population 288 |
| Deep-on/IDs 2/3 | Exactly identical to all three controls | 416 | PASS / PASS |

At (99,7), all checked raw passes also fit the unchanged ULP/envelope rule.
Image-wide ON1 still fails: noisy max 5.443394e-5 > ordinary envelope
2.712011e-6; albedo max 0.002548903 > 0.000209540. Counts match; no threshold
was relaxed and no extra snapshot controls were added. The experiment supports
majorant involvement at the original pixel, not complete GPU determinism.

IDs host optimization: `df3d3d060`. Native own-process CPU sampling found 86.3%
of export sample weight in string/frame-metadata construction/destruction.
Per-pixel cloning copied 358 names x 146,750 pixels (~52.5 million name copies)
outside the old quantize timer. Borrow the immutable manifest; arithmetic,
headers, ordering, budgets and capacities remain unchanged. No new dependency.

| Performance 587x250x4 / 1e-3 | Before IDs-on | After IDs-on | Matched after IDs-off |
| --- | --- | --- | --- |
| Export wall s | 179.581 | 3.879 | 2.255 |
| Serial OpenEXR wall s | 1.599 | 1.200 | 0.468 |
| Serial spill staging wall s | 0.206 | 0.133 | 0.148 |
| Other export wall s (preparation/diagnostics/barriers) | 177.776 | 2.547 | 1.639 |
| Density fit aggregate worker s | 7.655 | 2.221 | 2.089 |
| Mixture fit aggregate worker s | 86.367 | 18.288 | 7.215 |
| Quantize/coalesce aggregate worker s | 30.962 | 9.193 | 2.387 |
| Spill bytes | 319,349,160 | 319,349,160 | 319,349,160 |
| EXR bytes | 63,661,043 | 63,661,043 | 22,398,061 |

Unprofiled export is **46.30x faster (97.84% less wall time)** and **1.72x**
IDs-off, meeting the ~3x target. Samples remain 7,109,968; EXR/spill bytes
are unchanged. Worker elapsed timers overlap and must not be added to wall time.
Profiled wall is 175.458 -> 4.328 s; approximate mean busy logical cores during
export are 16.47 -> 9.30 of 24 (69% -> 39%). Reduced CPU load removes waste.
Per-row barriers remain; scanline serialization is serial and now takes 1.200 s
(~31% of export wall). CPU sample windows are inferred from log times; sampled
waiting instruction pointers are not measurements of wait duration.

All five existing d880054ba IDs-on EXRs are **whole-file SHA256 identical**
(small/performance at both errors; realistic all-samples 1e-4). Matched IDs-off
performance EXR is identical too. Nine CTests pass, including signed-window
serial/parallel byte identity and actual scanline failure coordinates. All 75
common CUDA kernel resource records are unchanged; original beauty-source hash
is unchanged. The snapshot executable is excluded from unchanged-beauty pools.

Reports and reproducible local profiling helpers:
`builds/validation/landscape-cloud/optimization-phase6/review/review-results.json`,
`snapshot-comparison.json`, `export-performance.json`, `profile-{before,after}.csv`;
`builds/phase6_cpu_sampler.cpp`, `run_phase6_review_*.py`.

The diagnostic flag is resolved under the final Section 2 root-cause rule;
qualification is complete in the Phase 6 table above.

#### Phase 6 calibrated CUDA raw policy - historical flag before root-cause resolution

Rechecked saved realistic117 / original max1024 adaptive / GPU OIDN /
IDs-on / 1e-4 / cap0. No new renders. Exactly 31 compatible unchanged-beauty
deep-off controls each compared with the other 30, using the same whole-state
4-ULP match, exact populations and four independent-seed pixel SE as deep-on.
All leave-one-out pixels have same-count references; no zero-SE mismatches.
The majorant-snapshot executable remains excluded.

| Metric | Deep-off min | Deep-off median | Deep-off max / limit | Deep-on | Result |
| --- | --- | --- | --- | --- | --- |
| Step-2 fallback pixels | 3 | 13 | 21 | 15 | PASS |
| ViewLayer.Debug Sample Count.X | 0 | 0 | 0 | 0 | PASS |
| ViewLayer.Denoising Albedo.R | 0 | 0 | 0.139636357 | 0.152607569 | FAIL |
| ViewLayer.Denoising Albedo.G | 0 | 0 | 0.132614054 | 0.161283634 | FAIL |
| ViewLayer.Denoising Albedo.B | 0 | 0 | 0.132372598 | 0.159599209 | FAIL |
| ViewLayer.Denoising Depth.Z | 0 | 0 | 0.00122305225 | 0 | PASS |
| ViewLayer.Denoising Normal.X | 1.92252228e-08 | 7.00013995e-07 | 2.42135988e-06 | 2.18037146e-07 | PASS |
| ViewLayer.Denoising Normal.Y | 8.65560592e-08 | 7.99446967e-07 | 2.36343069e-05 | 5.79094279e-07 | PASS |
| ViewLayer.Denoising Normal.Z | 0 | 0 | 7.48448884e-05 | 0 | PASS |
| ViewLayer.Noisy Image.R | 0 | 0 | 0.0961943274 | 0.026550997 | PASS |
| ViewLayer.Noisy Image.G | 0 | 0 | 0.0371523278 | 0.0104031659 | PASS |
| ViewLayer.Noisy Image.B | 0 | 0 | 0.0087957657 | 0.00636220051 | PASS |
| ViewLayer.Noisy Image.A | 0 | 0 | 0 | 0 | PASS |

Only (99,7) is outside the calibrated channel range: denoising albedo R/G/B.
Their nearest absolute differences are 0.001097053/0.001097083/0.001097083;
four-seed SE is 0.00718872/0.00680220/0.00687399. Noisy RGB passes.
The unchanged paired-pixel bias check passes every one of 12 channels;
largest abs(mean)/SE is 1.712194, below 3. All 15 fallback pixels and their
channel details/nearest references are recorded in the machine report.

Implemented final calibrated Section 2 policy in host validators, replacing
the guessed limits. Unit checks exercise held-out ordinary bounds, equality
at the measured boundary, larger-effect rejection, zero variance/count
handling and the unchanged signed-bias test. Renderer/CPU comparison unchanged.

Reports: `builds/validation/landscape-cloud/optimization-phase6/review/calibrated-raw-policy-existing.json`;
calibration holds each of the 31 per-render counts and per-channel maxima.
Log: `builds/phase6-calibrated-raw-policy.log`.
The original albedo flag is preserved and resolved by the saved all-raw
diagnostic agreement below; no calibrated limit or bias threshold changed.

#### Phase 6 root-cause review - realistic117 1e-4 resolved

Read all six saved raw EXRs at (99,7), not only the previous RGB summary.
Three deep-off and three deep-on/IDs-on snapshot runs have 416 samples each.
Each deep-on matches off-1 across ALL 12 checked channels within the existing
4-ULP rule. RGB/A and populations are bit-identical; albedo/normal/depth are
not bit-identical, with at most 1/2/3 ULP respectively against off-1.

| Raw channel at (99,7) | Off 1 | Off 2 | Off 3 | On 1 | On 2 | On 3 | Max on vs off-1 ULP |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ViewLayer.Debug Sample Count.X | 0.40625 | 0.40625 | 0.40625 | 0.40625 | 0.40625 | 0.40625 | 0 |
| ViewLayer.Denoising Albedo.R | 0.3936423659324646 | 0.3936423659324646 | 0.3936423659324646 | 0.3936423659324646 | 0.39364239573478699 | 0.3936423659324646 | 1 |
| ViewLayer.Denoising Albedo.G | 0.36854931712150574 | 0.36854934692382812 | 0.36854934692382812 | 0.36854928731918335 | 0.36854931712150574 | 0.36854931712150574 | 1 |
| ViewLayer.Denoising Albedo.B | 0.38054010272026062 | 0.38054004311561584 | 0.38054010272026062 | 0.38054010272026062 | 0.38054004311561584 | 0.38054010272026062 | 2 |
| ViewLayer.Denoising Depth.Z | 992.44610595703125 | 992.44610595703125 | 992.446044921875 | 992.4459228515625 | 992.446044921875 | 992.446044921875 | 3 |
| ViewLayer.Denoising Normal.X | -0.0061283782124519348 | -0.0061283777467906475 | -0.0061283777467906475 | -0.0061283791437745094 | -0.0061283777467906475 | -0.0061283777467906475 | 2 |
| ViewLayer.Denoising Normal.Y | -0.019438771530985832 | -0.01943877711892128 | -0.019438762217760086 | -0.019438769668340683 | -0.019438767805695534 | -0.019438771530985832 | 2 |
| ViewLayer.Denoising Normal.Z | -0.62305563688278198 | -0.62305557727813721 | -0.62305563688278198 | -0.62305563688278198 | -0.62305563688278198 | -0.62305557727813721 | 1 |
| ViewLayer.Noisy Image.R | 0.13867595791816711 | 0.13867595791816711 | 0.13867595791816711 | 0.13867595791816711 | 0.13867595791816711 | 0.13867595791816711 | 0 |
| ViewLayer.Noisy Image.G | 0.10783448815345764 | 0.10783448815345764 | 0.10783448815345764 | 0.10783448815345764 | 0.10783448815345764 | 0.10783448815345764 | 0 |
| ViewLayer.Noisy Image.B | 0.10268621891736984 | 0.10268621891736984 | 0.10268621891736984 | 0.10268621891736984 | 0.10268621891736984 | 0.10268621891736984 | 0 |
| ViewLayer.Noisy Image.A | 1 | 1 | 1 | 1 | 1 | 1 | 0 |

The normalized debug count 0.40625 is 416/1024. Snapshot patch `9a017f055`
remains isolated; the diagnostic executable is never an ordinary reference.
No new diagnostic renders or gate thresholds were introduced. The original
calibrated albedo flag is retained but resolved by the user-approved root-cause
step; every original image-wide bias channel still passes. CPU remains exact.

Raw policy application, independent alpha qualification, remaining three
realistic pairs and fresh identity/full regression replay all PASS.
Phase 6 accepted by the user; proceed to 6a and stop for its review.
Evidence: `builds/validation/landscape-cloud/optimization-phase6/review/snapshot-all-raw-pixel99-7.json`
and `root-cause-raw-policy-existing.json`.


#### Phase 6a - accepted

Before: eight accepted Phase 6 pairs, preserved in
`builds/validation/landscape-cloud/optimization-phase6a/before.json`.
Same-object/facing hard surfaces merge after publication; numeric default
`--deep-z-tolerance 1e-4`, strict forces 0. No extra transmittance allowance:
FLOAT rounding spends remaining publication headroom. Exterior transmittance
is bounded; depths inside each merged span are a separate, recorded depth
approximation. Existing 20-byte device event and spill layout remain intact.

Same captured rays, z=0 first then default 1e-4. The second export has warm
I/O. MB means decimal bytes / 1e6. Original adaptive max-1024 settings and GPU
OIDN remain unchanged for realistic117. Renderer commit `5accee7d8`;
validators/policy through `bffe258b7`.

| Case | Error / deep cap | IDs | Deep samples, 0 -> 1e-4 | EXR MB, 0 -> 1e-4 | Export wall s, 0 -> 1e-4 | Commit |
| --- | --- | --- | --- | --- | --- | --- |
| sloped16x16 / 1024 CPU | 1e-3 / all | off | 262,143 -> 108,317 | 1.395 -> 0.815 | 0.414 -> 0.397 | `5accee7d8` |
| sloped16x16 / 1024 CPU | 1e-3 / all | on | 262,143 -> 108,317 | 1.406 -> 0.822 | 0.498 -> 0.476 | `5accee7d8` |
| sloped16x16 / 1024 CUDA | 1e-3 / all | off | 262,141 -> 108,325 | 1.315 -> 0.815 | 0.390 -> 0.378 | `5accee7d8` |
| sloped16x16 / 1024 CUDA | 1e-3 / all | on | 262,141 -> 108,325 | 1.326 -> 0.823 | 0.477 -> 0.451 | `5accee7d8` |
| small | 1e-4 / all | off | 88,699 -> 87,538 | 0.609 -> 0.614 | 5.397 -> 5.002 | `5accee7d8` |
| small | 1e-4 / all | on | 442,344 -> 441,179 | 3.680 -> 3.674 | 5.809 -> 5.629 | `5accee7d8` |
| small | 1e-3 / all | off | 53,113 -> 51,973 | 0.357 -> 0.361 | 5.132 -> 5.098 | `5accee7d8` |
| small | 1e-3 / all | on | 160,773 -> 159,629 | 1.122 -> 1.131 | 5.383 -> 4.708 | `5accee7d8` |
| performance | 1e-4 / all | off | 6,813,973 -> 6,658,162 | 59.211 -> 58.549 | 4.417 -> 4.279 | `5accee7d8` |
| performance | 1e-4 / all | on | 19,881,881 -> 19,722,474 | 167.918 -> 167.594 | 9.480 -> 9.675 | `5accee7d8` |
| performance | 1e-3 / all | off | 3,076,802 -> 2,922,312 | 22.398 -> 22.175 | 2.541 -> 2.595 | `5accee7d8` |
| performance | 1e-3 / all | on | 7,109,968 -> 6,951,905 | 63.661 -> 63.143 | 4.427 -> 4.447 | `5accee7d8` |
| realistic117 | 1e-4 / all | off | 6,622,479 -> 3,554,038 | 44.987 -> 28.561 | 178.048 -> 174.061 | `5accee7d8` |
| realistic117 | 1e-4 / all | on | 9,829,984 -> 6,614,294 | 70.215 -> 52.643 | 337.956 -> 334.381 | `5accee7d8` |
| realistic117 | 1e-4 / 64 | off | 1,111,434 -> 952,302 | 8.625 -> 7.877 | 23.240 -> 23.485 | `5accee7d8` |
| realistic117 | 1e-4 / 64 | on | 3,411,125 -> 3,247,295 | 27.125 -> 26.523 | 32.539 -> 32.158 | `5accee7d8` |
| realistic117 | 1e-3 / all | off | 6,498,864 -> 3,443,234 | 43.734 -> 27.386 | 149.958 -> 146.799 | `5accee7d8` |
| realistic117 | 1e-3 / all | on | 7,410,538 -> 4,209,432 | 52.350 -> 34.894 | 199.470 -> 201.623 | `5accee7d8` |
| realistic117 | 1e-3 / 64 | off | 972,866 -> 814,555 | 7.312 -> 6.270 | 21.261 -> 20.599 | `5accee7d8` |
| realistic117 | 1e-3 / 64 | on | 1,644,724 -> 1,481,771 | 13.482 -> 12.828 | 23.382 -> 22.680 | `5accee7d8` |

All eight IDs-off/on combined-alpha comparisons PASS with exactly zero
full-flatten difference. Every same-capture z comparison also has exactly zero
flatten difference; maximum exterior transmittance error is 2.66807475e-08,
within the unchanged EXR header bounds. Independent accepted-camera/physical
volume oracle and depth-cut checks PASS; validators read error from headers.

Surface/hard-step records on realistic117, IDs on, z=0 -> 1e-4:
Zero-width hard steps are included; each merged surface group counts once.

- 1e-4, deep cap all: 5,859,539 -> 2,643,849 (54.9% fewer).
- 1e-4, deep cap 64: 843,215 -> 679,385 (19.4% fewer).
- 1e-3, deep cap all: 5,837,712 -> 2,636,606 (54.8% fewer).
- 1e-3, deep cap 64: 840,871 -> 677,918 (19.4% fewer).

Sloped16/1024 CPU IDs-on surface records: 262,143 -> 108,317 (58.7% fewer);
EXR 1,406,093 -> 822,286 bytes (41.5% smaller). At realistic117/1e-3/all with
IDs on, total records fall 43.2% and EXR bytes fall 33.3%. Export speed is
largely unchanged: merging follows curve fitting, and some cases are slightly
slower or produce slightly larger compressed files. No capture speedup claimed.

Fitting times are summed worker times, not export wall time. Sloped CPU IDs-on:
density 0 -> 0 s; mixture 1.066 -> 1.073 s; quantize/coalesce 0.069 -> 0.073 s.
Realistic117/1e-3/all IDs-on: density 17.191 -> 17.063 s; mixture
428.474 -> 427.349 s; quantize/coalesce 4.392 -> 4.648 s. Full per-case timings,
old Phase 6 baseline, paired measurements and actual validation reports:
`builds/validation/landscape-cloud/optimization-phase6a/phase-results.json`.

Fresh replay PASS: strict 81/81; numeric z=0 30/30; nine CTests; six
CPU/CUDA strict/numeric compatibility matrices; four IDs matrices; CPU/CUDA
boundary suites; CUDA host lifecycle; 26 IDs alpha comparisons, two known
overlaps and four exact-UINT known-alpha object selections. Host-only beauty
proof PASS: unchanged beauty-source hash, all 75 common CUDA kernel resource
records unchanged, and CPU exact equality. No new CUDA calibration controls.

Saved five flagged pixels re-evaluated without new renders:

| Pixel / channel | Absolute difference | Rule-1 absolute limit | Result |
| --- | --- | --- | --- |
| (3,8), (3,5), (5,0), (11,0), normal X (each) | 5.96046448e-8 | 2.38418579e-7 (reference 4 ULP) | PASS |
| (0,0), albedo R | 7.74860382e-7 | 4.17232513e-7 | Outside measured range; informational |
| (0,0), albedo G | 7.15255737e-7 | 4.10396083e-7 | Outside measured range; informational |

Five ordinary controls are below the required 20. Step 2 is informational;
6a qualifies by the user-approved host-only proof, not CUDA pixel thresholds.
Section 2 records the final ULP floor, calibration minimum and phase scope.
Saved original flags remain in `five-pixel-re-evaluation.json`.

Large evidence/TEMP: `D:/CyclesDeepScratch/optimization-phase6a/`.
Connected Gaffer before/default landscape:
`D:/CyclesDeepScratch/optimization-phase6a/evidence-final/realistic117/CUDA/ids-1e-3-cap64-z1e-4/phase6a_landscape_review.gfr`.
Sloped full-surface comparison:
`D:/CyclesDeepScratch/optimization-phase6a/evidence-final/sloped/CPU/ids-1e-3-cap0-z1e-4/surface_merge_review.gfr`.
Phase 6a accepted by the user. Full-resolution Phase 9 remains unlaunched.

#### Phase 6a follow-up - controlled export check

Realistic117, original 1024 adaptive, GPU OIDN, error 1e-3/all, z=0.
Phase 6 executable then current, sequentially, 24 threads, TEMP on D:,
Blender native colour config (OCIO unset), no concurrent render/compile jobs.
Raw spill is process-owned temporary storage, so cross-executable replay was
unavailable. Sampled raw-camera CSVs are SHA256-identical and both deep outputs
are bit-identical to the accepted Phase 6 dataset, including deterministic headers.

| Case / metric | Phase 6 | Current | Commit |
| --- | --- | --- | --- |
| IDs off / export wall s | 109.800 | 108.745 | `5accee7d8` |
| IDs on / export wall s | 154.472 | 138.080 | `5accee7d8` |
| IDs off / aggregate mixture-fit s | 100.554 | 104.021 | `5accee7d8` |
| IDs on / aggregate mixture-fit s | 424.957 | 362.439 | `5accee7d8` |

No current-build slowdown reproduced; no renderer fix. Read/staging/serialization
stay close (full stage counters in report). Historical paired/single-export
measurements do not isolate a build regression. Earlier diagnostic runs inherited
Gaffer OCIO and changed adaptive convergence; preserved but excluded from this
native-case conclusion. No threshold was introduced or changed.
Report: `builds/validation/landscape-cloud/optimization-phase6a/export-recheck-native/results.json`.
Large data: `D:/CyclesDeepScratch/optimization-phase6a/export-recheck-native/`.

#### Phase 6b - accepted

Implementation: `24da5f880`; native opacity/selection checks: `4ddb28e64`.
Host-only: existing camera capture already records holdout opacity. Preflight
now admits object flags and reachable Holdout surface closures. No beauty,
GPU capture, scheduling or buffer changes; the CUDA pixel gate is not required.
Beauty/GPU source hashes and all 75 kernel resource records are unchanged;
all 30 new CPU fixtures have exact beauty and raw-pass equality to deep-off.

IDs-on `cycles:deepIDHoldoutManifest` is a checked hash/name subset of the
unchanged ID manifest, omitted when empty. Exact UINT selection preserves only
the selected marker. Shadow-catcher/caustics retain their legacy diagnostic;
strict+IDs stays rejected. All six rejection tests preserve the final EXR.
Before: six holdout cases rejected; accepted 6a fixed-case metrics recorded in
`builds/validation/landscape-cloud/optimization-phase6b/before.json`.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| Object/material holdout, front/inside volume; two transparent cutouts | Native CPU/CUDA qualification | Six cases rejected | 60/60 PASS, strict and numeric with/without IDs | `24da5f880` |
| Holdout fixtures, strict | Max all-depth error vs ordinary opacity / header bound | Ordinary reference | 0 / 1e-06 PASS | `24da5f880` |
| Holdout fixtures, 1e-4 | Max all-depth error vs ordinary opacity / header bound | Ordinary reference | 2.59013751e-05 / 9.99999975e-05 PASS | `24da5f880` |
| Holdout fixtures, 1e-3 | Max all-depth error vs ordinary opacity / header bound | Ordinary reference | 7.13876313e-05 / 0.00100000005 PASS | `24da5f880` |
| IDs-on holdout selection | Max isolated opacity error | Known opacity 1.0 / 0.6 | 5.0127592e-08 PASS; marked plane/fog subset checked | `4ddb28e64` |
| CPU beauty/raw passes | Deep-on vs deep-off | Exact equality required | 30/30 exact | `24da5f880` |
| Existing deep output | Strict / numeric payload and deterministic headers | 81 / 30 references | 81/81 + 30/30 identical | `24da5f880` |
| Full regression | CTests, legacy/IDs matrices, boundaries, CUDA lifecycle, fixed landscapes | Accepted 6a suite | All PASS; 32 combined-alpha/known-ID comparisons | `24da5f880` |

Fresh fixed-case capture/readback/lane-write/spill/export/EXR/sample/memory
counters and full holdout records are in
`builds/validation/landscape-cloud/optimization-phase6b/phase-results.json`.
Holdout errors compare every boundary and exponential extremum; the existing
camera oracle and Gaffer depth cuts also pass. Bounds/limits remain unchanged.
Connected Gaffer review: `D:/CyclesDeepScratch/optimization-phase6b/holdout_review.gfr`.
Large evidence/TEMP stay on D:. Phase 6b accepted by the user.

#### Phase 7 - qualification passed; stop for review

Implementation: `a7357e66f`, `1bf677565`, `035d84b4e`. Renderer remains `24da5f880`.
The default command uses OpenEXR/NumPy, one CPU beauty control per fixture,
shared numerical checks and the original fixture limits. All data error gates
remain unchanged. SDK/Gaffer policy reports match on the existing 31-control
realistic117 dataset; only the output trace path differs. No new beauty renders.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| 47x20/max16 strict | Render+capture / wait+copy s | 2.899 / 1.788 | 2.856 / 1.755 | `035d84b4e` |
| 47x20/max16 strict | Lane-written / copied MiB | 25.09 / 344.03 | 25.09 / 344.03 | `035d84b4e` |
| 47x20/max16 strict | Spill stored/read/write MiB | 12.75 / 12.75 / 13.09 | 12.75 / 12.75 / 13.09 | `035d84b4e` |
| 47x20/max16 strict | Export / density-fit / mixture-fit / coalesce s | 6.799 / 2.112 / 17.406 / 1.822 | 8.121 / 2.687 / 22.318 / 2.298 | `035d84b4e` |
| 47x20/max16 strict | EXR bytes / deep samples | 4,445,714 / 561,793 | 4,445,714 / 561,793 | `035d84b4e` |
| 47x20/max16 strict | Peak host / device MiB | 5326.9 / 5310 | 5312.2 / 5591 | `035d84b4e` |
| 587x250/4 strict | Render+capture / wait+copy s | 37.156 / 33.717 | 34.423 / 31.427 | `035d84b4e` |
| 587x250/4 strict | Lane-written / copied MiB | 845.53 / 6833.91 | 845.53 / 6833.91 | `035d84b4e` |
| 587x250/4 strict | Spill stored/read/write MiB | 498.26 / 572.12 / 549.72 | 498.26 / 572.12 / 549.72 | `035d84b4e` |
| 587x250/4 strict | Export / density-fit / mixture-fit / coalesce s | 35.580 / 86.397 / 265.314 / 149.423 | 33.530 / 81.835 / 248.957 / 140.131 | `035d84b4e` |
| 587x250/4 strict | EXR bytes / deep samples | 394,273,944 / 52,189,081 | 394,273,944 / 52,189,081 | `035d84b4e` |
| 587x250/4 strict | Peak host / device MiB | 5356.4 / 5790 | 5552.7 / 5636 | `035d84b4e` |
| 47x20/max16 1e-4 | Render+capture / wait+copy s | 1.831 / 0.724 | 1.826 / 0.714 | `035d84b4e` |
| 47x20/max16 1e-4 | Lane-written / copied MiB | 8.05 / 8.97 | 8.05 / 8.97 | `035d84b4e` |
| 47x20/max16 1e-4 | Spill stored/read/write MiB | 8.74 / 8.74 / 9.08 | 8.74 / 8.74 / 9.08 | `035d84b4e` |
| 47x20/max16 1e-4 | Export / density-fit / mixture-fit / coalesce s | 3.700 / 0.275 / 0.998 / 0.124 | 3.654 / 0.272 / 0.993 / 0.119 | `035d84b4e` |
| 47x20/max16 1e-4 | EXR bytes / deep samples | 609,496 / 88,699 | 609,496 / 88,699 | `035d84b4e` |
| 47x20/max16 1e-4 | Peak host / device MiB | 5507.3 / 5655 | 5295.8 / 5572 | `035d84b4e` |
| 587x250/4 1e-4 | Render+capture / wait+copy s | 11.791 / 9.126 | 11.811 / 9.200 | `035d84b4e` |
| 587x250/4 1e-4 | Lane-written / copied MiB | 314.62 / 350.45 | 314.62 / 350.45 | `035d84b4e` |
| 587x250/4 1e-4 | Spill stored/read/write MiB | 341.49 / 410.86 / 392.36 | 341.49 / 410.86 / 392.36 | `035d84b4e` |
| 587x250/4 1e-4 | Export / density-fit / mixture-fit / coalesce s | 3.762 / 5.561 / 17.815 / 6.917 | 3.793 / 5.576 / 17.826 / 6.938 | `035d84b4e` |
| 587x250/4 1e-4 | EXR bytes / deep samples | 59,210,585 / 6,813,973 | 59,210,585 / 6,813,973 | `035d84b4e` |
| 587x250/4 1e-4 | Peak host / device MiB | 5322.8 / 5728 | 5370.8 / 5648 | `035d84b4e` |
| 47x20/max16 1e-3 | Render+capture / wait+copy s | 1.976 / 0.767 | 1.815 / 0.705 | `035d84b4e` |
| 47x20/max16 1e-3 | Lane-written / copied MiB | 7.12 / 8.03 | 7.12 / 8.03 | `035d84b4e` |
| 47x20/max16 1e-3 | Spill stored/read/write MiB | 7.80 / 7.80 / 8.15 | 7.80 / 7.80 / 8.15 | `035d84b4e` |
| 47x20/max16 1e-3 | Export / density-fit / mixture-fit / coalesce s | 3.555 / 0.194 / 0.393 / 0.043 | 3.524 / 0.189 / 0.395 / 0.044 | `035d84b4e` |
| 47x20/max16 1e-3 | EXR bytes / deep samples | 357,145 / 53,113 | 357,145 / 53,113 | `035d84b4e` |
| 47x20/max16 1e-3 | Peak host / device MiB | 5308.1 / 5621 | 5381.9 / 5572 | `035d84b4e` |
| 587x250/4 1e-3 | Render+capture / wait+copy s | 12.790 / 10.088 | 11.779 / 9.236 | `035d84b4e` |
| 587x250/4 1e-3 | Lane-written / copied MiB | 277.68 / 313.51 | 277.68 / 313.51 | `035d84b4e` |
| 587x250/4 1e-3 | Spill stored/read/write MiB | 304.56 / 373.96 / 355.48 | 304.56 / 373.96 / 355.48 | `035d84b4e` |
| 587x250/4 1e-3 | Export / density-fit / mixture-fit / coalesce s | 2.248 / 2.071 / 7.356 / 2.390 | 2.176 / 2.026 / 7.205 / 2.339 | `035d84b4e` |
| 587x250/4 1e-3 | EXR bytes / deep samples | 22,398,061 / 3,076,802 | 22,398,061 / 3,076,802 | `035d84b4e` |
| 587x250/4 1e-3 | Peak host / device MiB | 5376.0 / 5762 | 5390.6 / 5647 | `035d84b4e` |
| Default regression | Wall time (including cleanup) | 996.57 s | 587.66 s | `035d84b4e` |
| All regressions | Strict / numeric / CTests | 81 / 30 / 9 PASS | 81 / 30 / 9 PASS; exact CPU beauty; 75 unchanged records | `035d84b4e` |
| Historical sample CSVs | Bytes stored / files | 72,375,108,634 / 1021 raw | 23,873,719,812 / 1021 verified ZIP | `035d84b4e` |

Fit counters are aggregate worker seconds; export is wall time. Single render
measurements, not renderer speedup claims (renderer unchanged). The baseline
uses the legacy orchestration plus nine CTests; the new command also includes
surface/lens/motion and holdout smoke fixtures. Exact outputs and thresholds hold.

All 1021 listed CSVs are historical diagnostics; none requires a raw
working copy. Identity/beauty checks retain their EXRs, and camera-oracle replays
stream the archives. Allocated disk saved: 48,501,388,822 bytes.
Each archive has a decompressed SHA256/size verification record before replacement.

Results and inventory: `builds/validation/landscape-cloud/optimization-phase7/`.
Passing run intermediates removed from its owned D: directory; `--keep` preserves
them. Failed harness-development evidence stays on D:. Thirteen archived validation reports
listed in [the archive index](src/deep/ARCHIVED_REPORTS.md); the approved
[M8 record](src/deep/M8_RELEASE_VALIDATION.md) is retained. README lists all 59 current core files plus Blender
overlay plumbing. Connected review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Phase 7 accepted. Phase 8a stopped at the OptiX small-landscape beauty bias gate.
Full-resolution Phase 9 remains unlaunched.


### Phase 8a results (accepted)

OptiX SVM uses its own deep module/pipeline/SBT and per-queue launch state;
shared capture/reconstruction, native double grid integration and beauty
sources remain unchanged. Native shader-raytrace modules link with the pinned
clang-cl/NVCC toolchain. AO/Bevel are qualified outside opacity dependencies;
ray-traced opacity fails explicit preflight. No fallback restriction is needed.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| All compiler audit cases | Own oracle/depth cuts; cross flattened alpha | Retained cl.exe/NVCC references | 111/111 PASS; 104 byte-different; max flat 4.75347219e-07 < 1e-4 | `5640d9db5` |
| CUDA 33x17/4 raw capture | Before fitting | 2,244 rays / 70,201 events | Counts equal; 338 rays differ; event max 1 ULP; cubic coefficients identical | `5640d9db5` |
| Same-build/backend identity | Payload + deterministic headers | New candidate references | CPU/CUDA 81/81 strict + 30/30 numeric; OptiX 41/41 strict + 24/24 numeric | `5640d9db5` |
| CUDA/OptiX beauty | Matched per-backend controls | 20 ordinary + four seeds per fixture | 64/64 targets PASS; CPU exact; unchanged sources and 75 resources | `5640d9db5` |
| OptiX small strict noisy green | Signed bias / reference-mean FLOAT ULP | Bare 3-SE flag | PASS: 1.06980727e-09 / 1.49011612e-08; reference mean 0.163578587 | `5640d9db5` |
| Full regression / default replay | Wall time | Nine CTests + matrices/boundaries/CPU beauty/identity | PASS; full cumulative 2919.6 s; default 660.7 s | `5640d9db5` |
| OptiX/CUDA pairs | Cross flattened alpha / own bounds | Unified limit 1e-4 | 65 PASS; max flat 1.39451287e-08; own oracles/depth cuts PASS | `5640d9db5` |
| Opaque foreground strict | Own oracle / cross flat | CUDA oracle 1.49011612e-8 | OptiX same oracle; flat 0; <=3-ULP depths informational | `5640d9db5` |
| volume33 strict | Render+capture / wait+copy / export s | CUDA 0.5155 / 0.4683 / 0.4315 | OptiX 0.4255 / 0.3520 / 0.4306 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 0.1523 / 0.3158 / 0.2065 | OptiX 0.1530 / 0.3133 / 0.2047 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 7,551,024 / 115,818,976 / 3,758,164 | OptiX 7,551,024 / 115,818,976 / 3,758,164 | `5640d9db5` |
| volume33 1e-4 | Render+capture / wait+copy / export s | CUDA 0.3254 / 0.2765 / 0.2145 | OptiX 0.2306 / 0.1550 / 0.2160 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 0.0181 / 0.0380 / 0.0129 | OptiX 0.0182 / 0.0375 / 0.0129 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 2,332,408 / 2,415,032 / 2,440,120 | OptiX 2,332,408 / 2,415,032 / 2,440,120 | `5640d9db5` |
| volume33 1e-3 | Render+capture / wait+copy / export s | CUDA 0.3291 / 0.2803 / 0.0869 | OptiX 0.2221 / 0.1501 / 0.0871 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 0.0048 / 0.0131 / 0.0024 | OptiX 0.0048 / 0.0133 / 0.0024 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 953,108 / 1,035,732 / 1,060,820 | OptiX 953,108 / 1,035,732 / 1,060,820 | `5640d9db5` |
| small strict | Render+capture / wait+copy / export s | CUDA 2.8568 / 1.7533 / 7.9137 | OptiX 4.6664 / 3.6808 / 7.9922 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 2.3169 / 22.5614 / 2.4189 | OptiX 2.3237 / 22.5969 / 2.3434 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 26,307,972 / 360,742,784 / 13,367,672 | OptiX 26,308,472 / 360,742,784 / 13,367,732 | `5640d9db5` |
| small 1e-4 | Render+capture / wait+copy / export s | CUDA 1.7829 / 0.6951 / 3.5972 | OptiX 1.6921 / 0.7275 / 3.6707 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 0.2526 / 0.9845 / 0.1159 | OptiX 0.2517 / 0.9684 / 0.1125 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 8,441,980 / 9,404,540 / 9,163,900 | OptiX 8,442,060 / 9,404,620 / 9,163,980 | `5640d9db5` |
| small 1e-3 | Render+capture / wait+copy / export s | CUDA 1.7585 / 0.6816 / 3.4207 | OptiX 1.6929 / 0.7373 / 3.5299 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 0.1802 / 0.3823 / 0.0417 | OptiX 0.1813 / 0.3826 / 0.0417 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 7,462,316 / 8,424,876 / 8,184,236 | OptiX 7,462,396 / 8,424,956 / 8,184,316 | `5640d9db5` |
| performance strict | Render+capture / wait+copy / export s | CUDA 33.7385 / 30.8840 / 32.6980 | OptiX 54.1163 / 51.4686 / 32.7222 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 70.0270 / 246.3750 / 140.9640 | OptiX 69.8806 / 245.2360 / 140.9930 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 886,601,284 / 7,165,515,584 / 522,461,344 | OptiX 886,600,804 / 7,164,432,320 / 522,461,184 | `5640d9db5` |
| performance 1e-4 | Render+capture / wait+copy / export s | CUDA 11.4912 / 8.9838 / 3.6599 | OptiX 10.8676 / 8.5926 / 3.6468 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 4.6722 / 17.2369 / 6.7716 | OptiX 4.6806 / 17.2769 / 6.7814 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 329,903,372 / 367,471,372 / 358,079,372 | OptiX 329,903,788 / 367,471,788 / 358,079,788 | `5640d9db5` |
| performance 1e-3 | Render+capture / wait+copy / export s | CUDA 11.5487 / 9.0912 / 2.2239 | OptiX 10.1119 / 7.8752 / 2.1389 | `5640d9db5` |
| Same case | Aggregate worker s: density / mixture / publication | CUDA 1.6966 / 6.9553 / 2.2453 | OptiX 1.7002 / 6.9828 / 2.2437 | `5640d9db5` |
| Same case | Lane bytes written / GPU copied / spill bytes | CUDA 291,168,564 / 328,736,564 / 319,344,564 | OptiX 291,168,560 / 328,736,560 / 319,344,560 | `5640d9db5` |

All 64 saved CUDA/OptiX beauty cases were re-evaluated with the reference-mean
FLOAT ULP floor. Only OptiX small strict noisy green exceeds 3 SE; its bias
is 0.072 reference-mean ULP and passes. The original opaque-foreground pixel
flag and its six-render snapshot root-cause resolution remain recorded.

Times are single sequential runs on RTX 3080 / Ryzen 5900X; setup excluded.
Capture wait+copy includes GPU kernel wait and transfer, not kernel-only time.
Beauty-only medians (20 controls), deep samples/EXR sizes, peak host/device memory,
all fitting stages and each beauty fallback pixel/channel are in the report.
Cross-build/backend curves, depth shifts and counts are informational under
Section 2. Same-build/backend byte checks include all deterministic headers.

New clang-cl 20.1.8 / NVCC 12.8.61 references are promoted separately on D:;
old references remain unchanged. [Promotion](builds/validation/landscape-cloud/optimization-phase8a/promoted-baseline.json).
[Results](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json),
[compiler audit](builds/validation/landscape-cloud/optimization-phase8a/toolchain-audit.json),
[raw diagnostic](builds/validation/landscape-cloud/optimization-phase8a/raw-capture-toolchain.json).
Connected review: `D:/CyclesDeepScratch/regression-tools/phase8a_review.gfr`.
Phases 8a and 8b accepted by the user on 2026-10-09. Phase 8c results follow; Phase 9 remains unlaunched.

### Phase 8b results (accepted)

[Before measurements](builds/validation/landscape-cloud/optimization-phase8b/baseline.json):
18 fixed CUDA/OptiX cases, accepted 8a executable, measured before implementation.
Renderer `21149fed7`, OSL 1.15.3.0: optimized ShadingSystem::getattribute group
queries plus actual loaded OSO operation metadata. Constant, texture, noise,
camera-ray queries and mixed Script/native-node materials pass on CPU/OptiX.
Static allocation proof preserves the native 1024-byte arena/16-entry stack;
trace/@ao/@bevel, unsafe ray/attribute/userdata queries, closure loops/capacity,
coloured or invalid extinction fail explicitly. The deep program reuses native
OSL callables; the guarded deep program-group gap was moved before callables.
Beauty shader sources and call paths remain unchanged.

| Case | Metric | Before | After | Commit |
| --- | --- | --- | --- | --- |
| OSL five fixtures / two devices / three modes | Own oracle/native alpha, beauty, rerun identity | GPU OSL unsupported | 30/30 PASS; max native-alpha error 9.75338e-8 | `21149fed7` |
| OSL unsafe fixtures | Explicit rejection and previous EXR preserved | Restricted node allowlist | 26/26 PASS | `21149fed7` |
| Texture 33x17x4 CPU vs OptiX | Maximum flattened-alpha difference | Pristine beauty 0.000901579857 | Deep 0.000901608817; native waiver PASS; own errors CPU 9.75338e-8 / OptiX 9.43985e-8 | `21149fed7` |
| Other OSL CPU vs OptiX cases | Maximum flattened-alpha difference | GPU OSL unsupported | 2.78461e-7; PASS 1e-4 | `21149fed7` |

[Full OSL checks](builds/validation/landscape-cloud/optimization-phase8b/osl-surfaces-qualified.json),
[summary](builds/validation/landscape-cloud/optimization-phase8b/osl-summary.json),
[pristine evidence](builds/validation/landscape-cloud/optimization-phase8b/pristine-waiver.json).
Pristine revision 749518deb reproduces texture alpha difference at (14,9).
CPU exact beauty and all 15 calibrated OptiX beauty targets pass. All 30
same-build OSL payload/header reruns are identical. GPU OSL requires OptiX;
OSL volumes remain 8c. Native nodes and Script nodes share Cycles' scene-wide OSL.
Connected review: `D:/CyclesDeepScratch/regression-tools/phase8b_review.gfr`:
10 actual deep inputs, paired beauty, orthographic camera, depth cut and point
cloud; 1683 points per full image, 561 after a cut at 2.5.

Final SVM identity: 81/81 strict + 30/30 numeric; OptiX 65/65. Nine CTests
pass (11.08 s); CPU beauty exact, beauty-source hash and 75 kernel resource
records unchanged. Full SVM GPU beauty: 64/64 PASS, plus 15/15 new OSL OptiX
beauty targets. Matrices, boundaries, all header oracles/depth cuts, adaptive,
lens/motion, IDs, holdout and AO/Bevel checks pass.

The OptiX performance/1e-4 beauty count initially had five fallback pixels
against the control maximum four; channel limits and bias all passed. Existing
Section 2 snapshot resolution passes every checked raw channel at all five:
(274,140), (560,198), (533,66) are six-run bit-identical; (515,216) differs at
most 1.49012e-8 and (159,68) at most 5.96046e-8, with each deep-on matching
one deep-off within 4 ULP. All six runs have four accepted samples per pixel.
The diagnostic executable remains isolated; no beauty patch entered this branch.
Only the eight unfinished beauty checks resumed; completed renders/oracles
were retained. [Resolution](builds/validation/landscape-cloud/optimization-phase8b/majorant-resolutions.json).

[Final results](builds/validation/landscape-cloud/optimization-phase8b/phase-results.json),
[regression](builds/validation/deep-regression/20261008T155657Z-2f92fb70/results.json).
Expanded regression work took 2089.95 s (34.83 min), including the resumed
beauty tail; six diagnostic renders add 173.49 s separately.
This includes OptiX and full GPU beauty, beyond the default regression set.
All 18 fixed-case sample counts, EXR bytes, lane-written/copied/spill bytes
are unchanged. Single sequential timings follow; density/mixture values are
aggregate worker seconds, not wall time. Timing noise is not an optimization
claim. After peaks: host working set <=5.38 GiB; device-wide GPU <=6750 MiB
(includes desktop/other apps). Exact counters and measured peaks are in JSON.

| Case / backend / error | Capture s, before -> after | Export s, before -> after | Density / mixture fit worker s, before -> after | Deep samples | EXR MB | Commit |
| --- | --- | --- | --- | --- | --- | --- |
| volume33 / CUDA / strict | 0.556 -> 0.516 | 0.435 -> 0.434 | 0.156/0.315 -> 0.155/0.315 | 114,304 -> 114,304 | 0.789 -> 0.789 | `21149fed7` |
| volume33 / CUDA / 1e-4 | 0.332 -> 0.327 | 0.216 -> 0.217 | 0.018/0.038 -> 0.019/0.038 | 10,329 -> 10,329 | 0.088 -> 0.088 | `21149fed7` |
| volume33 / CUDA / 1e-3 | 0.345 -> 0.326 | 0.087 -> 0.085 | 0.005/0.014 -> 0.005/0.013 | 3,452 -> 3,452 | 0.033 -> 0.033 | `21149fed7` |
| small / CUDA / strict | 3.014 -> 3.039 | 8.081 -> 7.854 | 2.344/22.909 -> 2.287/21.448 | 561,860 -> 561,860 | 4.447 -> 4.447 | `21149fed7` |
| small / CUDA / 1e-4 | 1.767 -> 1.967 | 3.757 -> 3.727 | 0.254/1.003 -> 0.260/0.974 | 89,072 -> 89,072 | 0.613 -> 0.613 | `21149fed7` |
| small / CUDA / 1e-3 | 1.759 -> 1.797 | 3.421 -> 3.553 | 0.179/0.383 -> 0.182/0.385 | 53,481 -> 53,481 | 0.361 -> 0.361 | `21149fed7` |
| performance / CUDA / strict | 34.316 -> 34.289 | 33.230 -> 33.054 | 70.670/248.539 -> 71.058/247.479 | 52,191,416 -> 52,191,416 | 394.322 -> 394.322 | `21149fed7` |
| performance / CUDA / 1e-4 | 11.607 -> 11.558 | 3.790 -> 3.717 | 4.701/17.444 -> 4.689/17.156 | 6,828,290 -> 6,828,290 | 59.346 -> 59.346 | `21149fed7` |
| performance / CUDA / 1e-3 | 11.590 -> 11.622 | 2.127 -> 2.094 | 1.695/6.952 -> 1.705/6.952 | 3,090,807 -> 3,090,807 | 22.574 -> 22.574 | `21149fed7` |
| volume33 / OPTIX / strict | 0.439 -> 0.417 | 0.435 -> 0.436 | 0.155/0.316 -> 0.156/0.318 | 114,304 -> 114,304 | 0.789 -> 0.789 | `21149fed7` |
| volume33 / OPTIX / 1e-4 | 0.248 -> 0.228 | 0.214 -> 0.214 | 0.018/0.037 -> 0.018/0.038 | 10,329 -> 10,329 | 0.088 -> 0.088 | `21149fed7` |
| volume33 / OPTIX / 1e-3 | 0.239 -> 0.215 | 0.090 -> 0.088 | 0.005/0.013 -> 0.005/0.013 | 3,452 -> 3,452 | 0.033 -> 0.033 | `21149fed7` |
| small / OPTIX / strict | 4.858 -> 4.876 | 8.039 -> 7.965 | 2.347/22.777 -> 2.289/21.518 | 561,628 -> 561,628 | 4.443 -> 4.443 | `21149fed7` |
| small / OPTIX / 1e-4 | 1.691 -> 1.765 | 3.616 -> 3.730 | 0.252/0.976 -> 0.253/0.953 | 88,934 -> 88,934 | 0.611 -> 0.611 | `21149fed7` |
| small / OPTIX / 1e-3 | 1.780 -> 1.742 | 3.546 -> 3.460 | 0.183/0.376 -> 0.181/0.385 | 53,356 -> 53,356 | 0.358 -> 0.358 | `21149fed7` |
| performance / OPTIX / strict | 54.858 -> 55.017 | 32.815 -> 33.021 | 70.401/246.392 -> 70.780/246.809 | 52,183,415 -> 52,183,415 | 394.246 -> 394.246 | `21149fed7` |
| performance / OPTIX / 1e-4 | 10.889 -> 10.961 | 3.684 -> 3.838 | 4.716/17.358 -> 4.764/17.374 | 6,823,509 -> 6,823,509 | 59.304 -> 59.304 | `21149fed7` |
| performance / OPTIX / 1e-3 | 10.093 -> 10.136 | 2.085 -> 2.103 | 1.707/6.970 -> 1.703/6.921 | 3,086,077 -> 3,086,077 | 22.521 -> 22.521 | `21149fed7` |

Phase 8b accepted. Phase 8c results follow; Phase 9 remains unlaunched.

### Phase 8c - accepted (2026-10-09)

Renderer `8137045eb` (Blender SHA `677aab08cfb6ffb02298f7d67f1e61a24801fd1b195436524a935871b85358a8`). Initialized deep shader storage/globals on CPU and GPU; deterministic native grid filters and native mip-selection expectation in the separate deep OptiX services module. Beauty remains unchanged. Adaptive midpoint step doubling reuses evaluations and fails explicitly on capacity/refinement/evaluation limits.

Header method is `shader-eval-adaptive`; material step range means starting/maximum world-unit steps. E/2 is the **stated, not proven** stepping allowance; E/2 bounds representation/fitting/publication. Features narrower than the finest evaluated step can be missed. Shader error bounds and the 8192-event cap are unchanged; the user-approved beauty-floor update is recorded below.

Independent CPU h/64 integration samples the actual shader on selected accepted rays without event records/cap. Deterministic fixed midpoint converges at order 2; published curves plateau at their separately bounded fitting error.

| Initial step | Raw curve vs h/64 | EXR curve vs h/64 | Observed order | Commit |
| --- | ---: | ---: | ---: | --- |
| 0.005 | 4.75544e-06 | 1.17926e-05 | 1.99996 | `3347aaca4` |
| 0.0025 | 1.18889e-06 | 1.25428e-05 | 1.99988 | `3347aaca4` |
| 0.00125 | 2.97246e-07 | 1.22462e-05 | 1.99971 | `3347aaca4` |
| 0.000625 | 7.43266e-08 | 1.22358e-05 | 1.9961 | `3347aaca4` |
| 0.0003125 | 1.8632e-08 | 1.22503e-05 | - | `3347aaca4` |

[Convergence](builds/validation/landscape-cloud/optimization-phase8c/deterministic-convergence.json). Same-step CPU/OptiX payloads and deterministic headers repeat exactly.

Shader fixtures: 17x9x4. **Before = diagnostic fixed midpoint; after = adaptive, same initial/max step.** Times exclude startup/export; CPU uses render+capture because capture is not separately timed. GPU capture/readback is separately recorded in JSON. Evaluation counts include GPU count and write passes. Fitting columns are aggregate worker seconds, not wall time.

| Case / error | Point evaluations before -> after | Render+capture s before -> after | Analytic render+capture s | Export s before -> after | Density/mixture fitting s before -> after | 4x-finer / CPU h64 error | Commit |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| CPU/constant/1e-4 | 246,500 -> 739,500 | 0.015344 -> 0.026447 | 0.007576 | 0.341948 -> 0.694736 | 0/0.087601 -> 0/0.159336 | 0 / 8.25415e-08 | `8137045eb` |
| CPU/constant/1e-3 | 246,500 -> 739,500 | 0.018433 -> 0.022118 | 0.008929 | 0.354866 -> 0.693976 | 0/0.0855853 -> 0/0.164354 | 0 / 8.25415e-08 | `8137045eb` |
| OPTIX/constant/1e-4 | 493,000 -> 1,479,000 | 0.122011 -> 0.212357 | 0.055611 | 0.353712 -> 0.891614 | 0/0.0888965 -> 0/0.212294 | 0 / 8.25415e-08 | `8137045eb` |
| OPTIX/constant/1e-3 | 493,000 -> 1,479,000 | 0.124425 -> 0.205268 | 0.056693 | 0.353291 -> 0.72072 | 0/0.0859678 -> 0/0.179036 | 0 / 8.25415e-08 | `8137045eb` |
| CPU/texture/1e-4 | 277,294 -> 835,402 | 0.022247 -> 0.042477 | 0.00992 | 0.426315 -> 0.794392 | 0/0.117889 -> 0/0.182677 | 1.3268e-05 / 1.30236e-05 | `8137045eb` |
| CPU/texture/1e-3 | 277,294 -> 834,706 | 0.023115 -> 0.034245 | 0.010432 | 0.387738 -> 0.782 | 0/0.0939699 -> 0/0.17999 | 6.25286e-05 / 6.16785e-05 | `8137045eb` |
| OPTIX/texture/1e-4 | 554,588 -> 1,670,804 | 0.169949 -> 0.398914 | 0.068472 | 0.396374 -> 1.08147 | 0/0.115169 -> 0/0.216528 | 1.32672e-05 / 1.30257e-05 | `8137045eb` |
| OPTIX/texture/1e-3 | 554,588 -> 1,669,412 | 0.165544 -> 0.402868 | 0.064561 | 0.390139 -> 1.01416 | 0/0.0977551 -> 0/0.224874 | 6.25676e-05 / 6.16823e-05 | `8137045eb` |
| CPU/grid/1e-4 | 11,628 -> 34,940 | 0.011667 -> 0.011155 | 0.010296 | 0.0312259 -> 0.0429792 | 0/0.0059173 -> 0/0.0104422 | 6.02518e-06 / 5.83067e-06 | `8137045eb` |
| CPU/grid/1e-3 | 11,628 -> 34,924 | 0.011259 -> 0.010865 | 0.009715 | 0.0284086 -> 0.0423433 | 0/0.0044115 -> 0/0.0086695 | 1.13393e-05 / 3.13675e-05 | `8137045eb` |
| OPTIX/grid/1e-4 | 23,254 -> 69,874 | 0.068277 -> 0.080363 | 0.063097 | 0.0246222 -> 0.0441991 | 0/0.0043375 -> 0/0.0097399 | 6.02518e-06 / 5.83067e-06 | `8137045eb` |
| OPTIX/grid/1e-3 | 23,254 -> 69,842 | 0.071962 -> 0.073552 | 0.061749 | 0.0235309 -> 0.0410225 | 0/0.0042288 -> 0/0.0086006 | 1.13392e-05 / 3.13675e-05 | `8137045eb` |
| CPU/svm_optin/1e-4 | 277,294 -> 957,346 | 0.019233 -> 0.025608 | 0.010504 | 0.418531 -> 0.896365 | 0/0.114862 -> 0/0.203724 | 6.14319e-06 / 6.19989e-06 | `8137045eb` |
| CPU/svm_optin/1e-3 | 277,294 -> 834,426 | 0.020193 -> 0.026453 | 0.010343 | 0.417208 -> 0.815188 | 0/0.0979099 -> 0/0.182739 | 6.04393e-05 / 6.26186e-05 | `8137045eb` |
| CUDA/svm_optin/1e-4 | 554,588 -> 1,914,692 | 0.093217 -> 0.184397 | 0.046775 | 0.419696 -> 0.906266 | 0/0.113441 -> 0/0.203663 | 6.14321e-06 / 6.20013e-06 | `8137045eb` |
| CUDA/svm_optin/1e-3 | 554,588 -> 1,668,852 | 0.090359 -> 0.164768 | 0.041927 | 0.410956 -> 0.831823 | 0/0.095821 -> 0/0.185347 | 6.04379e-05 / 6.26185e-05 | `8137045eb` |
| OPTIX/svm_optin/1e-4 | 554,588 -> 1,914,692 | 0.106362 -> 0.199363 | 0.065455 | 0.414513 -> 0.906904 | 0/0.114308 -> 0/0.207495 | 6.14321e-06 / 6.20013e-06 | `8137045eb` |
| OPTIX/svm_optin/1e-3 | 554,588 -> 1,668,852 | 0.108145 -> 0.184386 | 0.064029 | 0.413309 -> 0.820901 | 0/0.0973145 -> 0/0.182712 | 6.04379e-05 / 6.26185e-05 | `8137045eb` |

All 18 shader cases pass their header bounds, finer reference, h64 oracle, same-build rerun and CPU exact/GPU calibrated beauty gates; 31 explicit/atomic rejection checks pass. Fixed vs adaptive readback/spill bytes, deep samples, EXR size and stage timings are in [results](builds/validation/landscape-cloud/optimization-phase8c/final-results.json) and [qualification](builds/validation/landscape-cloud/optimization-phase8c/final-shader-volumes.json).

Adaptive adds work at an already small step. A larger starting cap allows refinement where needed; this is a measured speed gain on the textured fixture, not a universal speedup.

| Texture / 1e-4 | Fixed .005 | Adaptive .005 | Adaptive .32 cap (voxel maximum .125) | Commit |
| --- | ---: | ---: | ---: | --- |
| CPU point evaluations | 277294 | 835402 | 111348 | `8137045eb` |
| CPU export s | 0.426315 | 0.794392 | 0.0967644 | `8137045eb` |
| CPU render+capture s | 0.022247 | 0.042477 | 0.013026 | `8137045eb` |
| CPU h64 EXR error | - | 1.30236e-05 | 1.27354e-05 | `8137045eb` |
| OPTIX point evaluations | 554588 | 1.6708e+06 | 245866 | `8137045eb` |
| OPTIX export s | 0.396374 | 1.08147 | 0.104373 | `8137045eb` |
| OPTIX render+capture s | 0.169949 | 0.398914 | 0.262459 | `8137045eb` |
| OPTIX h64 EXR error | - | 1.30257e-05 | 1.27357e-05 | `8137045eb` |

Native cases retain the fresh Phase 8b before baseline: 47x20/max16 adaptive small landscape and 587x250x4 performance landscape, CUDA/OptiX, all three error modes. Every payload/header is identical. Single-run warm measurements; timing differences are not asserted as speedups.

| Native case | Render+capture s before -> after | Export s before -> after | Density/mixture aggregate s before -> after | Deep samples | EXR MiB before -> after | Commit |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| small/CUDA/strict | 2.8898 -> 2.82126 | 6.74272 -> 8.11307 | 2.03987/17.4251 -> 2.35416/22.8935 | 561,860 | 4.24057 -> 4.24057 | `8137045eb` |
| small/CUDA/1e-4 | 1.77967 -> 1.80548 | 3.38852 -> 3.54537 | 0.242265/0.833786 -> 0.25199/0.978533 | 89,072 | 0.584493 -> 0.584493 | `8137045eb` |
| small/CUDA/1e-3 | 1.79925 -> 1.79567 | 3.3417 -> 3.54146 | 0.179784/0.36353 -> 0.181885/0.382722 | 53,481 | 0.343897 -> 0.343897 | `8137045eb` |
| small/OPTIX/strict | 4.78539 -> 4.65984 | 6.93761 -> 7.82229 | 2.06886/17.8724 -> 2.33859/22.7283 | 561,628 | 4.23724 -> 4.23724 | `8137045eb` |
| small/OPTIX/1e-4 | 1.71368 -> 1.71085 | 3.35147 -> 3.94624 | 0.242559/0.830978 -> 0.257833/0.988155 | 88,934 | 0.582538 -> 0.582538 | `8137045eb` |
| small/OPTIX/1e-3 | 1.71285 -> 1.72238 | 3.25599 -> 3.48957 | 0.177653/0.3634 -> 0.183278/0.388969 | 53,356 | 0.341861 -> 0.341861 | `8137045eb` |
| performance/CUDA/strict | 34.1692 -> 33.8201 | 33.2436 -> 33.0227 | 70.9172/247.944 -> 70.687/247.682 | 52,191,416 | 376.055 -> 376.055 | `8137045eb` |
| performance/CUDA/1e-4 | 11.5481 -> 11.6942 | 3.84417 -> 3.6701 | 4.75664/17.4307 -> 4.71914/17.3338 | 6,828,290 | 56.5964 -> 56.5964 | `8137045eb` |
| performance/CUDA/1e-3 | 11.603 -> 11.6299 | 2.41151 -> 2.14213 | 1.70778/6.9513 -> 1.70731/6.98326 | 3,090,807 | 21.5278 -> 21.5278 | `8137045eb` |
| performance/OPTIX/strict | 54.2198 -> 54.3955 | 33.1896 -> 32.8239 | 70.7652/247.076 -> 70.114/244.542 | 52,183,415 | 375.982 -> 375.982 | `8137045eb` |
| performance/OPTIX/1e-4 | 10.9084 -> 10.9132 | 3.75528 -> 3.62584 | 4.74091/17.3392 -> 4.70775/17.2395 | 6,823,509 | 56.5569 -> 56.5569 | `8137045eb` |
| performance/OPTIX/1e-3 | 10.1024 -> 10.1665 | 2.10196 -> 2.11725 | 1.70201/6.94212 -> 1.70407/6.96747 | 3,086,077 | 21.4776 -> 21.4776 | `8137045eb` |

Final native identity **81/81 strict, 30/30 numeric, OptiX 65/65**; nine CTests, CPU beauty exactness, CPU/CUDA/OptiX matrices, boundaries, AO/Bevel and full calibrated CUDA/OptiX beauty all pass. Native OSL surface payload/header replay is 30/30 unchanged. Beauty source hash `002ab0fac1db9181633d6aefffb80e6c2a8af36a3e980e4a8947268d5b93d11e` and all 75 common kernel resource records are unchanged.

Full regression (including optional OptiX/GPU beauty stages): 2012.73 s. [Report](builds/validation/deep-regression/20261008T235713Z-283b08f9/results.json). [OSL surfaces](builds/validation/landscape-cloud/optimization-phase8c/mip/osl-surface-replay.json). [Resources](builds/validation/landscape-cloud/optimization-phase8c/mip-resources.json).

User-approved step-2 accumulation floor is recorded in Section 2 and applied by `a549dd619`; step 1, colour/reference floors, calibration, count and bias rules are unchanged.

| Saved step-2 case | Absolute difference | Previous allowance / result | Accumulation-aware allowance / result | Commit |
| --- | ---: | --- | --- | --- |
| CUDA small / 1e-4, (22,8), Normal.X | 1.11758708954e-8 | 9.76033028295e-9 / FAIL | 4.76837158203e-7 / PASS | `a549dd619` |
| Earlier OptiX opaque foreground / 1e-3, (0,5), Normal.X | 2.22044604925e-16 | result-magnitude floor / flagged | 4.76837158203e-7 / PASS | `a549dd619` |

The historical audit checks 1,656 bounded normal/albedo step-2 observations in 55 saved reports; four changed report occurrences represent these two pixel/channel cases, with no new failures. Original reports remain unchanged. [Audit](builds/validation/landscape-cloud/optimization-phase8c/accumulation-ulp-audit.json).

All 64 native CUDA/OptiX beauty targets pass the updated policy. The completed numerical/identity/CTest stages and native renders were retained; only GPU beauty was re-evaluated, with zero native renders repeated. Original stopped evidence remains in [the preserved report](builds/validation/deep-regression/20261008T235713Z-283b08f9/results-before-accumulation-ulp.json). The fresh 30-case OSL surface replay passes identity, own oracles and CPU exact/OptiX beauty.

The native mip selector unit integrates its two-level probabilities independently; the OptiX device unit repeats exactly and matches the known expected-density analytic curve within the header bound. [Device filter check](builds/validation/landscape-cloud/optimization-phase8c/device-mip-unit-v4.json). It is a filter unit, not a cross-backend scene qualification.

Weak synthetic grids use native Blender clipping=0 so the intended geometry is present; an explicit nonempty-output assertion caught the original clipped fixture. Existing qualified VDB/landscape assets are unchanged.

Large EXRs/CSV/spill/TEMP: `D:/CyclesDeepScratch/optimization-phase8c/qualified-mip/` and the regression D: root in its report. Connected Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase8c_final_review.gfr` (actual deep inputs, alpha/beauty/fine-step selection, DeepSlice -> DeepToPointCloud). [Presentation checks](builds/validation/landscape-cloud/optimization-phase8c/gaffer-review.json). Phase 8c accepted; Phase 9 launch confirmed by the user on 2026-10-09.

### Phase 9 - production preflight (2026-10-09)

[Preflight](builds/validation/landscape-cloud/optimization-phase9/preflight.json):
installed `D:/CyclesDeepScratch/optimization-phase8c/mip/blender/blender.exe`,
SHA-256 `677aab08cfb6ffb02298f7d67f1e61a24801fd1b195436524a935871b85358a8`.
clang-cl 20.1.8 / NVCC 12.8.61 / CUDA 12.8.0 / OptiX 8.0.0 / OSL 1.15.3.0.
Scene hash `4f40bd62911d8d6c81e373126bb7ea81f4264c476f0d5b44caef2faa9e63c902`;
376 objects, 110 materials, all external assets present. Original threshold
0.03/minimum 8, seed 0/frame 0/animated seed, GPU OIDN retained.
High Performance active: AC/DC automatic sleep and hibernate both Never.
Large outputs and child TEMP/TMP/cache paths:
`D:/CyclesDeepScratch/optimization-phase9/production-20261009/`.
Production timings/gates remain pending; no Gaffer production review created.

#### Phase 9 validation-harness repair (2026-10-09)

Run 1 rendered and published successfully; the queue stopped before the
independent oracle because Python `-I` omitted its sibling `sample_csv` import.
The oracle CLI now explicitly adds its own directory; the same fix covers the
Gaffer validator's isolated invocation. No renderer/binary or gate changed.
[Dry run](builds/validation/landscape-cloud/optimization-phase9/resume-dry-run.json):
15/15 checks on preserved 47x20/33x17 outputs, 13.52 seconds, zero new renders.
Includes exterior comparison, curve extraction, isolated oracle, Gaffer cuts,
raw/native population checks, all five control/four seed command validations,
and the full beauty-policy invocation. User authorized continuation from run 2
only after preserved run-1 checks pass; deep-memory remains 8192 MiB.

#### Phase 9 - preserved run 1 checks passed; run 2 resumed (2026-10-09)

[Run 1 checks](builds/validation/landscape-cloud/optimization-phase9/run1-checks.json)
and [measurements](builds/validation/landscape-cloud/optimization-phase9/run1-metrics.json).
119,223,274 independent probes across 81 diagnostic pixels pass. Oracle plus
full-image exterior error is 8.23258e-5, below the unchanged 1e-3 header bound.
Five Gaffer depth cuts pass; deep/native accepted populations match exactly.
No run-1 render or export repeated. Beauty remains pending the requested controls.

| OptiX, IDs, 1e-3, all samples | Phase 5 projection | Measured run 1 |
|---|---:|---:|
| Render + capture | 190.98 min | 71.06 min |
| Production export | 187.43 min | 28.68 min |
| Same-capture z=0 validation export | Not projected | 34.66 min |
| Spill | 181.32 GB | 167.87 GB |
| Production EXR | 4.39 GB | 1.99 GB |
| Peak host | 13.93 GiB budget estimate | 5.79 GiB |
| Peak total GPU | 5925 MiB estimate | 6723 MiB |
| Production deep records | Not projected | 281,802,862 |
| Max independent oracle error | <=1e-3 gate | 8.22990e-5 |
| Max exterior / flattened-alpha difference | <=1e-3 gate | 2.68281e-8 / 0 |
| Max Gaffer cut error | <=1e-3 gate | 2.60427e-6 |
| Native accepted samples, min / median / max | Not projected | 16 / 288 / 1024 |

Projection used CUDA, IDs off and z=0; differences are informational, not a
controlled speedup claim. Production timing excludes the extra validation export.
The explicitly authorized resume starts at deep-samples 64, then five ordinary
and four seed-varied deep-off references, then the unchanged beauty policy.
Same qualified executable and 8192 MiB deep-memory setting; all large files/TEMP
remain under D:/CyclesDeepScratch/optimization-phase9/production-20261009/.
Failure stops the queue without retry. No production Gaffer review created yet.

#### Phase 9 - eleven renders complete; stopped on calibrated count rate

[Results](builds/validation/landscape-cloud/optimization-phase9/production-results.json)
retain per-run counters, fitting times, all nine references and projection comparisons.
No render/export repeated. Both independent camera oracles, full-image exterior,
Gaffer cuts and native populations pass. The empty-reference validator crash is
fixed: missing counts have a separate calibrated category. Both preserved runs
fail the user-approved count-rate rule; direction passes. Matched-count raw/bias
checks were not run after this failure. No investigation, extra render or Gaffer
review. Runnable validator tests cover count-rate boundaries, direction rejection,
ties, exact binomial tails, and an allowed unmatched-count pixel end to end.

| OptiX, IDs, error 1e-3, z=1e-4 | All deep samples | Deep prefix 64 |
|---|---:|---:|
| Render + capture | 71.06 min | 15.39 min |
| Production export | 28.68 min | 2.89 min |
| Validation companion export | 34.66 min | 3.01 min |
| Peak host / total GPU | 5.79 GiB / 6723 MiB | 5.24 GiB / 6308 MiB |
| Peak process GPU | 3.74 GiB | 3.74 GiB |
| Spill / EXR | 167.87 / 1.99 GB | 20.10 / 0.705 GB |
| GPU lane writes / copied | 148.11 / 174.45 GB | 18.38 / 38.72 GB |
| Deep records | 281,802,862 | 86,125,949 |
| Oracle max error | 8.22990e-5 | 8.22990e-5 |
| Gaffer cut max error | 2.60427e-6 | 2.84080e-7 |
| Exterior max / flattened alpha | 2.68281e-8 / 0 | 2.68261e-8 / 0 |
| Native accepted min / median / max | 16 / 288 / 1024 | 16 / 288 / 1024 |
| Beauty policy | Count-rate FAIL; direction PASS | Count-rate FAIL; direction PASS |

Phase 5 projected render+capture/export: all 190.98/187.43 min; prefix64
68.55/24.80 min. These are CUDA IDs-off z=0 estimates, not controlled speedups.
The shader/host/device budgets are unchanged; numerical prefix checks evaluate
the captured prefix, not an approximation bound against all beauty rays.

Count calibration (587,500 pixels; five existing ordinary controls):

| Metric | Controls (leave one out) | All deep samples | Deep prefix 64 |
|---|---|---:|---:|
| Unmatched count, min / median / max | 252 / 295 / 329 | Four-subset mean 488: FAIL | Four-subset mean 470.6: FAIL |
| Five four-reference counts | 329, 278, 307, 252, 295 | 489, 490, 501, 480, 480 | 462, 483, 473, 460, 475 |
| Unmatched against all five | — | 460 | 439 |
| Fewer-sample direction | Range 39.749%–56.771% | 151/377 = 40.053%: PASS | 157/363 = 43.251%: PASS |
| Modal ties excluded from direction | 90, 86, 81, 89, 102 | 83 | 76 |
| Two-sided binomial p | Recorded per control | 0.000131972 | 0.0116538 |
| Count differences from unique mode | Full histograms in reports | -560 to +544; -16: 53, +16: 100 | -608 to +496; -16: 56, +16: 59 |

Differences are multiples of 16, including multiple adaptive steps. Ties are
excluded only from direction, not from the count-rate test. Run 1's small
binomial p alone does not fail: its direction is inside the calibrated range.
[Run 1 count report](builds/validation/landscape-cloud/optimization-phase9/deep-all-beauty-count-policy.json)
and [run 2 count report](builds/validation/landscape-cloud/optimization-phase9/deep-64-beauty-count-policy.json)
retain every unmatched pixel, subset, exclusion and histogram. This exceeds the
control rate range; cause is uninvestigated as instructed. Phase 9 is incomplete.

#### Phase 9 - approved majorant count diagnostic (running)

One user-approved experiment: production source/toolchain plus isolated snapshot
patch `9a017f055`, full1175x500/max1024 adaptive/GPU OIDN/OptiX, three deep-off
then three deep-on/error1e-3/prefix64/IDs/default z/8192 MiB renders. Diagnostic
install, TEMP and outputs: `D:/CyclesDeepScratch/optimization-phase9/majorant-count-diagnostic-20261009/`.
The previous backup/build/install/restore procedure preserves production binary
`677aab08…358a8`, installed modules and generated source/cache originals.
Compare off leave-one-out (two references) with each on run's mean across three
two-reference subsets; also report unmatched counts against all three controls,
direction and tie exclusions. No gate change or acceptance; record the result
and stop for the user's decision. No further investigation or retries.
