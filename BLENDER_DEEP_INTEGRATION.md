# Blender deep output

Custom Blender uses the same Cycles deep core as the standalone renderer.
**M8 is complete; final user review passed on 2026-09-29.** See [release gates](DEEP_MILESTONES.md) and
[measurements](DEEP_PERFORMANCE_AND_VDB.md) for current qualification.

## Build

Pinned Blender: `749518deb2f0735a22361a07488b35f7ea5c2fdf` (5.3 alpha).
Cycles synchronization point: `97dbe6f57cdf4ede2d2b75ebdda507c8712edb7a`.
Windows libraries: `60d6e96b917568278d400a4024c98da0fb777338`.
Source, build and install live under `builds/blender/`.

Use `tools/prepare_blender_deep.py` on a clean pinned Blender checkout. It
applies the core and host adapter; do not rerun it over an existing dirty overlay.

```powershell
cmake -S builds/blender/source -B builds/blender/build -G 'Visual Studio 17 2022' -A x64 -C tools/blender_deep_build.cmake -DLIBDIR=C:/Users/jun/Documents/ChatGPT/cycles_deep/lib/windows_x64 -DCMAKE_INSTALL_PREFIX=C:/Users/jun/Documents/ChatGPT/cycles_deep/builds/blender/install -DWITH_CYCLES_DEEP_OPAQUE=ON
cmake --build builds/blender/build --target INSTALL --config Release --parallel 2 -- /verbosity:quiet /p:CL_MPCount=2
```

Close test Blender processes before installing. The system Blender installation
is separate and does not include this feature.

## Render and review

```powershell
& builds/blender/install/blender.exe --factory-startup --background --disable-autoexec test-assets/blender/blender-3.5-splash.blend --python-exit-code 1 --python tools/render_blender_deep_scene.py -- --output builds/validation/blender-deep/scene-full-deep --samples 128 --percentage 100 --deep
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_blender_deep_gaffer.py builds/validation/blender-deep/scene-full-deep
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd builds/validation/blender-deep/scene-full-deep/blender_deep_review.gfr
```

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
  `deep_max_events`, `deep_memory_mb`. Deep is off by default.
- Output: scalar Z/ZBack/A and separate native beauty. No deep RGB or refracted
  light-path reconstruction. Glass uses native camera-alpha semantics.
- One enabled view layer, mono background render, no automatic tiling.
- CPU denoising is qualified; CUDA plus denoising is explicitly rejected.
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
