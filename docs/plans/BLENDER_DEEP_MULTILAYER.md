# Blender Deep: multiple view layers (option A1)

Status: **implemented, option A1** (2026-10-18; status updated 2026-11-22).
CPU/OptiX fixtures, path preflight, UI and migration checks pass. The original
`hair_bsdf` blocker was resolved by later allowlist and Ribbon hair work; the
unchanged junkshop scene renders all three layers (junkshop5, Step 4 Part E).
Multi-layer cancellation was verified manually on 2026-11-22: Esc during Girl left BG published and no Girl, Robots or temporary files. All acceptance checks are now covered.
The original task made no core changes; later steps did.
The implementation request narrows section 3.3 to a warning label and excludes
all optional core work in section 3.4. Qualification results are recorded in
[the implementation report](../deep/BLENDER_DEEP_MULTILAYER_RESULTS.md).
Trigger: `thejunkshopsplashscreen_deep_v001.blend` fails with
`Deep requires exactly one enabled layer; enabled on: BG, Girl, Robots`.

## 1. Root cause

The error is a deliberate guard in the Blender host adapter, not a core or
writer limitation.

| Where | What |
| --- | --- |
| `tools/prepare_blender_deep.py:441-454` → `intern/cycles/blender/session.cpp` `BlenderSession::render()` | Counts every view layer with `cycles.use_pass_deep` and fails if more than one. |
| same, lines 461-489 | Resolves **one** deep path from `scene.cycles.deep_output_path` or the beauty `render.filepath`, with `.deep.exr` substituted. The variable map has only general and render-path variables (`blend_name`, `scene_name`, `camera_name`, `resolution_*`, `fps`). Nothing identifies the view layer. |

**Why the guard exists:** without it, every deep layer resolves to the same
file. In the junkshop file, `render.filepath = render\v001\` and the override is blank.
BG, Girl and Robots would all publish `render/v001/0001.deep.exr`. Publication is
atomic rename, so each layer would silently replace the previous one, and the
last layer would win. The guard turns that silent data loss into an error.

The error appears three times, once for each `render()` call, because Blender
invokes the engine once per layer. Its name order differs because the
evaluated scene lists the current layer first.

The old count guard also blocked layers with `layer.use == False` and layers
skipped by **Render Single Layer**. The approved collision check deliberately
includes them to protect jobs that render layers separately, but rejects only
actual path collisions.

## 2. Is the underlying architecture already per-layer? Yes.

| Concern | Finding | Source |
| --- | --- | --- |
| Render loop | Blender calls `engine->type->render()` once per render-enabled layer, each with its own depsgraph evaluation (`FOREACH_VIEW_LAYER_TO_RENDER`). `render_frame_finish` runs after all layers. | `source/blender/render/intern/engine.cc:872-941, 1100-1115` |
| Output driver | A new `BlenderOutputDriver`, with its own `deep_path_`, is built at the top of every `render()` call. | adapter, `session.cpp:355-416` |
| Session params | `get_session_params(..., &b_view_layer)` reads `use_pass_deep` from the layer being rendered. A non-deep layer gets `deep.enabled = false`. Mixed deep/non-deep layers are already qualified. | adapter, `sync.cpp` |
| Capture buffer ownership | `PathTrace::deep_capture_` (`unique_ptr<deep::Capture>`) is destroyed and recreated in `reset_deep()` on every `Session::reset`. That reset runs once per layer and view. Workers are detached first. | `src/integrator/path_trace.cpp:736-802` |
| Publication timing | `write_deep_output()` runs on the session thread right after `run_main_render_loop()`, and `render()` blocks on `session->wait()`. Layer N's EXR is therefore fully written before layer N+1 syncs. | `src/session/session.cpp:291-295` |
| Scene isolation | Each layer re-syncs objects. Indirect-only clears `PATH_RAY_VISIBILITY_CAMERA`, and deep capture intersects with `PATH_RAY_VISIBILITY_CAMERA` only, so indirect-only geometry never enters that layer's deep. Excluded collections are not synced. Holdouts keep camera opacity, are tagged in the holdout manifest, and are qualified. | `blender/object.cpp:207-233`, `src/kernel/deep/surface_cuda.h:19,34`, `docs/deep/SUPPORT.md` |
| Manifests | `validate_deep_scene()` rebuilds the object and holdout manifests on each reset from that layer's synced scene. | `src/session/deep.cpp:445-470` |

So mixing deep samples between layers is not possible with the current code.
At most one capture exists at a time. The only shared resource is the
**output filename**.

**Compositor / File Output integration:** this is not practical. RenderResult
passes and compositor sockets are fixed-size float images, while deep pixels
have variable-length sample lists. Supporting them would need changes to
RenderResult, the compositor, the File Output node and the image writers.
That means deep upstream surgery. Deep continues to bypass the compositor and
is written by the output driver. Your File Output nodes still write the
**flat** beauty, which Nuke uses for DeepRecolor.

## 3. Proposed architecture: per-layer deep paths in the adapter

Every change stays in `tools/prepare_blender_deep.py`, except for two small
optional core items (3.4).

### 3.1 `{view_layer}` template variable

Add `variables.add_string("view_layer", layer->name)` before
`BKE_image_path_from_imformat`. It works in both the override and the default
path. Example override for the junkshop file:

```
//render/v001/{blend_name}_{view_layer}_####
→ render/v001/thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr
                                                 _Girl_0001.deep.exr
                                                 _Robots_0001.deep.exr
