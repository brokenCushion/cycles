# M4: CPU scalar transparency and OSL parity

M4 adds `--deep-transparent` to the experimental standalone adapter. The existing
`WITH_CYCLES_DEEP_OPAQUE=ON` build switch retains its historical name and gates
both M3 and M4. Without the new runtime switch, the M3 opaque contract applies.

## Capture semantics

The CPU work owner copies each accepted camera state before beauty traversal.
A separate straight visibility traversal preserves its raster sample, ray,
differentials, time, lens and random identity. It intersects camera-visible
static triangles and calls the ordinary `surface_shader_eval`, selecting SVM or
OSL through the existing kernel feature flag. It writes no beauty buffers.
Transparent bounce context and RNG offset advance in the private state. Beauty
closure selection, throughput, Russian roulette and bounce limits do not decide
whether the deep chain is complete.

Both backends call `bsdf_transparent_setup`. The adapter reads
`surface_shader_transparency` after evaluation: local alpha is `1 - T.x` only
when all three components of T are exactly equal, finite and in [0,1]. Colored
extinction fails export; there is no luminance conversion. The pinned shader's
`CLOSURE_WEIGHT_CUTOFF` is 1e-5. Extinction discarded by that existing cutoff is
absent from this model as well; deep does not restore sub-cutoff closures.

A miss or an exactly opaque surface completes a camera chain. Every intersected
surface, including zero-alpha surfaces, consumes one traversal slot. A hit beyond
`--deep-max-events` (default 16, range 1..64), unsupported primitive, invalid
opacity/depth or unresolved texture cache miss fails the whole export. A chain
ending exactly at capacity succeeds. No partial chain is published. Existing
output is preserved on these capture failures because reconstruction/writing
starts only after successful finalization.

The experimental storage reserves `width * height * samples * (1 + 2 * limit)`
floats in M4 mode, including completion counts. `--deep-memory-mb` covers this raw
allocation, not reconstruction, shader resources or EXR staging. M3 retains one
float per camera sample. CSV rows add local `alpha` and an `event` index; an empty
chain has depth/alpha zero and event -1. All camera samples remain in the estimator.

## Supported configuration

- Windows x64, MSVC 19.44, Release, CPU with OSL enabled; source baseline
  `a456b761034dda42c32eef9f4aae0fa5a5c9f604` plus the M3/M4 changes.
- Fixed 1..4096 samples, box filter width 1, static perspective pinhole camera,
  full frame, static polygon meshes, constant background.
- Surface graph allowlist: output, emission, diffuse BSDF, transparent BSDF,
  mix closure, checker texture, texture coordinate and image texture.
- These are ordinary Cycles graphs, compiled independently by SVM and OSL.
  Arbitrary custom OSL scripts are outside this gate. No deep-only shader exists.
- Refraction, colored transmission, ray portals, holdout, shadow catchers, volumes,
  shader side effects/AOV writes and other graph nodes are rejected. M3's camera,
  sampling, motion, denoise, subdivision and geometry restrictions remain.

## Reproduce

Build the `cycles` and `cycles_deep_capture_test` targets in the feature-enabled
configuration. Stage the executable beside the installed dependencies as
`install/cycles-m4.exe`, retaining the existing baseline executable.

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd python src/deep/validate_transparency_gaffer.py install/cycles-m4.exe build-m3/m4-acceptance
ctest --test-dir build-m3 -C Release --output-on-failure
```

The suite uses 16x12 images, 16 fixed samples, seed 123, four threads, plus a
one-thread repeat. Gaffer 1.7.2.0 reads the output; our fork renders through its
standalone executable, not Gaffer's bundled Cycles. The deferred 640x480 Gaffer
demo is not part of this milestone.

Acceptance checks:

- Analytic .25/.5-opacity emission and diffuse stacks (final alpha .625), opaque backdrop, misses,
  clear surfaces, and existing shader cutoff behavior.
- Procedural cutouts on transformed geometry with generated coordinates;
  grayscale image texture with mesh UVs and known texel opacity.
- Same raw event sequence and local opacity for SVM and OSL, depth within 2e-5
  scene units, opacity within 1e-6 absolute error.
- Independent raw-event transmittance oracle, including both sides of every
  depth boundary and Gaffer DeepToFlat, within 1e-6 absolute error.
- Bit-identical deep-on/off beauty per backend, independence from beauty's
  transparent bounce limit, and identical raw records with one/four threads.
- Overflow (including clear surfaces), colored extinction and refraction fail
  with nonzero exit and preserve the prior deep output. Exact capacity succeeds.
- Existing M3 opaque acceptance and the four CTest checks remain regression gates.

## Recorded result (2026-09-16)

All nine scenes passed for both CPU backends. The maximum SVM/OSL local-alpha
difference was zero; maximum deep transmittance error was 2.6077032089e-8;
maximum analytic axial-depth error was 1.9073486328e-6 scene units. Beauty was
bit-identical with deep off/on, and one/four-thread event CSVs were identical.
All eight rejection cases preserved the prior output. Exact-capacity traversal
passed for both backends. The four CTest checks and M3 regression suite passed.
Machine-readable results and EXRs are in `build-m3/m4-acceptance/`.
Feature-enabled and feature-disabled Release builds passed; the disabled binary
exposes no deep CLI options. `git diff --check` passed.

This is fixture-based numerical evidence for the stated configuration, not a
statistical confidence claim about arbitrary production materials or backends.
Production storage, reduction and atomic publication are M5; motion/DOF and
adaptive sampling are M7; volumes are M8. GPU OSL needs a separate qualification.
