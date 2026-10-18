# Blender deep output

Custom Blender uses the same Cycles deep core as the standalone renderer.
See the [supported scope](SUPPORT.md), [validation policy](VALIDATION.md)
and accepted [production results](evidence/README.md).
Run the command examples below from the repository root.

## Build

Pinned Blender: `749518deb2f0735a22361a07488b35f7ea5c2fdf` (5.3 alpha).
Cycles synchronization point: `97dbe6f57cdf4ede2d2b75ebdda507c8712edb7a`.
Windows libraries: `60d6e96b917568278d400a4024c98da0fb777338`.
New source, build, install and render outputs live under `D:/temp/cycles_deep_blender_ui/`.
Use clang-cl, Ninja, CUDA 12.8.0 and OptiX 8.0.0 in an x64 Visual Studio
developer environment. NVCC still uses its supported MSVC host compiler.
The existing toolchain under `D:/CyclesDeepScratch/` is read-only. Redirect
`TEMP`, `TMP`, `CUDA_CACHE_PATH` and `OPTIX_CACHE_PATH` under the new output root.

Use `tools/prepare_blender_deep.py` on a clean pinned Blender checkout. It
applies the core and host adapter; do not rerun it over an existing dirty overlay.

```powershell
cmake -S D:/temp/cycles_deep_blender_ui/blender-clean -B D:/temp/cycles_deep_blender_ui/build -G Ninja -C tools/blender_deep_build.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_FLAGS=-m64 -DCMAKE_CXX_FLAGS=-m64 -DLIBDIR=C:/Users/jun/Documents/ChatGPT/cycles_deep/lib/windows_x64 -DCMAKE_INSTALL_PREFIX=D:/temp/cycles_deep_blender_ui/install -DOPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCYCLES_RUNTIME_OPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCYCLES_CUDA_BINARIES_ARCH=sm_86 -DWITH_CYCLES_DEEP_OPAQUE=ON
cmake --build D:/temp/cycles_deep_blender_ui/build --target install --parallel 24
```

Close test Blender processes before installing. The system Blender installation
is separate and does not include this feature.

## Render and review

This legacy surface validator expects strict output (`Z == ZBack`).

```powershell
& D:/temp/cycles_deep_blender_ui/install/blender.exe --factory-startup --background --disable-autoexec test-assets/blender/blender-3.5-splash.blend --python-exit-code 1 --python tools/render_blender_deep_scene.py -- --output D:/temp/cycles_deep_blender_ui/review --samples 4 --percentage 10 --deep --deep-error strict
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_blender_deep_gaffer.py D:/temp/cycles_deep_blender_ui/review
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd D:/temp/cycles_deep_blender_ui/review/blender_deep_review.gfr
```

`deep_error` defaults to `1e-3`. Set it to `0` in Blender for strict output;
the render helper and standalone renderer use `--deep-error strict`.
Non-strict values must exceed the fixed `1e-6` FLOAT precision floor and be
at most `1e-2`. `cycles:deepError` records the effective FLOAT setting.
Strict preserves the old payload and all deterministic headers, including
`cycles:maxTransmittanceError`; validators read that legacy bound in strict.
Surface `--deep-reduce` and volume coalescing share the same setting/budget.
Numeric modes include bounded per-object device compression; strict retains exact
cubic capture. The validation policy records the budget split.

