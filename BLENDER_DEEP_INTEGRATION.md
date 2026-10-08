# Blender deep output

Custom Blender uses the same Cycles deep core as the standalone renderer.
Current work follows the [optimization plan](DEEP_OPTIMIZATION_PLAN.md).
See its results and [current status](DEEP_IMPLEMENTATION_STATUS.md).

## Build

Pinned Blender: `749518deb2f0735a22361a07488b35f7ea5c2fdf` (5.3 alpha).
Cycles synchronization point: `97dbe6f57cdf4ede2d2b75ebdda507c8712edb7a`.
Windows libraries: `60d6e96b917568278d400a4024c98da0fb777338`.
New source, build, install and render outputs live under `D:/CyclesDeepScratch/`.
Use clang-cl, Ninja, CUDA 12.8.0 and OptiX 8.0.0 in an x64 Visual Studio
developer environment. NVCC still uses its supported MSVC host compiler.

Use `tools/prepare_blender_deep.py` on a clean pinned Blender checkout. It
applies the core and host adapter; do not rerun it over an existing dirty overlay.

```powershell
cmake -S D:/CyclesDeepScratch/blender/source -B D:/CyclesDeepScratch/blender/build -G Ninja -C tools/blender_deep_build.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_FLAGS=-m64 -DCMAKE_CXX_FLAGS=-m64 -DLIBDIR=C:/Users/jun/Documents/ChatGPT/cycles_deep/lib/windows_x64 -DCMAKE_INSTALL_PREFIX=D:/CyclesDeepScratch/blender/install -DOPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCYCLES_RUNTIME_OPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCYCLES_CUDA_BINARIES_ARCH=sm_86 -DWITH_CYCLES_DEEP_OPAQUE=ON
cmake --build D:/CyclesDeepScratch/blender/build --target install --parallel 24
```

Close test Blender processes before installing. The system Blender installation
is separate and does not include this feature.

## Render and review

```powershell
& D:/CyclesDeepScratch/blender/install/blender.exe --factory-startup --background --disable-autoexec test-assets/blender/blender-3.5-splash.blend --python-exit-code 1 --python tools/render_blender_deep_scene.py -- --output D:/CyclesDeepScratch/blender/review --samples 4 --percentage 10 --deep
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_blender_deep_gaffer.py D:/CyclesDeepScratch/blender/review
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd D:/CyclesDeepScratch/blender/review/blender_deep_review.gfr
```

`deep_error` defaults to `1e-3`. Set it to `0` in Blender for strict output;
the render helper and standalone renderer use `--deep-error strict`.
Non-strict values must exceed the fixed `1e-6` FLOAT precision floor and be
at most `1e-2`. `cycles:deepError` records the effective FLOAT setting.
Strict preserves the old payload and all deterministic headers, including
`cycles:maxTransmittanceError`; validators read that legacy bound in strict.
Surface `--deep-reduce` and volume coalescing share the same setting/budget.
Numeric modes include bounded per-object device compression; strict retains exact
cubic capture. The plan records the budget split and qualification results.

For a beauty comparison, repeat into a separate output directory without `--deep`.
Use identical device, samples and resolution. The helper preserves scene geometry,
materials and camera, disables compositing/sequencing, and never saves the source.
VDB fixtures use `tools/create_vdb_deep_scene.py`; rendering requires
`--deep --deep-volume`. See the [VDB contract](src/deep/NATIVE_VDB_PLAN.md).

Qualification uses Blender's bundled colour configuration. Gaffer's launcher
sets an ACES `OCIO` environment override; do not pass that override into Blender
when reproducing these native Blender fixtures. The qualification runners remove
it only for renderer children and preserve Gaffer's reader environment.

The review graph contains the deep reader, depth cuts and DeepToPointCloud.
View `SceneDeepPoints`, `BlenderBeauty` or `FullDeepAlpha`. `SceneDepthCut` controls
both image and point-cloud cuts. Max Points limits display only; EXR data is intact.
DOF points are camera-depth projections, not exact lens-ray hit positions.

## Host contract and limits

- Settings: `use_deep_output`, `use_deep_volume`, `deep_output_path`,
  `deep_max_events`, `deep_memory_mb`, `deep_error`, `deep_samples`,
  `use_deep_ids`. Film > Deep Output exposes these controls. Deep and IDs are off
  by default.
