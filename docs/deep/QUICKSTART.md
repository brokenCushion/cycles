# Try Cycles Deep

The quickest first check is to [download an accepted deep EXR](https://github.com/brokenCushion/cycles/releases/tag/deep-output-evidence-2026-10-10)
and inspect it in Gaffer. To render your own, use our custom Blender build.

## 1. Download Blender

Download the Windows x64 ZIP from the [Blender release](https://github.com/brokenCushion/cycles/releases/tag/cycles-deep-blender-2026-10-17).
Verify it against the release's `SHA256SUMS.txt`, then extract the **whole folder**
and launch `blender.exe`. Keep its DLLs, Python and kernels beside it.
The source archive, build pins and license notices accompany the download.

This Blender 5.3 alpha package includes the View Layer UI update. Its CPU and
OptiX smoke outputs match the qualified UI-change results byte-for-byte.
It was qualified on Windows with an RTX 3080. CUDA kernels target `sm_86`;
other CUDA architectures and other operating systems are not qualified by this
package. See the [support matrix](SUPPORT.md) before choosing a backend.

## 2. Find the settings

Set the render engine to **Cycles**: the Deep panel appears only for Cycles.
In builds containing the second UI revision, open **View Layer > Passes > Deep**,
directly below Cryptomatte, and enable **Deep Alpha** inside the panel.
**Deep Color** is a disabled placeholder (not implemented). **Deep ID** requires
Deep Alpha and a non-zero Deep Transmittance Error; strict (`0`) disables the
control. The remaining settings are greyed out while Deep Alpha is off and are
shared by all view layers. Only one layer can enable Deep Alpha; other layers may render
normally. Deep in interactive (F12) renders is experimental; **background renders
(`blender -b`) are the qualified path**. F12 uses the same Deep output driver
and path rules.

**Output > Output > Deep** offers an optional path override. Leave it blank to
write beside the beauty frame using `.deep.exr`; Blender handles `//`, `####`
and frame numbers. Diagnostic samples CSV is off by default in both background
and F12 renders and has no control in the revised panel. Python can enable it with
`bpy.context.scene.cycles.deep_write_diagnostics = True`.
The validation helper below does this explicitly.
The [UI verification report](../../builds/validation/blender-deep-ui/README.md)
records the small F12 cancellation/repeat/animation checks and remaining limits.

The October 17 package linked above uses the first UI revision: enable Deep in
the panel header, with the CSV checkbox visible under Diagnostics. The second
revision described here is pending a new release. The October 11 package uses
**Render Properties > Film > Deep Visibility**; the helper supports it as well.

- **Deep Samples:** `0` captures all accepted camera samples; `64` takes the first
  64 per pixel, or fewer if adaptive sampling stops earlier. This caps camera
  samples, not depth layers, and does not reduce beauty sampling.
- **Deep ID:** identifies objects through overlapping layers; requires non-strict error.
- **Error:** `1e-3` controls visibility-curve approximation. It does not bound the
  sampling difference between capped and uncapped output.
- **Memory:** bounds deep working storage; large captures can spill to disk.

Deep files contain **Z, ZBack, A and optional UINT id**. Beauty colour is a separate
file; deep RGB is future work. [Full settings and limits](BLENDER.md#host-contract-and-limits).

## 3. Render a small example

Clone the repository and run this PowerShell example from its root, replacing
the executable, scene and output paths. Start with a small resolution before
attempting a full frame. The output directory should be new for each comparison.

```powershell
git clone https://github.com/brokenCushion/cycles.git
cd cycles
& "D:/Tools/cycles-deep-blender/blender.exe" --factory-startup --background --disable-autoexec "D:/Scenes/landscape.blend" --python-exit-code 1 --python tools/render_blender_deep_scene.py -- --output "D:/Renders/deep-example" --device OPTIX --samples 64 --percentage 5 --deep --deep-volume --deep-samples 64 --deep-ids --deep-error 1e-3 --deep-max-events 8192 --deep-memory-mb 8192
```

The helper writes `beauty.exr` and `scene.deep.exr` without saving your source
scene. It disables compositing and sequencing to expose the renderer result.
`--device CPU` selects CPU rendering. OSL deep capture is supported on CPU and
OptiX, within the support matrix; CUDA does not support OSL.
Remove inherited `OCIO` overrides when reproducing native Blender colour results.

For the landscape, obtain the [original scene and cloud assets](DEVELOPMENT.md#3-reproduce-full-qualification)
and repair their external paths. Volume capture requires a static, mono
perspective camera without depth of field or motion blur. This short example is
a starting command, not a reproduction of the full production qualification.

## 4. Inspect the deep file

In Gaffer, connect an **ImageReader** for `scene.deep.exr` to **DeepSlice**, then
**DeepToFlat**. View the alpha channel and enable/change DeepSlice's far clip to
see depth cuts. Load `beauty.exr` separately to view the colour render.

For a 3D preview, install the [DeepToPointCloud adapter](../../src/deep/gaffer/README.md)
and connect the **unflattened** deep image to it. Match the camera to your render;
start with Pixel Stride `4` and Max Points `100000`. These limit the preview, not
EXR reader memory or the data stored in the file. The preview shows depth samples,
not a fully shaded scene.

For exact deepID selection, use the [native UINT ID helpers](BLENDER.md#host-contract-and-limits);
ordinary FLOAT channel conversion can lose ID precision.

## Verified download, further checks

The October 17 package was built from the merged source and extracted into a fresh
directory. Every bundled runtime file was checksum-verified; startup registration,
CPU/OptiX background renders, F12 and legacy migration passed. Both ZIPs passed CRC.
See its release asset `package-verification.json` and
[developer verification](DEVELOPMENT.md) for independent checks and core tests.
