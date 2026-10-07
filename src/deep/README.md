# Cycles deep alpha

Produces **Z/ZBack/A camera visibility** with separate native beauty.
Current scope and qualification follow the [optimization plan](../../DEEP_OPTIMIZATION_PLAN.md)
and [current status](../../DEEP_IMPLEMENTATION_STATUS.md). Deep RGB remains deferred.

- [Release status](../../DEEP_IMPLEMENTATION_STATUS.md): qualified capabilities and M8 sign-off.
- [Milestones](../../DEEP_MILESTONES.md): M8 acceptance gates and M9 scope.
- [Measured evidence](../../DEEP_PERFORMANCE_AND_VDB.md): results and artifact locations.
- [Final M8 qualification](M8_RELEASE_VALIDATION.md): release matrix, regressions and review.
- [Blender usage](../../BLENDER_DEEP_INTEGRATION.md) and [Gaffer node](gaffer/README.md).
- [Architecture](ARCHITECTURE_REVIEW.md), [EXR format](EXR_VALIDATION.md),
  [storage/publication](PRODUCTION_VALIDATION.md), [native VDB](NATIVE_VDB_PLAN.md).

## Code map

Shared capture kernels: `src/kernel/deep/`.
Scheduling and device storage: `src/integrator/path_trace*`.
Configuration/preflight: `src/session/deep.cpp`.
Host interface: `src/session/output_driver.h`.
This directory contains reconstruction, spill storage, EXR writing and tests.
Blender and Gaffer are not core runtime dependencies.

## Build and test

The [M8 support matrix](RELEASE_MATRIX.md) distinguishes surface-only adaptive,
DOF and rigid-motion capture from static, fixed-sample volume capture. Run fresh
installed-build qualification with `tools/qualify_deep_release.ps1`; native
Blender VDB pairs and rejection fixtures use `tools/qualify_native_vdb.py`.

The dependency-free reference can build by itself:

```powershell
cmake -S src/deep -B builds/build-deep -G 'Visual Studio 17 2022' -A x64
cmake --build builds/build-deep --config Release
ctest --test-dir builds/build-deep -C Release --output-on-failure
```

For the renderer, enable `WITH_CYCLES_DEEP_OPAQUE`, `WITH_CYCLES_DEEP_TESTS`
and `BUILD_TESTING` in a normal Cycles build. The legacy OPAQUE option name also
covers supported transparency/volume capture. The optional EXR test target uses
OpenEXR. Current configured Windows build:

```powershell
cmake --build builds/build-m6 --target install --config Release --parallel 2
ctest --test-dir builds/build-m6 -C Release --output-on-failure
```

Keep generated builds, renders and reports under `builds/`.

## Numerical contract

Each pixel ledger contains uniquely identified, completed camera samples with
nonnegative weights. Completed misses contribute to normalization. Incomplete
samples, duplicate IDs, invalid events and zero total weight fail explicitly.
Depth is positive axial camera distance in scene units.

`DeepSettings::samples`, `--deep-samples` and Blender `deep_samples` select the
first N accepted camera samples, including misses; 0 uses all beauty samples.
Beauty sampling is unchanged. Positive N publishes its effective maximum in
`cycles:deepSamples`; 0 omits the attribute for legacy identity. Curve error
bounds apply to the retained prefix; differences from all samples are sampling variation.

`DeepSettings::ids`, `--deep-ids` and Blender `use_deep_ids` enable the optional
UINT `id` channel. `cycles:deepIDManifest` maps raw MurmurHash3 seed-0 hashes to
object names. Reconstruction preserves object tags through camera averaging;
different objects retain overlapping intervals. IDs-off follows the original path.
Strict + IDs is rejected at preflight; IDs require a numeric error setting.
Use `tools/compare_deep_ids.py` for exact UINT selection before a FLOAT reader.

For each camera sample, surface events multiply transmittance by `(1 - alpha)`;
volumes contribute `exp(-optical_depth)`. Reconstruction averages transmittance,
then emits local alpha as `1 - T_after / T_before`. It does not average local
alphas or accept incomplete batches. Optical depth includes physical ray length.

Surface depths remain distinct until checked FLOAT export. Volumes use fitted
exponential intervals with bounded whole-curve error. See `volume.h` for numerical
allowances and `reconstruction.h` for caller responsibilities. Tests use independent
raw-event/physical integration oracles; assertions remain active in Release builds.

Scalar deep alpha cannot preserve subpixel correlations between independently
rendered elements. Two half-covered elements may combine to 0.5 or 1 depending
on overlap, while scalar alpha-over yields 0.75. Lossless arbitrary deep merges,
recovery of deep colour from flat beauty, and arbitrary dynamic-range precision
are not promised.
