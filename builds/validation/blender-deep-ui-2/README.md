# Deep panel revision 2 — 2026-10-17

Branch: `blender-deep-ui-2`, based on `deep-output`
`98ef479432626d13102f1ed289d46a0926b42718`.
All Blender changes come from `tools/prepare_blender_deep.py`.
The clean pinned worktree check validated 1,105 files. Incremental build/install
passed under `D:/temp/cycles_deep_blender_ui/`; the toolchain was read-only.
Against the released overlay, only `blender/addon/properties.py` and
`blender/addon/ui.py` changed. Renderer source and numeric defaults are unchanged.

## Verification

- [Alpha off](alpha-off.png): Color disabled, Alpha usable, ID and settings inactive.
- [Alpha on](alpha-on.png): Color disabled, ID and settings active; defaults display
  as `0.0010` and `0.00010`. No header checkbox, diagnostics or F12 note.
- [Strict](strict-ids-inactive.png): ID inactive with the requested explanation;
  its saved value is preserved. Other settings remain active.
- [Alpha tooltip](alpha-tooltip.png): exact requested text, visually verified.
- RNA/draw assertions cover both render engines (Cycles/Eevee), Alpha off/on,
  strict/non-strict, row ordering, hidden CSV, default values and tooltip text.
  Output panel polling still follows Deep Alpha and Cycles. Setting the Color
  placeholder through Python has no effect on the rendered Deep EXR.
- Existing background suite passed: numbered and relative paths, overrides,
  CSV off/on for surfaces and volumes, two-layer rejection, mixed beauty/Deep
  layers, mono rule and baking with Deep enabled.
- Legacy migration passed: settings preserved, disabled layer unchanged,
  idempotent migration, explicit Alpha disable survives save/reload.
- `render_blender_deep_scene.py` still explicitly enables diagnostics and produced
  a valid 324-row, 5,246-byte surface CSV. Surface and volume CSV cases also passed
  in the background suite.
- CPU factory-cube render, 32x32, four samples, four threads, error `0.001`:
  entire 4,914-byte EXR equals the October 17 packaged-build result.
  SHA-256: `a982d3f277e16c9d1428f7bf0beda7fa8933ea61de98e3e33016c13fd89fb92c`.

Small JSON results and screenshots are stored here; no EXRs are committed.
Background assertions: `tools/test_blender_deep_ui.py`.
Screenshots: `tools/test_blender_deep_panel.py` with `--enable-event-simulate`,
window geometry `-p 100 50 1800 1900` and `--tooltip 685 460` on this display.
Both GUI verification processes closed without saving a scene.

## VDB defaults assessment — no changes

**16 events is a modest general default, not the actual native VDB capacity.**
[`PathTrace::reset_deep`](../../../src/integrator/path_trace.cpp) raises native-grid
capacity to `max(4096, deep_max_events)`; the supported maximum is 8192
([constants](../../../src/kernel/deep/types.h)). Thus leaving 16 selects 4096
for native VDB capture. It can suit small grids, but is not a reliable preset for
complex volumes. The accepted landscape needed 8192; retain that explicit setting
when reproducing it. Event overflow fails explicitly rather than truncating data.

**512 MiB is a conservative starting budget, not a production VDB recommendation.**
Capture spills to disk, but CPU worker buffers or GPU staging, reconstruction,
row buffers and export workers still require resident memory. Insufficient budgets
can fail working-set checks or limit export parallelism; spilling cannot remove
those requirements ([capture allocation](../../../src/deep/capture.cpp)). The
budget excludes ordinary scene/beauty memory. The accepted landscape used
8192 MiB and still generated substantial spill files; see the
[production measurements](../../../docs/deep/evidence/README.md).

No default was changed. This assessment uses source inspection and existing
production evidence; no new large-VDB capacity/performance sweep was run.
No release packaging, tag, upload, push or PR was performed for this revision.
The already-published October 17 release was left unchanged.
