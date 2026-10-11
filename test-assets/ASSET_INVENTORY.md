# Real-asset test inventory

Inspected 2026-09-19 with Blender 5.2.1.
The bakery/VDB observations below record the original inputs. Current qualified
features are listed in the [support matrix](../docs/deep/SUPPORT.md).

## Production landscape

The final landscape is **Scanlands by Piotr Krynski**, the Blender 3.3 LTS demo
scene listed under CC-BY-SA on [Blender's demo-files page](https://www.blender.org/download/demo-files/).

- [Original Blender 3.3 splash scene (.blend)](https://www.blender.org/download/demo/splash/blender-3.3-splash.blend)
- [Cloud Pack VDB (.zip)](https://www.mediafire.com/file/879onggscuon7ew/CloudPackVDB.zip/file), supplied by the user as the cloud source used in the test scene

Its path-repaired copy and original texture/VDB inputs are local assets, not
included in Git. Both accepted OptiX production outputs and their source-scene
checksum are recorded in the [evidence](../docs/deep/evidence/README.md).

## Blender scene

`blender/blender-3.5-splash.blend`

- Cycles scene, active camera `CAM-wide`, alternate camera `CAM-closeup`.
- Saved resolution: 664 x 625 at 50% (effective output 332 x 312); animation
  frames 1–250.
- 175 materials and a large assembled scene: architecture, furniture, food,
  plants, lights and two cameras.
- No `VOLUME` object is currently present, so the scene does not reference the
  supplied VDB yet.

## VDB

`vdb/firePlume_0000.vdb` (approximately 41 MB)

- Blender loads it successfully.
- Grids: `density` (FLOAT) and `flames` (FLOAT).
- Both grids use the same object transform; the loaded transform translates
  the grid by approximately `(-111.5, -96, -287.5)` in the VDB object space.

## Test implication

The scene now renders through the custom native Blender/Cycles integration at
664x625, 128 samples, with validated scalar deep output. Compact capture repeats
it in 247.633 seconds with byte-identical deep output and unchanged beauty.
Standalone Cycles still does not open `.blend` files directly.

The supplied VDB remains a separate input. Native heterogeneous VDB capture was
subsequently implemented and qualified; see the current support matrix. The helper
`tools/create_vdb_deep_scene.py` successfully creates a native VDB test scene at
`builds/validation/native-vdb/scene.blend`, with grid transforms, bounds and source
hash recorded in `scene.json`. That initial preparation record is historical.
It leaves the original VDB unchanged.

An exact 664 x 625, 100% reference render is saved at
`builds/validation/blender-reference/664x625/splash-wide.png`.
