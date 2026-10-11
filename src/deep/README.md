# Cycles deep alpha

Produces **Z/ZBack/A camera visibility** with separate native beauty.
Current scope and qualification follow the [support matrix](../../docs/deep/SUPPORT.md)
and [validation policy](../../docs/deep/VALIDATION.md). Deep RGB remains deferred.

- [Support matrix](../../docs/deep/SUPPORT.md), [Blender usage](../../docs/deep/BLENDER.md)
  and [Gaffer node](gaffer/README.md).
- [Approved M8 release record](../../docs/deep/M8_RELEASE_VALIDATION.md) and
  [archived historical reports](../../docs/deep/ARCHIVED_REPORTS.md).

## Code map

Shared capture kernels: `src/kernel/deep/`; host reconstruction, spill and EXR:
`src/deep/`; scheduling: `src/integrator/path_trace*`; preflight: `src/session/deep.cpp`.
Blender and Gaffer are optional host integrations.

## Regression command

Use Python with NumPy, psutil and OpenEXR 3.x, from the repository root. Supply
a configuration for the executable and preserved reference set you are qualifying:

```text
python tools/run_deep_regression.py --config <qualified-build.json> --optix --cuda-beauty <beauty-targets.json>
```

`tools/deep_regression_config.json` records historical local build paths, source baseline,
fixture/golden paths, build registry and CUDA resource baseline. Verify executable
paths rather than assuming those historical installs are current. Supply another
file with `--config` for the current build; register its provenance with
`tools/record_beauty_build.py` first. A stale executable or changed beauty source
fails explicitly. GPU-side changes require the separate `--cuda-beauty FILE`
stage (JSON `targets`: run-relative case, references, pool, seed_references,
snapshot_off/on/build). It applies the unchanged policy in the plan.
`--optix` adds native SVM OptiX matrices, boundaries, fixed landscapes and
the accepted 65-case SVM identity replay;
the beauty stage accepts independently matched controls for each GPU backend.
The qualified Phase 8a OptiX SVM implementation uses its own module, pipeline,
shader table and queue arguments.
It shares capture/reconstruction with CUDA; beauty's shaders and launch layout
remain unchanged. Native grids retain double integration; volume bounds use
the exact triangle scan because OptiX has no CUDA BVH2 nodes.

The default checks nine CTests, CPU/CUDA matrices, boundaries, exact CPU beauty,
75 kernel resources, strict 81/81 and numeric 30/30 identity, plus surface,
transparency, adaptive, DOF, motion, overlap/UINT IDs, holdout and small landscape.
The numerical oracle and SDK reader replace overlapping Gaffer numerical runs;
the legacy `validate_*_gaffer.py` tools remain for optional reader/interactive review.
Large output and TEMP/TMP are owned by `D:/CyclesDeepScratch/regression/<run-id>/`.
Passing runs remove their own intermediates; `--keep` preserves them. Failed runs
retain evidence. Only small reports remain in `builds/validation/deep-regression/`.
Cleanup refuses reparse points and never touches golden or historical directories.

Historical `.samples.csv.zip` files stream directly through `sample_csv.py`;
identity/beauty checks still read the retained EXRs. `archive_deep_samples.py`
requires an inventory and `--replace-verified`; it verifies every decompressed
byte and journals the digest before replacing an approved physical CSV.

The standalone reference remains buildable with `cmake -S src/deep -B builds/build-deep`.
Renderer builds enable `WITH_CYCLES_DEEP_OPAQUE`, `WITH_CYCLES_DEEP_TESTS` and
`BUILD_TESTING`. Run `tools/test_deep_regression.py` for storage/reader self-checks.

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

Object holdout flags and Holdout surface closures retain their normal camera
opacity in deep; native beauty keeps its matte colour/alpha. With IDs,
`cycles:deepIDHoldoutManifest` is a hash/name subset of `cycles:deepIDManifest`
for objects with the flag or a reachable Holdout closure. It is omitted when
empty. Shadow catchers and caustics remain rejected.

For each camera sample, surface events multiply transmittance by `(1 - alpha)`;
volumes contribute `exp(-optical_depth)`. Reconstruction averages transmittance,
then emits local alpha as `1 - T_after / T_before`. It does not average local
alphas or accept incomplete batches. Optical depth includes physical ray length.

Numeric same-object surface merging uses `--deep-z-tolerance` (default 1e-4);
zero disables it and strict forces zero. Combined alpha and curves outside merged
depth bands retain their checks; the band's interior is a depth approximation.
Volumes use fitted exponential intervals with bounded error. See `volume.h` for numerical
allowances and `reconstruction.h` for caller responsibilities. Tests use independent
raw-event/physical integration oracles; assertions remain active in Release builds.

