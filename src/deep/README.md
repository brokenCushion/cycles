# Cycles deep alpha

Produces **Z/ZBack/A camera visibility** with separate native beauty.
Current scope and qualification follow the [optimization plan](../../DEEP_OPTIMIZATION_PLAN.md)
and [current status](../../DEEP_IMPLEMENTATION_STATUS.md). Deep RGB remains deferred.

- [Support matrix](RELEASE_MATRIX.md), [Blender usage](../../BLENDER_DEEP_INTEGRATION.md)
  and [Gaffer node](gaffer/README.md).
- Historical milestone reports are archived at commit `9cad1e861`.

## Code map

Shared capture kernels: `src/kernel/deep/`; host reconstruction, spill and EXR:
`src/deep/`; scheduling: `src/integrator/path_trace*`; preflight: `src/session/deep.cpp`.
Blender and Gaffer are optional host integrations.

## Regression command

Use Python with NumPy, psutil and OpenEXR 3.x. On this machine:

```powershell
& builds/build-m6/run-cuda.cmd D:/CyclesDeepScratch/regression-tools/Scripts/python.exe tools/run_deep_regression.py
```

`tools/deep_regression_config.json` records the accepted build, source baseline,
fixture/golden paths, build registry and CUDA resource baseline. Supply another
file with `--config` when qualifying another build; register its provenance with
`tools/record_beauty_build.py` first. A stale executable or changed beauty source
fails explicitly. GPU-side changes require the separate `--cuda-beauty FILE`
stage (JSON `targets`: run-relative case, references, pool, seed_references,
snapshot_off/on/build). It applies the unchanged policy in the plan.

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

## Core footprint for rebases

All 53 Cycles source/build files outside `src/deep/` and `src/kernel/deep/`, relative to
local `origin/main` (`a456b7610`). Shared deep headers remain in `src/kernel/deep/`.

| Files | Reason |
| --- | --- |
| `CMakeLists.txt`<br>`src/CMakeLists.txt`<br>`src/app/CMakeLists.txt`<br>`src/integrator/CMakeLists.txt`<br>`src/kernel/CMakeLists.txt`<br>`src/kernel/device/cuda/CMakeLists.txt`<br>`src/session/CMakeLists.txt` | Guarded build flags, source lists, CUDA deep entry point and nine CTests. |
| `src/app/cycles_standalone.cpp`<br>`src/app/cycles_xml.cpp` | CLI/preflight configuration and standalone fixture scene inputs. |
| `src/app/deep_output.cpp`<br>`src/app/deep_output.h`<br>`src/app/oiio_output_driver.cpp`<br>`src/app/oiio_output_driver.h` | Output-driver deep callback, diagnostic ledger and atomic EXR publication. |
| `src/app/deep_output_driver_test.cpp`<br>`src/app/deep_output_driver_test.xml`<br>`src/app/deep_output_driver_transparent_test.xml`<br>`src/app/deep_output_driver_volume_test.xml` | Host API/lifecycle/overflow fixtures, independent of Blender. |
| `src/device/cpu/kernel.cpp`<br>`src/device/cpu/kernel.h`<br>`src/device/cuda/device_impl.cpp`<br>`src/device/cuda/kernel.cpp`<br>`src/device/cuda/queue.cpp`<br>`src/device/cuda/queue.h`<br>`src/device/kernel.cpp`<br>`src/device/queue.cpp`<br>`src/device/queue.h` | CPU/CUDA entry dispatch, queue arguments, async transfers and kernel naming. |
| `src/integrator/path_trace.cpp`<br>`src/integrator/path_trace.h`<br>`src/integrator/path_trace_deep_tile.h`<br>`src/integrator/path_trace_work.h`<br>`src/integrator/path_trace_work_cpu.cpp`<br>`src/integrator/path_trace_work_cpu.h`<br>`src/integrator/path_trace_work_gpu.cpp`<br>`src/integrator/path_trace_work_gpu.h` | Separate capture scheduling, bounded GPU batches/host spill and output-driver tile adapter. |
| `src/kernel/bvh/bvh.h`<br>`src/kernel/bvh/intersect_filter.h`<br>`src/kernel/bvh/volume_all.h`<br>`src/kernel/device/cpu/bvh.h` | Bounded deep intersection batches using existing BVH; default beauty traversal unchanged. |
| `src/kernel/device/cpu/kernel.h`<br>`src/kernel/device/cpu/kernel_arch.h`<br>`src/kernel/device/cpu/kernel_arch_impl.h`<br>`src/kernel/device/gpu/kernel.h` | Guarded CPU architecture dispatch and GPU deep kernel entry. |
| `src/kernel/integrator/surface_shader.h` | Template switch disables closure storage for deep opacity evaluation; beauty uses the original default. |
| `src/kernel/types.h` | KernelShader padding carries extinction constants without increasing its size; deep DeviceKernel enum. |
| `src/kernel/util/nanovdb.h` | Optional uniform-tile dimension accessor for deep grid traversal; ordinary reads preserve defaults. |
| `src/scene/object.cpp`<br>`src/scene/shader.cpp`<br>`src/scene/shader.h` | Object primitive ranges and preflight extinction constants uploaded for separate capture. |
| `src/session/deep.cpp`<br>`src/session/deep.h`<br>`src/session/output_driver.h`<br>`src/session/session.cpp`<br>`src/session/session.h` | Public settings/preflight, camera/data contract and deep output-driver callback lifecycle. |

Blender-specific properties, synchronization, sample-count pass and output-driver
plumbing live in `tools/prepare_blender_deep.py`, outside the standalone renderer.
