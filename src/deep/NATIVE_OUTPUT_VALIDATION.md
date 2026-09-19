# Native deep ownership and host output

Date: 2026-09-19. First implementation patch from `ARCHITECTURE_REVIEW.md`.
M8 homogeneous CPU absorption remains the volume scope; no CUDA volume support
or performance improvement is claimed here.

## Ownership and delivery

- `session/deep.h`: `DeepSettings` in `SessionParams` holds enable/mode, event
  limit and the deep working-memory budget. No output paths.
- `session/deep.cpp`: the existing scene/material/device allowlist now runs
  inside Session before scene compilation and capture allocation.
- `integrator/path_trace.cpp`: PathTrace owns capture. Each delayed session
  reset releases old storage and supplies workers with a new non-owning pointer.
  Film no longer points into command-line application storage.
- `session/output_driver.h`: an existing host opts in with
  `supports_deep_output()` and implements `write_deep_render_tile()`.
- `integrator/path_trace_deep_tile.h`: a callback-scoped adapter exposes
  reconstructed Z/ZBack/A, actual camera populations and optional raw diagnostic
  samples. It does not expose the capture object, spill file or OpenEXR types.
- `app/deep_output.cpp`: the standalone adapter retains bounded scanline EXR
  export, CSV diagnostics, beauty-file identity and atomic EXR publication.

The render loop finishes all worker batches before deep delivery. Capture must
pass completeness validation; cancellation, device errors and progress errors
suppress delivery. Driver exceptions become Session progress errors. A scope
guard releases the render-in-progress flag even if a flat output callback
throws, so cancellation/destruction does not wait forever.

The driver receives a full frame with coincident data/display windows at origin
(0, 0), layer/view identity, and Y-up render-buffer coordinates. Depth is positive
camera-axis distance. Equal front/back is a surface; positive-length intervals
describe exponential scalar extinction. The API supplies visibility alpha, not
deep RGB. The EXR adapter flips rows to file coordinates as before.

`get_pixel()` returns an owned vector for one pixel. Hosts can retain returned
data, but the tile reference itself expires when the callback returns. Reads
must be serialized on the callback thread; do not reset Session there. A host
retaining an entire image owns the additional memory cost. The renderer still
budgets its own capture/reconstruction working set.

Raw samples distinguish surface local alpha from volume optical depth. Actual
accepted population includes completed misses. Reconstruction is available only
after the complete render, not as partial progressive output.

## Host usage

Build the host against the same headers with `WITH_CYCLES_DEEP_OPAQUE` enabled.
This changes the OutputDriver vtable and requires a host rebuild; it is not a
drop-in Gaffer DLL replacement.

1. Set `SessionParams::deep.enabled`, mode, event limit and memory budget.
2. Install a deep-capable OutputDriver before resetting/starting the session.
3. Set full-frame BufferParams matching the scene camera and call reset/start.
4. Consume samples in the final deep callback, checking `tile.cancelled()` during
   long exports and before publishing the host result.
5. After waiting, inspect `Session::progress` for errors or cancellation.

Reset creates a fresh capture, including when dimensions/sample counts change.
Disabling deep on reset releases capture. Adding samples to an already completed
capture is not a resume API. Tiling, crops, sample subsets and multiple devices
remain rejected. Current CPU/CUDA surface and CPU-volume capability restrictions
are unchanged.

Reset testing exposed compiler-added normal links in finalized shader graphs.
Preflight now recognizes those links and compiled closure-weight nodes while
retaining the qualified material families. Finalized graphs are immutable under
Cycles' existing graph contract; hosts replace graphs when changing materials.

## Validation

The native `cycles_deep_output_driver` test renders into an in-memory host with no
file writer. It covers depth/alpha/population and layer/view, returned-data
lifetime, changed dimensions and sample counts, disabling deep, unsupported
drivers, crop rejection, cancellation before delivery, and flat/deep callback
exceptions. It also exercises repeated opaque, transparent and volume sessions.
The same session successfully renders again after each tested failure or
cancellation. All seven CTest groups pass (10.76 seconds); the standalone build
with `WITH_CYCLES_DEEP_OPAQUE=OFF` also passes.

Commands used:

```powershell
cmake --build builds/build-m6 --target install --config Release --parallel 2
ctest --test-dir builds/build-m6 -C Release --output-on-failure
.\builds\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_transparency_gaffer.py builds/install-m6/cycles.exe builds/build-m6/native-output-surface
.\builds\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd env python src/deep/validate_volume_capture_gaffer.py builds/install-m6/cycles.exe builds/build-m6/native-output-volume
cmake --build builds/build-baseline --target cycles --config Release --parallel 2
```

CPU SVM/OSL surface validation: nine scenes and eight rejection cases pass;
maximum raw-ledger curve error `2.60770320892334e-8`.

CPU volume validation: nine scenes and twelve rejection/preservation cases pass;
maximum Gaffer/raw-ledger cut error `4.0410239277033355e-7`, below `1e-6`.
Independent geometry/extinction maximum error is `1.2843861837419368e-6`, within
its separate `2e-6` geometry gate. Deep-on/off beauty difference is zero.
All 31 matching raw CSV ledgers across the saved surface/volume baselines are
byte-identical. A deep-only CLI render also succeeds with no beauty path.

Review: `builds/build-m6/native-output-volume/m8_cpu_volume_review.gfr`. The open Gaffer
session uses the new `review.deep.exr` and matching beauty. FrontPoints/BackPoints
read Z/ZBack through the live DeepToPointCloud node; VolumeDepthCut controls cuts.
The images are generated by the standalone renderer; Gaffer's bundled renderer
has not been replaced.
The saved review reloads with 135,974 points in each front/back cloud and a valid
RGBA beauty reader.

CUDA runtime, other GPU backends, deep RGB, tiled rendering, resume and total
renderer memory limits are not newly qualified by this patch. Shared typed raw
records and CUDA storage/readback scheduling are the next architecture work.
