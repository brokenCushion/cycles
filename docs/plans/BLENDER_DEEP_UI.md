# Plan: move Deep controls into Blender's View Layer and Output tabs

Status: planned, nothing implemented. Written for handoff to a fresh session.
Layout decision (2026-10-16): **option B, grouped like Cryptomatte**. All deep
settings sit under the pass checkbox; only the file path goes in the Output tab.

## Goal

Today every deep option lives on `scene.cycles` in one subpanel,
Render Properties > Film > Deep Visibility. Move them so Deep behaves like a
pass and a first-class feature, in the same way Cryptomatte keeps its settings
under its pass checkboxes:

| Location | Contents |
|---|---|
| View Layer tab > Passes > **Deep** (subpanel directly below Cryptomatte) | Enable checkbox (`view_layer.cycles.use_pass_deep`), then the settings below it: `use_deep_volume`, `use_deep_volume_shader_eval`, `deep_volume_step`, `use_deep_ids`, `deep_error`, `deep_z_tolerance`, `deep_samples`, `deep_max_events`, `deep_memory_mb`, plus the new `deep_write_diagnostics` (see below). Settings are greyed out (`layout.active`) until the checkbox is on. |
| Output tab > Output panel > **Deep** subpanel | Optional `deep_output_path` override, blank by default. Shown only when the Deep pass is on. |
| Render tab | **No Deep panel.** Delete the old Film > Deep Visibility subpanel. |

### Where the properties live

- `use_pass_deep` lives on `CyclesRenderLayerSettings` (`view_layer.cycles`).
- The other settings stay on `scene.cycles` (render-wide). The View Layer subpanel
  draws them from `context.scene.cycles`. Add a one-line label in the subpanel:
  "Shared by all view layers". Only one view layer can use deep, so per-layer
  copies would add complexity with no benefit, and keeping them on `scene.cycles`
  means old files keep their values.

## Path behaviour

- The compositor File Output node cannot be used. Deep data is written by the
  C++ output driver and never enters the render result, so there is no socket.
- **Blank override:** derive the deep file from Blender's own frame-path
  function for Output Properties > Output path (Python
  `scene.render.frame_path(frame=...)`; on the C++ side use the same Blender path
  logic through RNA, or `BLI_path_abs` + `BLI_path_frame`). Don't reimplement
  `####` handling by hand. Blender's function already handles `//` relative
  paths, paths without `####` (it appends the frame number itself), padding and
  the file-extension option. Replace the image extension with `.deep.exr`, so
  the deep file sits next to the beauty image: `shot_####` gives
  `shot_0001.deep.exr`. This also fixes a current gap: the single fixed
  `deep_output_path` is overwritten on every frame of an animation.
- **Non-blank override:** resolve it with the same Blender path functions
  (relative `//` and `####` frame substitution, with the frame number appended
  when there is no `#`), so it behaves exactly like Blender's own output path.

## Diagnostic CSV becomes opt-in

The `.samples.csv` written next to deep EXRs is a validation file. For the
landscape it is about 2.5 GB per frame. Add `scene.cycles.deep_write_diagnostics`
(default **off**), drawn in the Deep subpanel under a small "Diagnostics" label.

- Production renders write only the deep EXR.
- `tools/render_blender_deep_scene.py` and the other validation scripts must turn
  it on explicitly wherever a validator reads the CSV.
- First check how the CSV is produced today (always, or only with a flag) and
  record it in the results.

## Interactive (F12) renders

Deep capture and EXR publication currently require background rendering
(`blender -b`). With a visible Deep checkbox, users will tick it and press F12,
then get no file and no explanation. Do both of the following:

1. When the pass is on, show a clear note in the Deep subpanel: "Deep files are
   written only in background renders (blender -b)". If an F12 render runs with
   Deep on, report a warning in Blender's status/info area rather than failing
   silently.
2. Investigate whether F12 renders can write deep output with the same driver,
   and report feasibility and risks. Don't implement it in this plan without
   approval.

## View layer rule

The rule becomes **exactly one view layer with Deep enabled** (instead of
exactly one view layer in the scene). Other view layers may exist and render
normally. If more than one has Deep enabled, fail with a clear message naming
those layers. Keep the existing mono-only check.

## Old .blend files

Migrate, don't reset. In the Cycles add-on's file-load versioning (the existing
`version_update.py` handler, or a `load_post` handler if that's cleaner in this
fork):

- if the old `scene.cycles.use_deep_output` is on, set `use_pass_deep` on the
  scene's single render view layer;
- keep a non-empty old `deep_output_path` as the new override value (it then
  follows the Blender path rules above);
- the other settings already stay on `scene.cycles`, so they need no migration.

Make the migration idempotent and test it on a saved file with the old
properties (for example the landscape test scene).

## Where the code is

