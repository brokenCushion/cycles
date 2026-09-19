# Real-asset test inventory

Inspected 2026-09-19 with Blender 5.2.1.

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

The supplied VDB remains a separate input. Homogeneous volume capture is qualified
on CPU/CUDA; native heterogeneous VDB capture is not yet implemented. The helper
`tools/create_vdb_deep_scene.py` successfully creates a native VDB test scene at
`builds/validation/native-vdb/scene.blend`, with grid transforms, bounds and source
hash recorded in `scene.json`. This is preparation, not a completed deep render.
It leaves the original VDB unchanged.

An exact 664 x 625, 100% reference render is saved at
`builds/validation/blender-reference/664x625/splash-wide.png`.
