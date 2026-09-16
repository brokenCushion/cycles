# Deep opacity reference (M1)

This directory contains a dependency-free C++17 reference and tests, plus an
optional Deep EXR writer and CPU opaque sample storage. The opt-in standalone
renderer adapter is described in [CAPTURE_VALIDATION.md](CAPTURE_VALIDATION.md).
See EXR_VALIDATION.md for the writer. Production sample reduction is not implemented.

## Build and inspect

```powershell
cmake -S src/deep -B build-deep -G "Visual Studio 17 2022" -A x64
cmake --build build-deep --config Release
ctest --test-dir build-deep -C Release --output-on-failure
.\build-deep\Release\cycles_deep_reference_test.exe
```

Alternatively enable `WITH_CYCLES_DEEP_TESTS=ON` in a standalone Cycles build.
The option defaults to OFF. Tests require `BUILD_TESTING=ON` (CTest's default).
Enabling `WITH_CYCLES_DEEP_OPAQUE` additionally links CPU capture and reconstruction
to the renderer and enables the standalone `--deep-output` option.

## Contract and derivation

One `PixelLedger` owns camera samples for a single target pixel. Each sample has
a unique identity, nonnegative weight, completion flag, and local surface
opacities. A completed sample with no events is a miss, not an absent sample.
All samples, even zero-weight ones, must be complete and valid. An empty ledger
or all-zero weights is an error because normalization is undefined.

For each sample, start with T_s = 1. At an event multiply by (1 - local alpha).
At each distinct depth, combine all coincident events before calculating the
new weighted sum S = sum(w_s * T_s). Emit A = 1 - S_after / S_before when S drops.
The common denominator W cancels in this ratio. Consequently the product of
output (1 - A) telescopes to S(z) / W, including misses in W.

Input storage order does not matter. Samples are sorted by identity and events
by exact depth, sample identity, and alpha. Nearly equal depths are **not**
merged. Surface output stores depth and effective alpha in double precision;
serialization sets ZBack=Z. Pixel coordinates may be negative for
cropped/offset windows. This module does not establish image-window conventions.

Caller responsibilities:

- Supply positive axial depths in one consistent coordinate system.
- Supply scalar local opacity, not attenuated throughput or closure-selection
  probability; no RGB-to-scalar conversion occurs here.
- Set completion only after resolving the visibility chain. An opaque event
  may complete it because later transmittance is zero. A traversal limit,
  cancellation, or unresolved cache retry is incomplete.
- Assign sample IDs uniquely across batches; invoke reconstruction only when
  the pixel ledger is complete. This is not a batch-merging API.

Inputs outside the contract throw `std::invalid_argument`, including bad events
behind an opaque surface or in zero-weight samples. Input alphas are not clamped.
The implementation only clamps a weighted-sum increase within eight double
epsilons relative to the previous sum; larger increases throw. Finite weights
are scaled by the maximum weight to avoid sum overflow. Contributions below
double precision or underflow range can disappear; no arbitrary-dynamic-range
relative-error guarantee is made.

For E events, S camera samples, and D distinct depths, time is approximately
O(S log S + E log E + D*S), storage O(S+E). The repeated weighted summation favors
readability and stability. This is deliberately not a production accumulator.

## Validation

The test executable checks analytic expectations, invalid values, duplicate IDs,
incomplete chains, zero weights, extreme finite weights, 2,000 low-opacity
boundaries, and 250 deterministic randomized ledgers with shuffled samples and
events. Its independent oracle directly multiplies raw event transmittance at
queried depths, without the implementation's sweep or summation helper.
Comparisons cover both sides of every input boundary and the final curve.
Assertions are explicit runtime checks and remain active in Release builds.

The executable prints PASS/FAIL groups, maximum absolute checked error, and a
small `Z ZBack A` text example. Acceptance is absolute error <= 1e-12 on these
bounded fixtures, not a universal precision guarantee.

An expected-limitation fixture preserves the coverage ambiguity: two separately
reconstructed half-covered elements can have identical scalar outputs whether
they cover the same or opposite halves. Scalar alpha-over gives 0.75; the true
combined coverage can be 0.5 or 1. This model does not promise lossless deep
merges or recovery of per-depth color from flat beauty.
