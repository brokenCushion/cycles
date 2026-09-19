# Native Blender scene deep-render integration

## Current result

**Live review update (2026-09-20):** the optimized scene and native VDB point
clouds are visually verified in Gaffer. Recovered graph edits remain intact;
a separate validated scene review was opened without overwriting them. Earlier
desktop-access limitations below are resolved.

**Native VDB continuation:** the supplied `firePlume_0000.vdb` now produces native
scalar-absorption deep output on CPU and CUDA at 256x256, one sample. Results are
in `builds/validation/native-vdb/final-cpu/` and `final-cuda/`. Each beauty is
pixel-identical to its device's deep-disabled reference. Both Gaffer review
graphs pass reader, depth-cut and million-point reload checks. Live presentation
of these new results still needs working desktop window access. This is density
visibility, not emission/scattering deep RGB. See [qualification](src/deep/NATIVE_VDB_PLAN.md).

**Performance follow-up:** compact spill storage repeats the unchanged full
scene in 247.633 seconds versus 1862.293 seconds for the reference (7.52x in
this single-run comparison). The entire deep EXR is byte-identical and every
beauty RGBA pixel is unchanged. Results: `scene-compact-deep/` under the same
validation root; machine-readable evidence: `compact-performance-comparison.json`.
This includes render/capture/reconstruction/publication, not just kernel time.
The optimized Gaffer review passed headless depth cuts and million-point reload
validation and is open in a separate window. Live viewport verification awaits
desktop access (capture black; activation reports Access is denied). The original
live review and the user's unsaved edits are preserved.

The supplied `test-assets/blender/blender-3.5-splash.blend` rendered successfully
through custom Blender/Cycles at **664x625, 128 samples**, with its evaluated
geometry, original materials, orthographic camera, DOF, adaptive sampling and
denoising. The original file was not saved or changed.

Full results: `builds/validation/blender-deep/scene-full-deep/`:

- `scene.deep.exr`: 244,457,944 bytes, native scalar deep visibility Z/ZBack/A.
- `beauty.exr`: native linear beauty, exactly equal in every RGBA pixel to the
  paired deep-disabled full-resolution render.
- `beauty.png`: preview using the supplied Blender scene's view settings.
- `blender_deep_review.gfr`: live deep reader, depth cuts, matched orthographic
  camera and DeepToPointCloud; one million distributed display samples.
- `render.json`, `scene.deep.exr.samples.csv`, `gaffer_validation.json`: evidence.

Gaffer validated 81 raw diagnostic pixels and 21,076 depth-boundary checks.
Maximum checked transmittance error is 3.100437212522067e-9; checked DeepSlice
alpha error is zero. The saved graph reloads with a valid million-point cloud
and restores SceneDeepPoints as its Viewer focus. This is EXR-derived geometry,
not a mesh-to-point conversion.

**Visual review complete.** After the user unlocked the desktop, the live
full-resolution Gaffer window was inspected successfully. The viewport displayed
the blue deep-derived bakery point cloud, including structure, pumpkin, plants,
furniture and ground. SceneDeepPoints feeds the user's added Transform; the
current view and unsaved edits were preserved. Earlier desktop-access failures
are resolved. The render-and-Gaffer-presentation objective is complete.

## Scope and fidelity

The objective is the original Blender scene rendered through modified Cycles,
then presented in Gaffer. No XML export, replacement material or proxy geometry
was used. The helper disables compositor/sequencer output for direct renderer
beauty comparison. Source SHA256:
`1c41c5a8bb47878a1be5becb7ea72c8dfadf11a95fc34771df2a9eb357537ef4`.

The existing deep contract is scalar camera visibility, with ordinary beauty
alongside. Deep RGB and refracted-light-path reconstruction are not implemented.
Glass keeps native Cycles shading and camera-alpha semantics. DOF point positions
are camera-depth projections, not exact original lens-ray world-space hits.

Scene: 1,077 source objects, 175 materials, active camera CAM-wide (ORTHO,
scale 3.9542861), frame 1. Saved resolution is 664x625 at 50%; the full test
explicitly uses 100%. Native Blender evaluates subdivision/solidify/mirror/bevel,
metaballs, curve geometry and instances before Cycles consumes them.

## Build and host integration

- Standalone baseline: `a456b761034dda42c32eef9f4aae0fa5a5c9f604`.
- Recorded synchronization pair: Cycles `97dbe6f57cdf4ede2d2b75ebdda507c8712edb7a`,
  Blender `749518deb2f0735a22361a07488b35f7ea5c2fdf` (5.3 alpha).
