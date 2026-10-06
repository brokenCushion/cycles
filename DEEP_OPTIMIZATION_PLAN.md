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
- Keep the numerical contract explicit: every approximation has a stated
  absolute transmittance bound, and the bounds sum to the user-visible setting.
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

   Exact CUDA gate definition (user decision, 2026-10-06; supersedes any
   single-repeat envelope):
   - Envelope: render K = 5 ordinary deep-off runs. For each pass, the envelope
     is the maximum per-pixel absolute difference over all pairs of those runs.
     A single pair underestimates GPU run-to-run variation.
   - Raw (noisy) passes: each deep-on pixel must be within
     `max(envelope, 4 ULP of that pixel's value)` of at least one deep-off run
     with the same accepted sample count (keep the existing matched-count rule;
     unknown counts fail). 4 ULP is float accumulation-order noise.
   - Every pass the denoiser consumes (noisy beauty, denoising albedo, normal,
     depth, sample count) is checked the same way, not only the beauty.
   - Denoised beauty: report its difference vs. the K-run denoised envelope.
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
- In the kernel, compress each object's ordered cell stream as it is produced
  (`deep_volume_grid_capture` in `src/kernel/deep/volume_grid.h`, and the
  homogeneous path in `volume.h`). Emit constant-extinction `DEEP_VOLUME` events
  (front, back, optical_depth) instead of one `DEEP_VOLUME_CUBIC` + density
  record per cell.
- Algorithm (O(1) state per stream): keep an anchor `(z_a, tau_a)` and a feasible
  slope cone. Each cell boundary vertex `(z_i, tau_i)` with allowed deviation
  `delta_i = eps_ray / T(z_a) - chord_error_i` (use the existing
  `deep_density_chord_error` for within-cell deviation; `T(z_a)` is the
  already-absorbed prefix, so the bound is absolute transmittance error) narrows
  the cone. Extend while the chord from the anchor to the newest vertex lies
  inside the cone; otherwise emit the segment ending at the last feasible
  vertex with its exact tau (preserves total optical depth at segment ends) and
  re-anchor there. This is the same bound the host fitter already uses; reuse
  its proof and document it.
- Per-ray absolute T bound survives camera averaging (convex combination), so
  the host fitting budget is unaffected.
- Optional, same phase: once a single object's own `T_obj(z) <= eps_ray`, total
  T is also <= eps_ray (extinction adds). Emit an opaque surface event at that
  depth and stop. Error <= eps_ray. Only enabled when `error` is not strict.
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
- Larger launches: process all active work of the tile set per launch where the
  staging budget allows, rather than ~2,700-lane batches with ~10% active.

Acceptance: deep EXR byte-identical to Phase 3 for the same settings; CUDA
cancellation/error-injection suites pass; capture time and GPU peak memory
reported (stay within the existing 8192 MiB device gate).

### Phase 5 - Deep sample count setting

- Add `int samples` to `DeepSettings`: `0` = all accepted beauty samples
  (default, current behaviour). Positive N = capture deep only for the first N
  accepted samples of each pixel (the sampler prefix is stratified).
- Kernel/host: skip capture for `sample >= N`; per-pixel population becomes
  `min(accepted, N)`. Size `Capture` by `min(samples, N)` (large memory saving).
- Plumb `--deep-samples` and Blender `deep_samples`; write `cycles:deepSamples`
  to the EXR header. Validators use the same subset for the accepted-camera
  oracle.
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

Acceptance: ids off is byte-identical to Phase 5. With ids on: combined alpha
(after deep flatten/merge) matches ids off within the header tolerance on the
compatibility matrices and small landscape; Gaffer and the OpenEXR reader load
the overlapping samples correctly; selecting one object's id isolates it
(checked on an overlap fixture with known per-object alpha).

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
- Consolidate the many `validate_*_gaffer.py` scripts into that harness where
  they overlap. Archive (Git history) the per-milestone `*_VALIDATION.md`
  evidence files; keep `RELEASE_MATRIX.md`, this plan, and one short status page.
- Keep the Cycles core footprint small for future rebases onto newer
  Cycles/Blender: list every non-`deep/` file touched (`kernel/types.h`
  `KernelShader` field, `kernel/util/nanovdb.h` accessor, `surface_shader.h`
  template parameter, film/pass plumbing) in `src/deep/README.md` with the
  reason for each.

### Phase 8 - OptiX backend

Most NVIDIA users render with OptiX; today enabling deep forces the slower CUDA
backend for the whole render. The visibility chain is mostly BVH traversal, so
RT cores should also speed up capture itself.
- Add the `deep_surface` kernel to the OptiX module using the existing shared
  kernel code; OptiX `scene_intersect`/`scene_intersect_volume` paths already
  exist in Cycles.
- Run the native VDB grid code under OptiX with the same CUDA double-precision
  qualification (`volume_grid.h` currently gates on `__KERNEL_CUDA__`).
- Acceptance: CPU/CUDA/OptiX matrices; OptiX deep alpha within header tolerance
  of CUDA; beauty gate vs. OptiX deep-off.

### Phase 9 - Landscape production run (last)

Run only after Phases 0-8 pass and the user confirms. One run validates the
final pipeline. Use OptiX if Phase 8 passed (CUDA otherwise; record which).

Run the full landscape (1175x500, max 1024 adaptive, GPU OIDN) with
`--deep-error 1e-3 --deep-samples 64 --deep-ids`, then `1e-3` with ids off
for the size/time comparison. Report capture time, export time, peak host/GPU
memory, EXR size, alpha oracle, Gaffer cuts, beauty gate. Create the connected
Gaffer review only after gates pass. Optionally run `1e-4`, and strict only if
the earlier phases make it practical (< 12 h).

### Not recommended now: deep RGB

Compositing deep alpha with the flat beauty (Nuke DeepRecolor) already covers
the main use, holding out CG or FX inside clouds. True deep colour needs path
contributions attributed to depth for scattering volumes: a large research
project. Revisit only if a concrete comp need appears that DeepRecolor cannot
handle.

### Later

- General volume shader fallback: instead of graph pattern matching in
  `src/session/deep.cpp` (Ray Depth / multiply / add / power), evaluate the real
  volume shader with `PATH_RAY_EXTINCTION` along the ray at voxel-scale steps,
  with a stated (not proven) error. Keep the analytic VDB path as the fast path.
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
