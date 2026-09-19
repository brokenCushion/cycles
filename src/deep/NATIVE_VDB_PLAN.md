# Native VDB capture implementation gate

This is the implementation plan for the supplied `firePlume_0000.vdb`, not a
claim that heterogeneous capture is implemented. The existing analytic density
reference and homogeneous CPU/CUDA tests do not qualify this asset.

## Native integration points inspected

- `scene/volume.h`: `Volume` inherits `Mesh`; native volume geometry must pass
  the host preflight explicitly. The present mesh-only, closed-convex test in
  `session/deep.cpp` cannot simply be applied to a sparse volume boundary.
- `kernel/geom/volume.h`: voxel attribute descriptors identify the image texture.
  World positions undergo the object's inverse transform before lookup.
- `kernel/util/image_3d.h`: the image texture's `transform_3d` maps that position
  to grid coordinates. NanoVDB linear interpolation uses `floor(P)` and eight
  integer-coordinate grid values. There is no extra half-voxel shift in this
  path. Float, FpN and Fp16 grids have distinct native accessors.
- `kernel/deep/volume.h`: current capture integrates one constant extinction
  value across a convex medium. Reusing that evaluation for a VDB would be
  incorrect. The ray must visit every crossed interpolation cell (or skip only
  regions with a proven empty bound), including thin occupied features.

## Required implementation

1. Resolve the native density grid and scalar absorption shader through Cycles
   attributes, shader validation and image metadata. Reject unsupported shader
   operations/interpolation explicitly. Preserve object and grid transforms.
2. Traverse grid cells in ray order. Trilinear density restricted to a ray is
   cubic in each cell; use all eight cell values to integrate it and bound the
   error introduced by representing it as deep exponential intervals. Merely
   sampling cell endpoints, or using stochastic beauty samples, is insufficient.
3. Extend bounded capture to carry the resulting ray data. The current 64-event
   limit is not a practical production VDB capacity. Evaluate chunked emission
   or another budgeted representation before increasing per-lane storage;
   account for host/device buffers and fail explicitly on exhausted capacity.
   Never allocate dynamically inside a GPU thread.
4. Handle near/far clipping, camera-inside rays, overlapping media and surface
   events using the same physical-length/axial-depth convention as homogeneous
   capture. Keep beauty RNG and state unchanged.

## Acceptance evidence

- Native cell fixtures: constant density, all-axis ramps, general cubic ray,
  negative grid indices, transformed grids, cell-edge/corner ties, clipped rays,
  and a one-voxel feature missed by sparse marching.
- Compare native CPU/CUDA outputs against independently evaluated grid integrals
  and depth cuts. Exercise explicit storage/precision failures and confirm no
  completed EXR is published for failed captures.
- Load the supplied VDB directly into a native volume scene, record asset/grid
  identity and transforms, render deep plus beauty, and compare deep-on/off
  beauty. Initial asset resolution/sample count must be reported explicitly.
- Present the actual EXR in Gaffer with depth cuts and DeepToPointCloud. Preserve
  the user's existing bakery review and its unsaved Transform/PathFilter edits.

Deep RGB and scattering reconstruction remain separate from the existing scalar
visibility contract; this gate must still test the actual VDB density grid.