- Exact Windows libraries: `60d6e96b917568278d400a4024c98da0fb777338`.
- Source/build/install: `builds/blender/{source,build,install}`.
- Build preset: `tools/blender_deep_build.cmake`; adapter recipe:
  `tools/prepare_blender_deep.py`. The recipe requires a clean pinned checkout;
  **do not rerun it on the already overlaid source**.
- The overlay retains Blender's own dependency discovery and WITH_PUGIXML option.
- Blender RNA settings expose use_deep_output, deep_output_path, deep_max_events
  and deep_memory_mb. Defaults are off, empty path, 16 events and 512 MiB.
- Native Blender output driver writes atomic deep EXR plus an independently
  published diagnostic CSV. They are not an atomic file pair. One enabled view
  layer, mono background rendering and no automatic tiling are enforced.
- Final executable build/install passed (`deep-build-7.log`, `deep-install-7.log`).
  The full render used build 6; its binary hash is recorded in
  `builds/validation/blender-deep/renderer-provenance.json`. Build 7 only adds the
  final session-reset validation correction. A new actual-scene preview on build
  7 passed Gaffer validation and is byte-identical in deep EXR to the prior preview.

```powershell
cmake -S builds/blender/source -B builds/blender/build -G 'Visual Studio 17 2022' -A x64 -C tools/blender_deep_build.cmake -DLIBDIR=C:/Users/jun/Documents/ChatGPT/cycles_deep/lib/windows_x64 -DCMAKE_INSTALL_PREFIX=C:/Users/jun/Documents/ChatGPT/cycles_deep/builds/blender/install -DWITH_CYCLES_DEEP_OPAQUE=ON
cmake --build builds/blender/build --target INSTALL --config Release --parallel 4 -- /verbosity:minimal /p:CL_MPCount=2
```

## Qualified compatibility changes

- Principled, Glass, Translucent and existing surface closures; scene texture,
  mapping, gradient/noise, ramps, invert, colour mix and bump nodes.
- Numeric automatic socket conversions; default normal links introduced during
  either graph simplification or finalization. Both lifecycle states remain
  accepted on session reset.
- Nonnegative Box, Gaussian and Blackman-Harris camera filters.
- Orthographic surface capture, including DOF and nonzero near clipping.
- Repeated static camera motion slots emitted by Blender with blur disabled.
- Analytic lights, including the scene's 13 lights carrying shadow-catcher flags.
  Surface shadow catchers, holdouts and caustics flags remain rejected.
- Native-resolution CPU denoising. CUDA+denoising remains explicitly rejected;
  GPU accumulation variation was amplified beyond the existing beauty bound.

## Validation and limitations

Evidence under `builds/validation/blender-deep/`:

- `socket-cpu/report.json`: 24 CPU fixtures, SVM and OSL, exact beauty comparisons.
- `socket-cuda/report.json`: 23 CUDA fixtures plus five rejection cases; existing
  floating accumulation bound, with deep curve tolerance 1e-6.
- `orthographic-opaque-regression/report.json`: opaque regression passed.
- `final-core-ctest.log`: all eight CTest groups passed, including host reset,
  cancellation and callback-failure recovery.
- `scene-full-beauty-comparison.json`: all RGBA pixels identical; no nonfinite values.
- `final-build-preview/gaffer_validation.json`: final installed Blender tested.
- `completion-audit.json`: file hashes, source identity, validation and the
  completed live visual-review evidence.

Full deep render took **1862.29 seconds (~31 minutes)**; beauty-only took 43.62
seconds. The render used the configured 512 MiB deep working-memory budget,
which excludes ordinary renderer/scene memory. Per-sample spill I/O is a major
performance limitation exposed by this test; this result is not a production
performance qualification. Capture reserves up to 128 samples and 16 events
per sample; overflow fails explicitly. Do not silently lower samples or replace
scene features to make the test appear faster.

## Reproduce and review

```powershell
& builds/blender/install/blender.exe --factory-startup --background --disable-autoexec test-assets/blender/blender-3.5-splash.blend --python-exit-code 1 --python tools/render_blender_deep_scene.py -- --output builds/validation/blender-deep/scene-full-deep --samples 128 --percentage 100 --deep
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_blender_deep_gaffer.py builds/validation/blender-deep/scene-full-deep
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd builds/validation/blender-deep/scene-full-deep/blender_deep_review.gfr
```

In the graph, view SceneDeepPoints for the EXR point cloud, BlenderBeauty for
native beauty, or FullDeepAlpha for flattened coverage. Enable SceneDepthCut's
farClip and adjust its value; CloudDepthCut follows it and preserves deep samples.
The million-point limit is a display selection, not a reduction of the saved EXR.
