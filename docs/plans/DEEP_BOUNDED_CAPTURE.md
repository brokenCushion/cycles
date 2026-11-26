# Deep bounded capture (plan)

Status: **in progress**. Branch `blender-deep-multilayer`.
Stage 1 (spill safety and size) implemented 2026-11-26: spill directory and
delete-on-close files, sparse index, zstd spill (2.56x on junkshop), early stop and
space projection, budget numbers, interval histogram. Junkshop Volume 25%: spill
62.0 -> 24.5 GB, wall time +7.7%. Measured per-node volume intervals: mean
150-420, max about 1.2k (sizes Stage 2).
Stage 2 (merge volumes during render) implemented 2026-11-26: online aligned-range
tree, node arena and stream, ledger. Strict and non-adaptive volumes byte-identical;
adaptive volumes use cap levels and pass the oracle within bound; their 11 golden
references were regenerated (user-approved) into deep-references\n...-20261126-volume-reduce, regression PASS with only the 15 pre-existing
landscape/surface_bevel divergences. Junkshop Volume 25%: spill 24.5 GB -> 0, wall 341 -> 195 s;
100% at 32 spp: disk 467 -> 21.5 GB peak, 43.5 -> 34 min, deep_memory_mb 32768 ->
16384, EXR byte-identical.