```

This uses the same idiom as Blender's own `{node_name}` in File Output. All
existing relative-path, frame-padding and extension rules still apply.

The `.deep.exr` suffix is unchanged (agreed 2026-10-18). Deep files can never
overwrite flat EXRs, and the matching flat/deep pair differs only by
`.exr` versus `.deep.exr`. `{view_layer}` expands to the exact view layer name.
To keep names consistent, name the File Output nodes after the layers.

### 3.2 Collision preflight (replaces the count guard)

On each `render()` call, resolve the path of **every layer in the scene with
Deep enabled, whether or not it renders in this run**. This catches farm
setups that split layers across separate jobs (Render Single Layer, or
`layer.use` toggled per job). Those jobs would otherwise each pass the check
and overwrite each other. `{view_layer}` normally makes destinations unique;
Windows case-insensitive collisions (for example layers `A` and `a`) still
fail. A disabled Deep layer blocks rendering only when it really would collide.
If any two paths are identical, fail **before rendering anything**, and name
the layers and the path:

> Deep layers BG, Girl, Robots would all write render/v001/0001.deep.exr; add {view_layer} to Output > Deep > Path Override

**Decision A1:** what happens when the override is blank and more than one layer has Deep enabled?

- **A1 (chosen):** fail through the preflight message above. Naming stays explicit, and existing single-layer paths do not change.
- **A2 (not implemented):** apply an automatic `_<layer>` suffix when more than one deep layer is enabled.

Single-layer output stays byte-identical in both options. The multiview
(stereo) guard is unchanged.

### 3.3 UI

The Output > Deep panel shows a warning when more than one layer has Deep
enabled and the override does not contain `{view_layer}` (including blank).
This is a simple string check; the C++ collision preflight is authoritative.
The tooltip on `deep_output_path` documents `{view_layer}`. The panel's poll
and existing layout remain unchanged. Resolved-path previews are out of scope.
Use the token only in the Deep override: the beauty writer cannot expand it.

### 3.4 Optional core hygiene

Not approved for this task; both changes below remain out of scope.

- Release `deep_capture_` right after a successful publication instead of at
  the next reset. This lowers peak memory while the next layer syncs, and it
  matters when Persistent Data is on.
- Write a `cycles:viewLayer` header attribute.
  **Decision needed (B):** write it always, which changes the strict-mode
  header baseline and the byte-identity references, or only in non-strict mode.

## 4. Risks, limitations, compatibility

- **Render cost:** each layer is a full Cycles render, which is normal Blender
  behaviour. Deep adds per-layer capture memory (`deep_memory_mb`, spilling to
  disk) one layer at a time.
- **No multi-file transaction:** each file is atomic, but if Girl fails, BG
  for that frame is already published. Cancelling mid-layer leaves no partial
  file for that layer.
- **Existing limits still apply to every deep layer:** single device (one GPU,
  not multi-GPU), no automatic tiling (forced off), mono only, and no shadow
  catcher or caustics on geometry. The junkshop shadow-catcher flags are all
  on lights, which are skipped, so they are fine.
- **Deep is alpha-only:** the EXR has Z/ZBack/A and no RGB. In Nuke, use
  DeepRead, then DeepRecolor with that layer's flat EXR from the File Output
  node, then DeepMerge. `film_transparent` is on in the junkshop file, which is
  required.
- **Use indirect-only rather than holdout for deep layers**, as the junkshop
  file already does. A holdout produces opaque deep samples of the held-out
  object, which double-occlude after DeepMerge with that object's own layer.
- `{view_layer}` is an adapter-only variable. Blender's UI will not
  validate or highlight it, and a future upstream variable with the same name
  could conflict. The risk is low.
- The ID manifest lists every synced object, including indirect-only ones
  absent from that layer's samples. This is harmless and is unchanged unless
  you ask for filtering.
- `CYCLES_DEEP_Z_BASELINE`, a validation-only environment variable, is one
  fixed path. With several deep layers, the last layer wins. Only validation
  runs use it.
- **Nuke DeepRead has not been tested here.** Reader validation so far uses
  Gaffer and OpenEXR. The final Nuke check must be done on your machine.

## 5. Implementation plan

| Milestone | Work | Exit criterion |
| --- | --- | --- |
| M1 Adapter | `{view_layer}` variable, path-resolver helper, collision preflight across all Deep-enabled layers, removal of the count guard, UI warning | `prepare_blender_deep.py --check` passes, and the build installs to `D:/temp/cycles_deep_multilayer/` |
| M2 Core hygiene (optional) | Early capture release, `cycles:viewLayer` header per decision B | ctest and existing byte-identity checks unchanged, except for any approved header change |
| M3 Tests | See section 6. Update `tools/test_blender_deep_ui.py:132-142`, which currently asserts that two deep layers must fail. | All pass on CPU, with an OptiX smoke run |
| M4 Production check | Junkshop at reduced resolution, 3 layers, frames 1-2, OptiX, Gaffer DeepMerge review | Three files per frame, review graph is correct |
| M5 Docs | `BLENDER.md` host contract, `SUPPORT.md`, this plan marked agreed | Markdown link check passes |

## 6. Tests

A new fixture script builds three layers (A, B, C) over shared geometry:
overlapping opaque objects at different depths, a transparent material, an
indirect-only collection, a holdout collection and an excluded collection.

1. **Isolation, the key test:** render all three layers in one run, then render
   each layer alone. The deep EXRs must be byte-identical on CPU. This
   proves that no state leaks between layers.
2. **Single-layer regression:** the existing single-layer files and paths are
   byte-identical to the current build.
3. **Ownership:** with IDs on, every sample ID in layer X belongs to an object
   camera-visible in X. Indirect-only and excluded objects never appear.
   Holdout objects appear only in the holdout manifest subset.
4. **Depth and alpha:** the existing Z/ZBack/A validators and depth-cut
   checks run on each file. Overlap and transparency produce several samples
   per pixel.
5. **Collision preflight:** a blank override with two deep layers, or an
   override without `{view_layer}`, fails before rendering and writes no file.
   This includes a Render Single Layer run whose colliding partner layer is
   not rendering. With `{view_layer}` in the path, a disabled deep layer
   (`use=False`) does not block rendering.
6. **Mixed:** deep plus non-deep layers in one run (existing test, kept).
7. **Animation:** frames 1-3 in the background give three files per frame with
   the correct numbering.
8. **Cancel:** cancelling during layer B leaves A published and no B or C
   files.
9. **Readers:** OpenEXR deep reader and the Gaffer review graph with DeepMerge
   of all layers. Run Nuke DeepRead, DeepRecolor and DeepMerge manually.