Scalar deep alpha cannot preserve subpixel correlations between independently
rendered elements. Two half-covered elements may combine to 0.5 or 1 depending
on overlap, while scalar alpha-over yields 0.75. Lossless arbitrary deep merges,
recovery of deep colour from flat beauty, and arbitrary dynamic-range precision
are not promised.

## Native VDB capture

Native scalar NanoVDB density uses the existing scene/grid transforms and linear
interpolation. Along a ray, trilinear cells integrate as exact cubics. Numeric
device compression has a stated transmittance allowance and never increases the
per-ray record count; cells that cannot merge retain their exact cubic record.
The host fits and publishes Z/ZBack/A with the remaining budget. Early termination
charges the object's allowance; overlapping media are bounded explicitly.
Scattering contributes extinction; deep colour remains deferred.

Preflight: `src/session/deep.cpp`; traversal/compression:
`src/kernel/deep/volume_grid.h`; captured records/spill: `capture.*`;
host fitting: `volume.cpp`; FLOAT/error/publication: `exr_writer.cpp`.
No host library, heap allocation or file access enters GPU kernels. Exhausted
capacity and unsupported shaders/cameras/grids fail explicitly.

The shared regression checks accepted-camera curves, independent geometry/depth
cuts, IDs, CPU beauty equality, strict/numeric identity and kernel/source records.
Independent cubic/grid tests remain separate from Gaffer reader review.

See the [architecture guide](../../docs/deep/ARCHITECTURE.md) for the integration
footprint and rebase notes.

## OSL surface checks

Create fixtures with `tools/create_osl_deep_cases.py` in Blender background mode,
then run `tools/run_osl_deep_checks.py --blender EXE --cases CASES.json --root D:/OUTPUT --report REPORT.json`.
Checks include native-alpha/oracle/depth cuts, CPU exact beauty, OptiX's 20-control
beauty policy, and same-build identity. `--pristine FILE` supplies hash-checked
native Blender evidence for the case-specific cross-backend waiver in the plan;
it never bypasses either backend's own bound.

## Shader-evaluated volumes

OSL volumes select native shader evaluation on CPU/OptiX. SVM requires explicit
`--deep-volume-shader-eval`; qualifying analytic volumes keep their existing path.
Use numeric `--deep-error` above 2e-6. `--deep-volume-step` is a world-unit cap on
the starting/maximum voxel step and is required for volumes without a grid. EXR attributes
`cycles:deepVolumeMethod`, `cycles:deepVolumeStep`, `cycles:deepVolumeStepMin`,
`cycles:deepVolumeStepRule` and `cycles:deepVolumeErrorProof` identify the method.
Half the error is a stated, not proven, adaptive-stepping allowance; half bounds
reconstruction/publication. Arbitrarily fast shader variation is not bounded.

`tools/run_osl_volume_checks.py --blender EXE --cases CASES.json --root D:/OUTPUT --report REPORT.json`
checks each fixture against a 4x-finer step, CPU exact/GPU calibrated beauty,
same-build identity, native analytic cost, and atomic capacity/preflight failures.
`--control-root D:/PREVIOUS` reuses GPU controls only after fixture hashes and the
beauty validator's executable/source, sampling and raw-pass checks agree.
Fixture creation uses `tools/create_osl_volume_cases.py` and the small pinned
OpenVDB asset generator `tools/create_osl_volume_grid.cpp`; no new dependency.

Deep shader data/globals are initialized and use native deterministic grid
filters, with no lookup RNG. Adaptive capture reuses evaluations and fails at
24 refinement levels, 65535 point evaluations or the unchanged event cap.
Features narrower than the finest evaluated step can be missed. Diagnostic
`CYCLES_DEEP_VOLUME_FIXED_STEP=1` selects fixed midpoint capture for comparison;
production defaults to `cycles:deepVolumeMethod=shader-eval-adaptive`.
The independent CPU shader oracle is enabled with `CYCLES_DEEP_VOLUME_ORACLE_DIR`,
`CYCLES_DEEP_VOLUME_ORACLE_STEP` and selected render pixels in
`CYCLES_DEEP_VOLUME_ORACLE_PIXELS` (for example `;8,4;;10,5;`). It samples actual
accepted rays without event records/cap. `tools/check_shader_volume_oracle.py`
independently integrates its CSVs and checks the EXR header bound.
