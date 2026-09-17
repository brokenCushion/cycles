# M7a — Adaptive camera populations

Implemented the first M7 sub-gate: CPU adaptive sampling for the existing
static perspective pinhole surface scope, using native SVM or the existing
restricted CPU OSL material set. CUDA native adaptive capture now also passes
the local RTX 3080 qualification below. Depth of field is additionally qualified
in [DOF validation](DOF_VALIDATION.md); motion blur remains rejected. This does
not complete all of M7.

## Contract

Deep follows beauty's accepted sample population. It does not alter beauty's
convergence test, stopping rule, sample sequence or maximum sample budget.
Each accepted sample has unit weight under the existing box-filter contract.
Completed misses contribute to the denominator. Converged/skipped samples do
not masquerade as misses.

After each CPU pixel batch, capture receives the independent film sample
counter. Effective sample IDs are contiguous even when adaptive filtering
reactivates a pixel. Population counts may increase but never decrease. At
publication, each pixel must have a positive independently reported population,
every identity below that count must be complete, and the total completed
capture count must equal the population total. Thus an absent capture, an extra
capture, cancellation or an unset population cannot silently become a smaller
valid sample set. Fixed-sample finalization remains unchanged.

CUDA reads the same independent film sample counter after camera initialization
has completed on the queue. The adaptive convergence flag remains unchanged
until this capture batch is drained. A converged lane reports a separate skip
marker without reading uninitialized sample/ray fields. An accepted lane reports
its effective sample ID and complete visibility chain. Host finalization applies
the same prefix completeness and total-population checks used by CPU capture.
Log counters distinguish accepted captures from skipped lanes. This changes the
staging record to 532 bytes (272,384 bytes per 512-record host/device buffer).

Population storage costs four bytes per pixel and is included in the deep
working-buffer preflight. The disk record layout and maximum-capacity spill
reservation are unchanged: adaptive sampling currently reduces captured work,
not reserved disk capacity. This is not a total-process memory limit.

## Validation — 2026-09-17

Build/install and all five CTest groups pass. New capture cases cover memory
and spill modes, accepted misses, missing population reports, missing IDs,
extra IDs, reactivation and decreasing populations. Fixed-sample CPU acceptance
passes all seven scenes and its remaining rejection checks; fixed CUDA stack
and opaque comparisons also pass. These are the initial CPU sub-gate results;
the CUDA extension is recorded separately below.

`validate_adaptive_gaffer.py` tests eight adaptive scenes at 32x24 with a
128-sample maximum: transparent stack, miss, variable-population edge, opaque,
CPU OSL stack and three seeds of an equal-color depth discontinuity. It compares
each exported curve with the raw accepted-sample ledger on both sides of all
depth boundaries, and compares deep-on/off beauty pixels exactly.

- Edge populations range from 16 to 128 samples, totaling 24,128 accepted
  samples instead of the fixed maximum of 98,304.
- Maximum curve error: `2.60770320892334e-8` (gate `1e-6`).
- Beauty pixels are identical in every adaptive case.
- Same-colored near/far geometry stops at 16 samples despite depth uncertainty.
  Three seeds differ from a fixed 512-sample reference at a depth-5 cut by up
  to 0.0625, 0.0625 and 0.060546875 transmittance. The finite reference is not
  exact ground truth. These are measured sampling differences, not export
  reconstruction errors. Beauty convergence does not guarantee deep convergence.

Artifacts: `build-m6/m7-adaptive/report.json`, raw CSVs, beauty/deep EXRs and
`m7_adaptive_review.gfr`. The graph compares the fixed reference with three
adaptive seeds through live DeepToPointCloud and editable depth cuts. It is a
small diagnostic fixture, not a production-asset qualification.

```powershell
cmake --build build-m6 --target install --config Release --parallel 2
ctest --test-dir build-m6 -C Release --output-on-failure
build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_adaptive_gaffer.py install-m6/cycles.exe build-m6/m7-adaptive
```

## CUDA qualification — 2026-09-18

The updated runtime CUDA kernel compiles successfully with CUDA 12.8.93 for
sm_86. The final host build/install and all five CTest groups pass. Fixed-sample
CUDA opaque/transparent regression checks and existing rejection checks pass.

Seven native adaptive fixtures pass against the independently captured CPU
results. Every pixel has the same accepted population and every accepted
identity has the same chain length. Maximum CPU/CUDA depth difference is
`2.86102294921875e-6`; local opacity is identical. Maximum exported curve error
is `2.60770320892334e-8`. Deep-on/off beauty pixels are identical in these runs;
the GPU test permits `2e-6` accumulation noise rather than promising bitwise
determinism in general.

The edge fixture exercises both outcomes: 24,128 accepted camera samples and
74,176 skipped lanes. The accepted log count equals the complete CSV population;
skipped lanes never appear as camera misses. Pixel populations range from 16
to 128 across multiple capture batches. Overflow during adaptive traversal and
unsupported CUDA OSL both preserve a pre-existing final deep file.

Three repeated-seed depth-cut comparisons reproduce the CPU uncertainty against
the finite 512-sample reference. These fixtures demonstrate correct accounting
of beauty's chosen population, not general deep convergence or guaranteed
CPU/GPU equality of adaptive stopping on arbitrary noise thresholds.

`build-m6/m7-adaptive-cuda/m7_adaptive_review.gfr` contains CUDA reference/seed
deep readers, live point clouds and depth cuts, plus matching CPU point clouds.
The saved graph is reloaded and a point cloud is evaluated by the test. EXRs,
raw ledgers, logs and `report.json` are in the same directory.

```powershell
build-m6/run-cuda.cmd build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_adaptive_gaffer.py install-m6/cycles.exe build-m6/m7-adaptive-cuda CUDA build-m6/m7-adaptive
```

Next: implement and validate motion blur; the DOF sub-gate now passes separately. M6's open
isolated performance measurements remain tracked in `CUDA_VALIDATION.md`.