- Optional `--deep-ids` / `use_deep_ids` writes per-sample UINT `id` and
  `cycles:deepIDManifest` (hex ID to object name), using Cycles Cryptomatte
  MurmurHash3 seed 0. Different objects retain overlapping samples. Select IDs
  as UINT before Gaffer converts channels to FLOAT; `tools/compare_deep_ids.py`
  provides exact selection and combined-alpha validation. IDs-off retains legacy
  output. Strict + IDs is rejected; IDs require a numeric deep error setting.
  Same-object depth merging and holdouts are separate review stops.
- Output: scalar Z/ZBack/A and separate native beauty. No deep RGB or refracted
  light-path reconstruction. Glass uses native camera-alpha semantics.
- One enabled view layer, mono background render, no automatic tiling.
- CPU denoising is qualified in the M8 release. This compatibility branch also
  admits native CUDA denoising; see the development evidence below.
- Deep EXR publication is atomic. The diagnostic CSV is published independently;
  the two files are not an atomic pair.
- Memory budget covers deep working storage, not ordinary scene/beauty memory.
  Unsupported settings or exceeded capacities fail explicitly.

## Reviewed scene

The supplied bakery scene passed at 664x625, 128 samples, with original materials,
orthographic camera, DOF, adaptive sampling and CPU denoising. Optimized deep output
is byte-identical to its baseline, with exact beauty RGBA. Gaffer review passed;
the user's recovered graph edits were preserved separately.

Current artifacts: `builds/validation/blender-deep/scene-compact-deep/`.
Numerical reports and timing qualifications live in
[measured results](DEEP_PERFORMANCE_AND_VDB.md); historical build narratives remain
in Git history.

## Landscape/cloud compatibility branch

The landscape production work preserves the supplied Blender 3.3 splash scene's shader graphs, HDRI,
HALF volume precision and adaptive/denoising settings. Its separate path-repaired
copy and renders live under `builds/validation/landscape-cloud/`. The original
assets are unchanged. See [compatibility evidence and remaining work](src/deep/LANDSCAPE_COMPATIBILITY.md).

Native-grid capture defaults to 4096 events per camera sample. On this branch,
Deep Events Per Sample can explicitly request up to 8192. GPU capture tries
4096 lanes with smaller event buffers and retries capacity failures within the
same 32 MiB staging reservation. Surface/homogeneous capture retains its
64-event limit. CUDA scattering fixtures pass with native denoising, including
adaptive populations and noisy-pass beauty checks. The original full resolution
and 1024-sample maximum still require the production measurements and review
documented in the compatibility evidence.

The compatibility exporter uses existing TBB workers for independent pixels,
with worker count bounded by the deep-memory preflight. The serial row allowance
is preserved; OpenEXR writes one completed row after all its workers join.
`tools/render_blender_deep_scene.py --threads 24` requests CPU concurrency;
the corrected maximum-1024-sample landscape configuration selects four export
workers at 1024 MiB, accounting for worst-case retained tree levels. The corrected
source also accepts larger budgets: 8192 MiB permits 24 workers for these settings
in the capture budget test. The higher-budget adapter is installed separately in
`builds/blender/install-landscape-hardware-budget`. Its repeated 47x20/max16
benchmark measured export medians of 12.071, 8.087 and 7.977 seconds with 4, 12
and 24 workers respectively, at 8192 MiB. This selects 24 workers for the pending
full-scene test; it does not establish full-scene speedup. The earlier
47x20/16-sample landscape benchmark exports in 10.41 seconds versus 42.89 seconds
serial, with byte-identical output. This small benchmark does not qualify full
scene performance. The full 1175x500/maximum-1024 CUDA production retry and its
validation retry is under `original-resolution-CUDA/CUDA/deep-ledger-bounded-AgX`.
The four-worker full retry published its EXR but took 165,570 seconds to export;
it failed the observed GPU-memory and raw-beauty gates. Full-scene production
qualification remains open; small-fixture speedup does not establish readiness.
The earlier ten-worker full run failed its per-pixel memory gate; it did not
publish an EXR or qualify the scene. See the compatibility evidence for details.
