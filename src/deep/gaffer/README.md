# Live DeepToPointCloud for Gaffer

`CyclesDeep.DeepToPointCloud` is a Python compute node with a real deep **image
input** and a **scene output**. Connect `ImageReader.out`, `DeepMerge.out`, or a
non-flattened `DeepSlice.out` to `in`. View `out` in Gaffer's 3D Viewer. No mesh,
external EXR path or precomputed scene cache is used by this node.

The project-local Gaffer 1.7.2.0 installation has this package installed under
`python/CyclesDeep` and its node-menu registration in `startup/gui/cyclesDeep.py`.
Create another node using Tab search **DeepToPointCloud**, or the
**Cycles Deep / DeepToPointCloud** menu. Other Gaffer installations need both
files installed on their Python/startup paths before opening the saved graph.

## Controls

- **in:** deep image, with A and positive axial depth (normally Z).
- **camera / cameraPath:** optional Gaffer camera scene. Uses the camera object's
  perspective/orthographic projection, film fit, aperture offset and world
  transform. Gaffer cameras look down -Z. Scene globals are not applied; use a
  camera whose object contains the intended render settings.
- **Vertical Field Of View / Forward Axis / Camera Transform:** manual camera
  fallback. Defaults match this review: 0.9 radians expressed in degrees,
  positive Z, identity transform. The input format supplies aspect and pixel aspect.
- **Pixel Stride:** 1 includes all pixels. 2 includes every other pixel in each
  dimension, keeping all samples at those pixels. Use it for faster previews.
- **Alpha Threshold:** skips samples at or below the specified effective alpha.
- **Depth Channel:** Z by default; ZBack can show the other boundary of intervals.
- **Use Image Color / Color:** unpremultiply RGB by sample alpha when present,
  otherwise use a fallback color. M4's raw opacity-only EXRs have no RGB; the
  review's upstream tint nodes supply identification colors.
- **Point Width:** viewport size in pixels. Changing it does not rebuild points.
- **Max Points:** maximum displayed points, default five million. If more samples
  survive the stride/alpha filters, a deterministic subset is selected across the
  entire input. For example, one million shows one million points from this
  review instead of failing or cutting off the last objects. At or above the
  available count, all samples are preserved.

Each selected sample becomes one point. The output retains `deepAlpha`, `deepZ`
and `deepZBack` as vertex primitive variables, with RGB in `Cs`. Image/frame,
upstream processing and camera changes invalidate the computed result through
Gaffer's dependency/hash system. Gaffer caches results in memory; there is no
manual conversion step or disk cache.

## Limits

This is an experimental Python adapter, not a claim of full Nuke feature parity.
It uses pixel centers because the original subpixel locations are unavailable.
It assumes positive axial depth, static pinhole perspective or orthographic
projection; ray-distance depth, lens distortion and motion-time reconstruction
are not supported. DOF images can be displayed as a central-camera projection
of their deep depth distribution, but the original lens-ray hit positions cannot
be recovered from Z/ZBack/A. A volume interval produces a boundary point, not a filled
volume. No hidden geometry is invented. Color/alpha are point attributes; the
viewport displays solid colored dots rather than reproducing deep compositing.

Python conversion of millions of samples takes seconds. Image hashes are taken
over the input data window, including deep offsets and all consumed channels.
Cancellation is checked during hashing and conversion. A first pass counts and
validates eligible samples, followed by conversion of selected samples only.
Selection uses one reproducible, hash-jittered index per disjoint stratum of
sample order, avoiding periodic selection of the same depth layer. It does not
guarantee that tiny objects survive very small caps. No new points or averaged
depths are invented. The cap limits output points, not input scan time or total
process memory; Pixel Stride can additionally reduce preview work.

## Validation

Run `gaffer env python src/deep/gaffer/test_deep_to_pointcloud.py` from the repo.
Tests cover multiple samples per pixel, known projected positions, nonzero
display origins, pixel aspect, RGB unpremultiplication and upstream color changes,
stride/opacity filters, limits, invalid inputs, camera transforms, orthographic
projection, frame-dependent depth, and save/reload with subsequent edits.

The complete 640x480 merged review evaluates to 3,069,012 valid points with Z
from 3.1000986099 to 10.9994649887. The live node is wired to
`MERGE_separate_deep_objects`; the old scene-cache reader is retained only as an
unconnected reference. Existing `VIEW_DEEP_POINTCLOUD` now follows the live node.
With `maxPoints=1,000,000`, the same graph evaluates to exactly 1,000,000 valid
points, with Z from 3.1001446247 to 10.9989461899 in the headless validation.
Restart Gaffer to load installed Python changes into an existing process.