For a beauty comparison, repeat into a separate output directory without `--deep`.
Use identical device, samples and resolution. The helper preserves scene geometry,
materials and camera, disables compositing/sequencing, and never saves the source.
VDB fixtures use `tools/create_vdb_deep_scene.py`; rendering requires
`--deep --deep-volume`. See the [VDB contract](../../src/deep/README.md#native-vdb-capture).

Qualification uses Blender's bundled colour configuration. Gaffer's launcher
sets an ACES `OCIO` environment override; do not pass that override into Blender
when reproducing these native Blender fixtures. The qualification runners remove
it only for renderer children and preserve Gaffer's reader environment.

The review graph contains the deep reader, depth cuts and DeepToPointCloud.
View `SceneDeepPoints`, `BlenderBeauty` or `FullDeepAlpha`. `SceneDepthCut` controls
both image and point-cloud cuts. Max Points limits display only; EXR data is intact.
DOF points are camera-depth projections, not exact lens-ray hit positions.

## Host contract and limits

- Geometry preflight follows camera visibility; keep geometry outside the qualified scope indirect-only in deep layers. Camera-invisible objects are skipped, but camera-visible shared geometry and holdouts remain checked.
- Select **Cycles** as the render engine to show **View Layer > Passes > Deep**,
  directly below Cryptomatte. In the second UI revision the header has no checkbox:
  enable **Deep Alpha** (`view_layer.cycles.use_pass_deep`) inside the panel.
  **Deep Color** (`view_layer.cycles.use_pass_deep_color`) is a disabled, default-off
  placeholder with no render effect. **Deep ID** draws the existing
  `scene.cycles.use_deep_ids`; it is inactive while Deep Alpha is off or the
  transmittance error is strict (`0`), with an explanation for strict mode.
  The remaining controls stay on `scene.cycles`, shared by all view layers and
  greyed out until Deep Alpha is enabled. Error and surface depth tolerance show
  four and five decimal places respectively; defaults remain `0.001` and `0.0001`.
  This revision is pending release; the October 17 package uses the header checkbox.
  The former Render > Film panel is removed in builds containing this UI change.
- **Output > Output > Deep** contains `scene.cycles.deep_output_path`, an optional
  override visible when the current layer's Deep pass is enabled. Blank uses
  Blender's beauty frame path with the image extension replaced by `.deep.exr`.
  Overrides use the same Blender relative-path and frame-numbering rules:
  `//renders/shot_####` produces `shot_0001.deep.exr`, then `shot_0002.deep.exr`.
  Without `#`, Blender appends the frame number. Deep data is not a compositor socket.
- For separate deep view layers, put `{view_layer}` in **Deep > Path Override**:
  `//render/v001/{blend_name}_{view_layer}_####` writes
  `<blend>_BG_0001.deep.exr`, `<blend>_Girl_0001.deep.exr` and
  `<blend>_Robots_0001.deep.exr` when those layers have Deep Alpha enabled.
  Leave the override blank for unchanged single-layer naming. The panel warns
  when multiple deep layers have no `{view_layer}` in the override.
  **Use this token only in the Deep override**, not in the ordinary beauty
  `render.filepath`: Blender's beauty writer does not know this adapter variable.
  File Output nodes continue writing flat RGBA EXRs independently.
- `scene.cycles.deep_write_diagnostics` defaults to off and is hidden from the
  revised panel. Enable it through Python only for validation; the sidecar can be very large.
  The validation render helper enables it explicitly and normalizes its single-frame
  artifacts to `scene.deep.exr` and `scene.deep.exr.samples.csv` for existing validators.
- Deep in interactive (F12) renders is experimental; background renders are the
  qualified path. Both modes use the same Deep output driver, path rules and
  opt-in CSV setting. Viewport rendering and baking do not enable deep capture.
  Small CPU cancellation, consecutive-render and animation-frame checks passed;
  see the [UI verification report](../../builds/validation/blender-deep-ui/README.md)
  for results, the OptiX byte-identity check and remaining F12 follow-up.
- Old files with `scene.cycles.use_deep_output` enabled migrate to their single
  render view layer. Existing settings and non-empty path overrides are preserved;
  the old switch is consumed once. Helpers retain the old property as a fallback
  when running older builds.
- Optional `--deep-ids` / `use_deep_ids` writes per-sample UINT `id` and
  `cycles:deepIDManifest` (hex ID to object name), using Cycles Cryptomatte
  MurmurHash3 seed 0. Different objects retain overlapping samples. Select IDs
  as UINT before Gaffer converts channels to FLOAT; `tools/compare_deep_ids.py`
  provides exact selection and combined-alpha validation. IDs-off retains legacy
  output. Strict + IDs is rejected; IDs require a numeric deep error setting.
  Same-object depth merging and holdouts are qualified within the support matrix.
- Output: scalar Z/ZBack/A and separate native beauty. No deep RGB or refracted
  light-path reconstruction. Glass uses native camera-alpha semantics.
- Multiple view layers may have Deep enabled if their resolved paths are unique.
  Before every non-preview render callback, all deep-enabled layers are checked,
  including disabled layers and layers skipped by Render Single Layer. Windows
  path comparisons ignore case. A collision fails before rendering with the
  colliding layer names, destination and a request to add `{view_layer}` to the
  Deep override. Blank overrides receive no automatic layer suffix (option A1).
  Each layer publishes independently; there is no multi-file transaction.
  Other layers render normally.
  Mono rendering and no automatic tiling remain required.
  See the [multi-layer verification](BLENDER_DEEP_MULTILAYER_RESULTS.md).
- Prefer **indirect-only** over holdout to separate objects into deep layers.
  Indirect-only objects contribute lighting but no camera-visible deep samples;
  excluded collections contribute neither. Holdout objects retain deep opacity
  and can double-occlude when merged with their own layer. The holdout manifest
  marks these objects as a subset of the complete ID manifest; manifests are
  not filtered to camera-visible objects.
- Native OIDN, including GPU denoising in the production landscape, is qualified
  within the support matrix.
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
[measured results](evidence/README.md); historical build narratives remain
in Git history.

## Accepted landscape production

The supplied Scanlands scene is qualified on OptiX at 1175x500, up to 1024
adaptive beauty samples, with GPU OIDN, IDs, error1e-3 and default z1e-4.
Both all-samples and deep64 outputs passed. Original source assets are unchanged.
The accepted renders use an 8192 MiB deep memory budget and up to 8192 events;
unsupported inputs or exhausted capacities fail explicitly. See the
[production evidence](evidence/README.md) for final files, settings and timings.
Earlier hardware-budget experiments remain in the [archive](ARCHIVED_REPORTS.md).

## OSL surface capture

Use CPU or OptiX with `scene.cycles.shading_system = True`. Deep evaluates the
native compiled OSL surface group for scalar camera transparency. Preflight
rejects trace/ray-dependent/path-dependent or unknown queries, dynamic textures,
closure-building loops and unprovable closure capacity; runtime rejects coloured
or invalid extinction and cache misses. The material and reason appear in the
error. Native shader nodes and Script nodes can coexist in OSL mode; Cycles uses
one scene-wide shading system. CUDA OSL is unsupported.
See [support and native texture differences](SUPPORT.md).

## Shader-evaluated volume capture

OSL volumes use the native extinction shader evaluator on CPU/OptiX. SVM volume
graphs use it only with `--deep-volume-shader-eval` (`use_deep_volume_shader_eval`
in Blender); otherwise analytic capture is unchanged. Supply numeric deep error.
`--deep-volume-step` (`deep_volume_step`) caps the world-space voxel step and is
required for a volume without a grid. Zero selects the grid step automatically.

The EXR identifies shader evaluation, starting/maximum step range and the adaptive step rule. Half
the requested error bounds representation/fitting/publication; half is the
**stated, not proven** stepping allowance. Validate shader variation against a
4x-finer-step reference. Non-grey/invalid extinction, unsupported shader features
and bounded traversal/event overflow fail explicitly. The stepping contract is
recorded in the [validation policy](VALIDATION.md).

## Read-only scene scanner

Run the scanner inside this custom Blender before submitting a render:

```powershell
& D:/temp/cycles_deep_multilayer/install/blender.exe --background --disable-autoexec scene.blend --python tools/scan_blender_deep_scene.py -- --report D:/temp/cycles_deep_multilayer/scan.json --frame 1
```

Omit `--frame` to inspect the current frame; use `--layer BG` to restrict geometry
inspection. Collision checks still include all deep-enabled layers, including
render-disabled layers. Non-deep layers are listed as skipped. The scanner never
renders or saves; it restores temporary frame/view-layer evaluation changes.
Exit codes are 0 (no BLOCK), 2 (BLOCK findings), and 1 (tool error).

**This is a prediction; render-time validation is the authority.** The report
separates BLOCK, WARN and OK-ignored findings and lists checks that need compiled
Cycles graphs or session state. Graph simplification can remove a reported node;
OSL, volume integration and render-only evaluated geometry need renderer checks.
Path resolution covers standard blend/scene/camera/resolution/FPS/view-layer
variables and frame/extension rules; unavailable project variables are reported
explicitly instead of inventing a destination. No BLOCK does not qualify a scene.

`tools/test_scan_blender_deep_scene.py` compares scanner verdicts with tiny CPU
renders and reports every mismatch, including conservative constant-folding
false positives. Run it in Blender with `--python-exit-code 1` and a fresh output
directory after `--`.

Use `--device CPU`, `--device CUDA` or `--device OPTIX` to check the intended
execution backend without changing preferences. If background Blender has no
enumerated devices, the scanner reports that device availability is undecidable
as a WARN, rather than falsely blocking the scene. It never refreshes or saves
user preferences.

`{view_layer}` is valid **only in Output > Deep > Path Override**. Native render
paths and compositor File Output paths do not define it. For matching flat files,
name the File Output nodes after the layers (for example `BG`, `Girl`, `Robots`)
and use `{blend_name}_{node_name}_####` as their file name.

The scanner checks render.filepath, File Output directories/file names/per-item
paths (including nested compositor groups), and the deep override. Its variable
table and brace/numeric-format syntax follow the pinned Blender
`source/blender/blenkernel/intern/path_templates.cc`. Doubled braces escape literal
braces; numeric formats use `#` or `#*.#+`, not Python's `04d`. String/path variables
do not accept a format specifier. Multilayer EXR item names are channel names,
not paths. `render.frame_path()` supplies native validation for render.filepath;
the pinned RNA/Python bindings expose no general File Output/deep-template
validator, so those use the source-derived rules without editing RNA.

`tools/test_blender_deep_path_templates.py` provides isolated render-backed path
fixtures. Pass a fresh output directory and case after `--`; cases include
`valid`, `view_layer`, `brace`, `deep`, `format`, `string_format`, `directory`,
`item`, `render`, and `numeric`. Invalid compositor paths can log an error without
raising from the render operator: check the log and expected output files.

## Per-layer deep settings

View Layer > Passes > Deep provides Volume (Scene/On/Off), Surface Depth
Tolerance (Scene/Custom), and Events Per Sample (Scene/Custom). Existing files
use Scene defaults. Custom values apply only to the current deep layer; strict
mode still forces effective depth tolerance to zero. Controls are inactive when
Deep Alpha is off.

When effective Volume is Off, Volume Shader Evaluation and Volume Step are not
applicable: the adapter passes false and zero, the scanner uses the same rule,
and the panel greys out both controls. When Volume is On they retain scene values.
The event limit is never clamped: surface mode rejects more than 64 events. Use
Events Per Sample > Custom > 64 for a surface layer when the scene uses 4096
volume events. Error, samples, IDs and memory remain scene-level.

For CPU static Ribbon hair, set Volume Off, tolerance Custom 0, and events Custom 64
on the hair view layer. Use SVM and disable motion blur. Hair BSDF and Principled
Hair (Chiang/Huang) are covered by the named fixtures, including scalar transparent
mixes. IDs remain object-level; native continuation does not promise every segment
or a closed hair interior. THICK/THICK_LINEAR, Hair Info, OSL on hair and point
clouds remain unsupported. GPU hair is not yet qualified.

Hair and volume capture cannot share one deep layer: put the hair layer in surface
mode and leave separate volume layers on Volume On/Scene. The hair-layer event
override must remain at or below the surface limit of 64; overflow fails explicitly.