Goal: intermediate deep data is bounded per pixel and per band, not by
samples x events. The numeric error contract in
[SUPPORT.md](../deep/SUPPORT.md#numerical-and-storage-contract) and strict-mode
bytes stay unchanged.

## 1. Problem and measured baseline

`Capture` spills every camera sample and reduces only after the last sample:

- `record_sample` (capture.cpp:477) -> `store_record` (:375) appends a 12 B
  `SpillRecord` header, N x 20 B `KernelDeepEvent` and 32 B `KernelDeepDensity`
  per grid-volume event to a per-band `std::tmpfile` stream.
- The constructor (:128-152) also zero-fills a fixed-stride index of
  `width*rows*samples*sizeof(SpillRecord)` per band (about 6 GB at
  2000x1000x128) before the first sample.
- Reduction happens only at export: `reconstruct_volume_pixel` (:970) decodes
  every ray with `volume_sample` (:571) and folds a balanced tree (:1038-1072,
  `retained_curves` about log2 N). `load_stream` (:736) stages a whole row, or a
  rebucketed row file, in RAM.

Measured on the junkshop VDB layer (2000x1000, 128 spp, about 244 events per
sample): about 985 GB of temp disk, and one export row stages about 4.5 GB
against a budget of `deep_memory_mb/8` (:76). The final EXRs are normal-sized.
Raw events dominate, not information content. Evidence:
`D:\temp\cycles_deep_junkshop\diag_spill\` (`cpu25_*`, `band_rows.json`).
MoonRay, by comparison, merges surfaces online per sample and compresses volumes
per pixel per thread (`moonray/lib/rendering/pbr/core/DeepBuffer.cc`).

Observations that shape the plan:

- `tmpfile()` lands on `%TEMP%`; nothing chooses the drive.
- A spill I/O error is not fatal early. `set_error(IO_ERROR)` (:439) only stops
  later `record_sample` calls (:491), so rendering continues to the end and fails
  in `write_deep_output` (path_trace.cpp:819).
- The volume tree is a pure function of sample indices. It pairs
  equal-population neighbours by index, with node tolerance
  `budget.reconstruction/(2*levels)` (:1021), so it does not depend on when the
  merges run.
- Opaque surfaces barely reduce. With z tolerance 0, distinct subpixel depths
  stay distinct. Surface reduction mostly helps duplicate depths, transparent
  layers and runs with z tolerance > 0.

## 2. Where reduction can happen

| Point | Verdict | Reason |
| --- | --- | --- |
| GPU/CPU kernels (`deep_surface`) | No | No per-thread allocation and no cross-pixel state; kernels already apply per-ray eps_ray. |
| **Host ingest** `Capture::record_sample` | **Primary** | Single choke point (CPU path_trace_work_cpu.cpp:215, 276; GPU path_trace_work_gpu.cpp:1144, 1220), host-side and outside beauty scheduling. |
| **Pass boundary / band memory full** | **Yes** | Per-band LSM compaction of per-pixel nodes (section 4). |
| Export (`begin_export_row`, `reconstruct_*`) | Reduced | Final fold over a few nodes per pixel instead of N rays. |

Ordering:

- **CPU.** One worker loops all `num_samples` of a pixel, then calls
  `set_population` (path_trace_work_cpu.cpp:~327). This is a natural
  pixel-finish hook.
- **GPU.** With the default scrambling distance, the lanes of one pixel are
  contiguous (`get_work_pixel`, work_stealing.h:31). Verify this for the deep
  kernel; otherwise fall back to the per-band hot arena. Correctness never
  depends on lane order, only on increasing sample index per pixel.

## 3. Algorithms and error accounting

### 3.1 Volumes: capture-time binary counter plus LSM compaction

Reuse the existing math exactly. Per pixel, keep a stack of nodes
`{first_sample, population, packed curve}`. To ingest ray s:

1. Compute `volume_sample` with the existing density tolerance.
2. Apply the existing one-ray `fit` (:1033) when `compact_ray`.
3. Push the result as a population-1 node.
4. While the top two nodes have equal population, `combine` (:1014) them with
   the same `budget.reconstruction/(2*levels)` allowance.

The error bound is unchanged: per-ray error is at most eps_ray, a convex average
keeps it, and each level adds its existing allotment. Export only folds the
remaining nodes and runs the final `fit`.

- **Ledger.** Each node carries `err_node = max(err_l, err_r) + spent_fit`.
  Export checks it against the budget and fails explicitly with the numbers.
- **Adaptive sampling.** Online, `levels` comes from the sample cap
  `ceil(log2 samples_)`. This is conservative but not bit-identical for adaptive
  volumes, so those references are regenerated under VALIDATION.md.
  Non-adaptive runs use the same `levels` as today.
- **Non-grid volumes or `samples <= 8`.** Keep the raw list (bounded).
- **LSM storage.** Each band keeps a hot arena of node curves. Equal-population
  neighbours are compacted at the end of each render work pass and when the
  arena fills, and the survivors are appended to the band's node stream. Steady
  state is at most `popcount(n) <= L` nodes per pixel (7 at 128 spp). Export
  reads only that pixel's nodes.

### 3.2 Surfaces: running sum curve with local error ledger

Per pixel, keep `S(z) = sum_s prod_{e in s, z_e <= z} (1 - alpha_e)` as a sorted
breakpoint list `{z, object|facing, dS (f64), err}`.

- **Ingest.** Each sample's events are sorted (depth, object) as the oracle does
  and merge-added. Equal keys add exactly, so this step is lossless.
- **Capacity.** On overflow, merge the min-cost pair (`min(dS_a, dS_b)`) with
  `err = err_a + err_b + cost`. A merge is allowed only if
  `err <= B_acc * W_t` (accepted weight so far). Later samples only dilute the
  error, so the T-error `err/W_final` can only shrink.
- **Rules.** Merges stay within one object in ID mode and follow the existing
  facing rules. Volume layers never use this path.
- **Budget.** `B_acc` comes from the existing E-F surface allowance. Export
  spends the remainder per pixel, and the header keeps E.
- **Opaque-only mode and z tolerance.** The same code handles opaque-only mode.
  With z tolerance > 0, apply `merge_surface_depths` at ingest too.

### 3.3 Optional lossy MoonRay-style mode

An opt-in `--deep-compress lossy` mode:

- Enforces a hard per-pixel segment cap (for example 32 surface / 64 volume
  nodes) with forced min-cost merges.
- Writes the measured max ledger as the effective error in the header.
- Volumes may use the sigmaT-difference merge (`< max/volCompressionRes`);
  surfaces may use an 8x8 coverage mask.
- Validators compare flattened alpha with beauty. No strict or numeric identity
  claim is made in this mode.

## 4. Data structures and bounds

- **Pixel state:** `{u32 accepted, u32 arena_offset, u16 node_count,
  u16 flags}`, about 12 B per pixel (24 MB at 2M pixels). It replaces the
  24 B x samples index.
- **Node:** a header `{pixel, first_sample, population, err_node,
  n_intervals}` plus n x 24 B `{boundary f64, hazard/alpha f64, object i32}`.
- **RAM.** The arena uses at most its share of `spill_memory_bytes_`. Export
  staging becomes `width x L x K x 24 B` per row: about 85 MB for K = 256, versus
  4.5 GB today. The `load_stream` row failure cannot occur.
- **Disk.** Per band, `pixels x (L x K_node x 24 B + 64 B)` plus an
  uncompacted tail of at most the arena size. For the dust layer (2M pixels,
  L = 7, K_node = 200) that is at most about 67 GB, and about 10-20 GB in
  steady state, versus 985 GB today. Measure K_node in stage 1 (spill safety) first. Removing the
  sparse index saves a further 6 GB.
- **Failure modes.** Each fails explicitly with numbers:
  - a node exceeds `reconstruction_limit()`;
  - the arena plus staging exceed the preflight budget;
  - free disk is below the projection;
  - the ledger exceeds the budget.

## 5. Strict, IDs, adaptive, deep_samples, motion, holdouts

- **Strict surfaces** cannot merge lossily. They keep the raw spill with only
  the stage 1 (spill safety) improvements and lossless compression, so their bytes are unchanged.
- **Strict volumes with `compact_ray`** run the capture-time tree with identical
  operations, so their bytes must be identical. This is the strongest gate.
- **IDs.** Per-interval object is kept, and surfaces merge only within one
  object. Volumes go through `reconstruct_volume_ids` via the same `fit`
  wrapper. Per-object measured error feeds the manifest error sum.
- **Adaptive sampling.** `set_population` stays authoritative, and `finalize()`
  checks the completed sample counters.
- **deep_samples N.** Every accepted sample weighs 1, and N caps `levels`.
- **Motion blur.** Each ray is one curve at its own shutter time, so reduction
  is per-pixel averaging and time never matters.
- **Holdouts.** They are object-level and objects are never merged, so holdouts
  are unaffected.
- **Duplicate detection.** It is lost with the index. Replace it with a
  per-pixel (last_sample, count) pair, plus a bitset when `samples <= 512`, and
  raise explicit `DUPLICATE` errors.
- **Cancellation.** Compaction threads observe cancel and join before
  `Capture` destruction. Atomic `.partial` publication is unchanged.

## 6. CPU vs CUDA/OptiX

The reducer is host-only. Kernels and the beauty scope policy are unchanged, and
GPU kernel resource records stay valid.

- **CPU.** Thread-local pixel buffers sit in the per-pixel sample loop, with
  `finish_pixel(x, y)` next to `set_population`.
- **GPU.** `consume` (path_trace_work_gpu.cpp:~1100-1150) feeds the reducer per
  lane, grouped by pixel. Merges run on a host pool that overlaps the next batch
  through the two `deep_batches_`.
- **Kernel rule.** There is still no GPU allocation, file I/O or STL in kernels.

## 7. Stages

Each stage lands and is validated on its own.

### Stage 1: spill safety and size (no format change)

1. Explicit spill directory (`CYCLES_DEEP_SPILL_DIR` / Blender option; default
   `%TEMP%`), using delete-on-close files.
2. Early I/O-error cancel: poll `Capture::failed()` in the work loop and cancel
   with the message.
3. Projection: after the first pass, extrapolate spill bytes against free disk
   and fail early with both numbers. Add numbers to the export-budget errors.
4. Lossless block compression of the append stream (zstd/lz4 if bundled).
5. Sparse or omitted zero-filled index.
6. K histogram logging (intervals per node and level) to size K_node.

### Stage 2: merge volumes during render (the measured problem)

 Move `volume_sample`, one-ray `fit` and
`combine` to ingest, with LSM compaction and the export fold. Strict and
non-adaptive numeric volume output must stay byte-identical. Volumes go before surfaces because
they are the measured problem.

### Stage 3: merge surfaces during render

Shared infrastructure plus surfaces: The `PixelReducer` API (`add`,
`finish_pixel`, `compact_band`, `export_nodes`), pixel-state arena, node stream
v1, counters and the surface accumulator (3.2).

### Stage 4: hardening

 Compressed node stream, parallel per-band compaction, GPU
overlap tuning, bound enforcement and messages.

### Stage 5: optional lossy mode

See 3.3.

## 8. Validation

- **All stages.** `tools/run_deep_regression.py` must pass in full: nine
  CTests, CPU/CUDA matrices, boundaries, oracles, CPU beauty exactness,
  resource/source proof, 81/81 strict and 30/30 numeric identity, and 65/65
  OptiX SVM with `--optix`. Gates and references never change to make a failure
  pass ([VALIDATION.md](../deep/VALIDATION.md)).
- **Stage 1 (spill safety).**
  - Regression unchanged.
  - Full/unwritable spill drive: the render stops early with numbers and the
    previous EXR is kept.
  - Mid-render cancel.
  - Compression round-trip.
- **Stage 3 (surfaces).**
  - `bounded_capture_test.cpp` checks the accumulator against the
    `reconstruct()` oracle on random, coincident-depth, ID and overflow
    fixtures, asserting `measured <= ledger <= E-F` and determinism.
  - Existing surface, transparency, ID, holdout, motion and adaptive
    validators, plus the Gaffer validators.
- **Stage 2 (volumes).**
  - Volume, VDB, motion and shader-eval validators, including the 4x-finer-step
    check.
  - **Primary gate:** byte-identical EXRs to the pre-change build for strict and
    non-adaptive numeric volumes, across forced batch splits.
  - Adaptive volumes: oracle within the header bound.
- **Junkshop production** (`D:\temp\cycles_deep_junkshop\scripts\render_runs.py`;
  test renders in `D:\Projects\cycles_deep-data\test_renders\junkshop`;
  baselines in `diag_spill\cpu25_*`):
  1. 25% CPU: spill bytes, peak RAM, EXR identity, flatten-vs-beauty stats.
  2. Full-size CPU: disk well below the 985 GB baseline and within the formula,
     with no export-row failure.
  3. CUDA/OptiX at 25% and full size: the same checks plus the GPU beauty gate.
- **Lossy mode.** Flattened alpha matches beauty, and the header shows the
  measured bound.

## 9. Risks

- **Compute moves, it doesn't disappear.** Merging shifts from export to render
  time and competes with CPU rendering. Measure wall time; GPU overlap (stage 4 (hardening))
  mitigates it.
- **The disk saving depends on K_node** (estimated 10-70 GB, unmeasured).
  stage 1 (spill safety)'s histogram decides it; compression and a lower top-level limit are the
  fallbacks.
- **Adaptive volume numerics change** because `levels` comes from the sample
  cap. Document it and regenerate those references with the oracle as the gate.
- **GPU lane order is unverified.** The arena fallback costs RAM.
- **Duplicate and completion detection is weaker** without the index.
  Mitigation: counters, bitsets, explicit errors, and keeping the index for
  strict surfaces.
- **Surface merge accounting is new math.** Prove it with adversarial oracle
  tests, and keep per-object ID errors in the header sum.
- **Compaction and cancellation.** Threads must join, and partial streams must
  never reach publication.
- **Windows stdio limit.** The `_getmaxstdio` check stays, and node files count
  against it.
- **Strict surfaces remain unbounded.** State this in SUPPORT.md; numeric mode
  is the bounded path.

Critical files: `src/deep/capture.cpp`, `src/deep/capture.h`,
`src/integrator/path_trace_work_cpu.cpp`, `src/integrator/path_trace_work_gpu.cpp`,
`src/integrator/path_trace.cpp`.