The adapter is `tools/prepare_blender_deep.py`. It patches a clean pinned Blender
checkout (`749518deb2f0735a22361a07488b35f7ea5c2fdf`) using `edit()` string
anchors. All Blender-side changes go in this script, not in the installed copy.
Don't rerun it over a dirty checkout; it refuses.

1. **Properties** (script lines ~80-107, `blender/addon/properties.py`): add
   `use_pass_deep` to `CyclesRenderLayerSettings`. Keep the remaining settings on
   `scene.cycles`, add `deep_write_diagnostics`, and keep the old
   `use_deep_output` readable for migration and script fallback.
2. **UI** (script lines ~108-129, `blender/addon/ui.py`):
   - Delete `CYCLES_RENDER_PT_deep` (the Film subpanel) and its registration.
   - Add `CYCLES_RENDER_PT_passes_deep`: `bl_context = "view_layer"`,
     `bl_parent_id = "CYCLES_RENDER_PT_passes"`, registered right after
     `CYCLES_RENDER_PT_passes_cryptomatte`. Header checkbox or first row: Enable.
     Below it: the settings, the "Shared by all view layers" label, the
     Diagnostics option, and the background-render note.
   - Add an Output-tab subpanel with `bl_parent_id = "RENDER_PT_output"` (built-in
     panel). Unverified: confirm it is a Python panel in this Blender version. If
     it isn't, fall back to a standalone Cycles panel in the Output tab.
3. **Sync** (script lines ~130-152, `blender/sync.cpp`): `get_session_params`
   reads the deep settings from the scene's `cscene`. The enable flag now comes
   from the render view layer. Check whether this function has view-layer access;
   if not, pass it in.
4. **Session** (script lines ~330-359, `blender/session.cpp`): resolve the deep
   output path as described above, then apply the view layer rule, the existing
   mono check, the diagnostics flag and the F12 warning.
5. **Callers**: `tools/render_blender_deep_scene.py` (lines ~105-125),
   `tools/create_vdb_deep_cases.py` (~262), and the docs `docs/deep/BLENDER.md`
   and `docs/deep/QUICKSTART.md` set or describe `scene.cycles.use_deep_output`
   and the Film panel location. Update them to the new locations. Keep
   `scene.cycles.use_deep_output` as a fallback in the scripts so old invocations
   still work (the user approved this). Validation scripts turn on
   `deep_write_diagnostics`.

## Preconditions and risks

- **Clean checkout:** the pinned checkout exists at
  `C:\Users\jun\Documents\ChatGPT\cycles_deep\builds\blender\source` at the right
  commit, but it is already patched (about 916 modified files), so the script
  will refuse it. **Don't reset or modify it**: current builds use it.
  - Create a separate clean worktree of the same commit, for example
    `git -C builds/blender/source worktree add D:\temp\cycles_deep_blender_ui\blender-clean 749518deb2f0735a22361a07488b35f7ea5c2fdf`.
  - Run `python tools/prepare_blender_deep.py --check` against it, then implement.
  - The build directory also goes under `D:\temp\cycles_deep_blender_ui\`.
- **Rebuild required.** This is a C++ and add-on change; a Python-only edit to the
  installed add-on cannot work, because sync still reads the old properties.
- **Installed reference copy** of the current add-on (read-only, for panel names
  and ordering): `C:/Users/jun/Downloads/cycles-deep-blender/5.3/scripts/addons_core/cycles/`
  (`ui.py` has `CYCLES_RENDER_PT_passes` at ~1057, `properties.py` has
  `CyclesRenderLayerSettings` at ~1549).
- Temp and build outputs go to `D:\temp\<project>` (see user memory). Don't touch
  `D:\CyclesDeepScratch`.

## Verification

- `tools/prepare_blender_deep.py --check` passes on the clean worktree.
- Rebuild and install. In the UI, confirm that:
  - the Deep subpanel appears directly below Cryptomatte in View Layer > Passes;
  - its settings are greyed out with the pass off;
  - the background-render note shows with the pass on;
  - the Render tab has no Deep panel;
  - the Output subpanel appears only with the pass on.
- Background render with the pass on: the deep EXR is written next to the beauty
  path given by Blender's frame path, with frame numbering across a 2-frame
  animation (no overwrite). This covers an Output path with `####`, one without
  `#`, and a `//` relative path.
- An override path with `####` writes the expected files.
- Without `deep_write_diagnostics`, no `.samples.csv` is written. With it on, the
  CSV is written and the existing validators still pass.
- Two view layers with Deep on fail with a message naming both. One with Deep
  on and another without both render.
- An F12 render with Deep on shows the warning, and no file is written.
- An old `.blend` with `scene.cycles.use_deep_output` on loads with the pass
  enabled and the same settings, and migration is idempotent.
- Re-run the existing deep validation for one qualified scene to confirm the
  deep data is byte-identical to before the UI move (payload and deterministic
  headers, per the project's identity rule).
