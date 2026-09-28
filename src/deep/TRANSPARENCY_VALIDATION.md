# Scalar transparency validation

Historical M4 CPU SVM/OSL evidence. Current support and resource limits:
[release status](../../DEEP_IMPLEMENTATION_STATUS.md).

## Semantics

A private copy of each accepted camera state traces straight visibility using
native surface shader evaluation. It preserves sample/ray/time/lens identity
and does not write beauty buffers. Beauty bounce limits and stochastic closure
selection do not truncate the deep chain.

Local alpha is `1 - T.x` only for finite, equal RGB transparency components in
[0,1]. Coloured extinction fails; it is not converted to luminance. The native
shader's 1e-5 closure cutoff remains part of the model.

A miss or opaque surface completes a chain. Every hit, including a clear
surface, consumes capacity. Exact capacity succeeds; overflow, invalid depth,
unsupported primitives and unresolved cache misses fail without replacing an
existing output. Empty samples remain in the estimator.

## Evidence

`validate_transparency_gaffer.py` checks nine scenes at 16x12 / 16 samples:
analytic opacity stacks, opaque backdrop, misses, clear/cutoff surfaces,
procedural cutouts and grayscale image textures. It compares native CPU SVM and
OSL, raw-event transmittance, both sides of depth boundaries, Gaffer flattening,
beauty on/off and one/four-worker determinism.

Original results: zero SVM/OSL local-alpha difference; maximum curve error
2.608e-8 and analytic depth error 1.908e-6. Beauty and worker-repeat records were
identical. Eight rejection cases preserved prior output; exact capacity passed.
Historical artifacts: `builds/build-m3/m4-acceptance`.

This establishes the allowlisted fixture contract, not arbitrary OSL shaders,
refraction, coloured transmission or production-scale qualification. Current
storage, sampling, device and volume gates are documented separately.

Run the validator with the current installed executable and a fresh output
directory. Build commands: [BUILDING.md](../../BUILDING.md).
