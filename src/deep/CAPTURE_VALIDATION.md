# Opaque capture validation

Historical M3 baseline; current support and memory limits are defined by
[release status](../../DEEP_IMPLEMENTATION_STATUS.md), not this checkpoint.

## Invariants

- Capture accepted camera samples without changing beauty state or RNG.
- Use positive camera-space Z, including the near-clip origin. Misses use
  `PRIM_NONE` and remain in the sample denominator.
- Require unique, complete sample identities; preserve FLOAT depth differences.
- Reconstruct conditional alpha: half coverage at Z=2 and half at Z=8 produces
  alpha 0.5 then 1. Match beauty's file orientation.
- Reject invalid state, incomplete samples, cancellation and overflow explicitly.

## Evidence

`validate_capture_gaffer.py` checks seven small scenes: off-axis plane, edge,
near/far coverage, tiny object, misses, translated camera/near clip and diffuse
shading. It compares deep on/off beauty, independent raw-hit counts, four Gaffer
cuts, worker-count determinism and fourteen invalid-setting/path/write cases.

The original 32x24, 32-sample CPU run passed: beauty exact, maximum depth error
9.54e-7, curve error 2.80e-8 and flattened-alpha error 5.97e-8. This is historical
opaque-surface evidence, not production-scale or arbitrary-material qualification.
Earlier smoke files outside the acceptance directory are not valid evidence.

Run the validator with the current installed Cycles executable and a fresh
output directory. It writes reports, EXRs, diagnostics and a Gaffer review.
Build and test commands: [build setup](../../BUILDING.md).
