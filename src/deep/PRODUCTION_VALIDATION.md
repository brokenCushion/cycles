# M5 — bounded CPU storage, reduction and publication

Implemented for the existing standalone CPU surface contract. This is a
candidate CPU implementation, not farm, GPU or full Blender qualification.

## Storage and controls

- The renderer now always spills raw capture to a temporary binary file.
  Checked fixed offsets retain every `(x, y, camera sample)` identity; explicit
  markers distinguish missing samples, completed misses and complete chains.
  Duplicate writes fail. Workers serialize file access; no normalized batch
  alphas are averaged. The supported fixed-sample box-filter contract has unit
  weights, unchanged from M3/M4. General weighted/adaptive batches remain M7.
- `--deep-memory-mb` (default 64, range 1–1024) preflights a conservative deep
  working-set reservation: 1 MiB plus `width * (128 + E * 128) + E * 512`, where
  `E = camera samples * max(1, max events)`. Reconstruction holds one pixel;
  output holds one scanline, including FLOAT staging and reduction validation.
  Height increases disk use, not deep application buffers. Oversized rows fail
  before rendering. This is a sizing bound, not an allocator/RSS enforcement
  mechanism; beauty, scene/shader data, allocator overhead and OS filesystem
  cache are outside this deep-only control. No device capture allocation exists.
- Spill disk capacity is still approximately
  `width * height * samples * (1 + 2 * max_events) * 4` bytes. Initialization and
  I/O failures fail the job; no events are silently dropped. The temporary file
  closes/deletes on normal destruction. Hard process termination and power-loss
  recovery are not qualified. Disk access is intentionally serialized and is a
  performance limitation; no high-throughput farm claim is made.
- `--deep-reduce` enables a strict 0.001 maximum absolute transmittance error.
  The default is off: unreduced reconstructed samples, not raw hits. Reduction
  delays small opacity steps to existing depths, preserves final transmittance,
  and reserves 1e-6 for FLOAT rounding. The writer checks the final FLOAT curve
  against the original at the union of all boundaries. Strong steps remain.
  No HALF, positional approximation preset, geometry-based merging or deep RGB.

## Output lifecycle

One owner exports after worker completion. EXRs are written into a uniquely
reserved sibling temporary directory, closed successfully, then renamed over
the destination (Windows `MoveFileExW`, POSIX rename). Failures preserve the old
final EXR and remove the temporary file during exception unwinding. Cancellation
is checked between rows and immediately before publication. Atomic rename is
qualified on the local filesystem only; network shares and crash durability are
not certified.

Beauty write and close success is now checked for jobs requesting both outputs.
The deep header stores `cycles:beautyIdentity`: path, byte count and FNV-1a64
checksum of the completed beauty file. This identifies which beauty belongs to
the deep file, including when an old deep file survives a later failed job.
It is not a cryptographic checksum or an automatic Gaffer pairing validator.
Beauty/deep are not a single filesystem transaction. The optional diagnostic
CSV publishes separately before the EXR and is not a frame-completion marker.

## Validation results

Artifacts: `build-m3/m5-acceptance/`, executable `install/cycles-m5.exe`.

- Feature-enabled and feature-disabled Release builds pass. Five CTest checks
  pass in each: capture, reference, production lifecycle, OpenEXR and version.
  Spill/memory outputs agree, including shuffled concurrent samples and misses;
  incomplete/duplicate ledgers and undersized budgets fail.
- Whole-image and streaming EXR round trips cover both compression modes,
  positive/negative window offsets and empty pixels. Injected stream-write
  failures exercise both writers. Mid-export cancellation, allocation failure,
  simulated disk-full exceptions and cancellation after the last row preserve
  the prior final file byte-for-byte. Disk-full is fault injection, not a filled
  physical drive. Rename failure is also exercised against a directory target.
- M4's nine native/OSL scenes and eight rejection cases pass with disk storage.
  Beauty pixels stay bit-identical. Maximum raw-reference/exported curve error
  is 2.60770320892334e-8; SVM/OSL opacity difference is zero.
- The original M3 seven-scene opaque suite and its rejection checks also pass
  with the final executable. Beauty remains unchanged; maximum curve error is
  2.7939677238464355e-8.
- A 128x96, 16-sample, 64-layer render needs **101,449,728 bytes** of raw spill
  while using a **32 MiB** deep working-memory allowance. A 1 MiB allowance fails
  before rendering and preserves the existing deep destination.
- Gaffer independently sweeps both sides of every boundary in all 12,288 pixels:
  samples **2,214,155 → 350,403** (84.17% fewer); EXR **4,399,394 → 1,117,907 bytes**;
  maximum curve error **0.0009987972072165174**; final opacity error
  **8.315972355177337e-10**. Deep-on, reduced, and beauty-only RGBA pixels match.
- Eight Gaffer DeepSlice holdout depths at four image positions pass; maximum
  alpha difference **0.000979013741016388**. Beauty identity checksum is verified.
- Real three-primitive geometry also renders at 640x480, 16 samples, 32 MiB:
  approximately 74.5/75.75 seconds including spill and export; unreduced/reduced
  EXRs are 13,417,436/13,417,433 bytes. Strong steps give almost no reduction.
  These timings are single local observations, not repeatable benchmark medians.

## Gaffer review

Open `build-m3/m5-acceptance/m5_review.gfr`. There are four columns:
`Layers_unreduced`, `Layers_reduced`, `Primitives_unreduced`, `Primitives_reduced`.
Each has a reader, DeepToFlat, live DeepToPointCloud and editable DeepSlice.
Select a `_Points` node and view it, then frame `/deepPoints`. Blue is unreduced;
orange is reduced. The point cap is 1,000,000; it is preview thinning, separate
from the EXR reduction. Headless evaluation returns 1,000,000 / 350,403 points
for the layer pair, and 1,000,000 for each primitive preview. All points come
from the deep files. Images contain Z/ZBack/A; preview colors are identification
colors, not rendered deep beauty.

The Gaffer window was launched, but Windows denied input access and returned a
black capture, so interactive viewport framing could not be verified this run.

## Reproduce

```powershell
cmake --build build-m3 --config Release --target cycles cycles_deep_capture_test cycles_deep_exr_test cycles_deep_production_test
ctest --test-dir build-m3 -C Release --output-on-failure
Copy-Item build-m3/bin/Release/cycles.exe install/cycles-m5.exe
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_transparency_gaffer.py install/cycles-m5.exe build-m3/m5-m4-regression
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_production_gaffer.py install/cycles-m5.exe build-m3/m5-acceptance
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/check_production_lifecycle.py install/cycles-m5.exe build-m3/m5-acceptance
```

The primitive portion reuses the XML generated by `create_primitives_review.py`
when present. That script remains the M4 test-asset generator. M5 layer fixtures
and all lifecycle tests are self-contained.
