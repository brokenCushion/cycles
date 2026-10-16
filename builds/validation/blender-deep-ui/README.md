# Blender Deep UI verification

Final local results, 2026-10-16. **Passed.** The user accepted checkpoint 2 and
then approved retaining experimental F12 output after the feasibility probe
showed that the released build already supported it.

The copied [CHECKPOINT_2.md](CHECKPOINT_2.md) and
[F12_FEASIBILITY.md](F12_FEASIBILITY.md) are historical snapshots, preserved
unchanged. Their references to an F12 block, background-only warning and
uncommitted implementation are superseded by this report and its commit.
The original [agreed plan](../../../docs/plans/BLENDER_DEEP_UI.md) is likewise
preserved as agreed; the user's later F12 decision governs the final behavior.

## Final behavior and build

- View Layer > Passes > Deep sits directly below Cryptomatte. Its checkbox
  belongs to the view layer; shared controls and diagnostics remain on the scene.
- Output > Output > Deep contains the optional path override. Blender's own
  frame-path rules handle numbering and relative paths.
- F12 uses the same Deep driver as background final renders. The subpanel says:
  “Deep in interactive (F12) renders is experimental; background renders are the
  qualified path.” CSV remains opt-in in both modes. No F12 warning/block remains.
- The optional null view-layer argument keeps viewport and bake capture off.
- All Blender edits come from `tools/prepare_blender_deep.py`; no core source changed.
- Clean overlay check: 1,029 files. All generated hashes match the manifest.
  Release rebuild/install passed with clang-cl 20.1.8, CUDA 12.8 and OptiX 8.
- Blender pin: `749518deb2f0735a22361a07488b35f7ea5c2fdf`.
- Final executable SHA-256:
  `7499d3a8dd448c469c4f8ef26e38cb37786c5869ed8a1d3f8a10b83bc454cdf5`.

Builds and raw render outputs remain under `D:/temp/cycles_deep_blender_ui/`;
the original patched `builds/blender/source` was not edited. The Scratch
toolchain was read-only. Output/cache path guards remain in the local build
runner; the explicitly permitted Windows GUI cache is the sole cache exception.
Nothing was pushed or published.

## Tests after removing the F12 block

| Check | Result and retained evidence |
|---|---|
| Two consecutive interactive renders in one process | Both completed and published structurally valid Deep EXRs; moving the cube changed identity. First CSV off, second explicitly on. [results](interactive-runtime/results.json) |
| Cancel mid-render over existing files | Native ESC cancelled the render; previous EXR and CSV SHA-256 values stayed unchanged; no partial publication directories remained. [results](interactive-runtime/results.json) |
| Cancel mid-render to fresh paths | Native ESC cancelled the render; no EXR, CSV or partial publication directories remained. [results](interactive-runtime/results.json) |
| Interactive animation frame step | `INVOKE_DEFAULT`, animation enabled, start=end=2: beauty and `frame_0002.deep.exr` published, CSV off. [results](interactive-runtime/results.json) |
| Background regression suite | Numbered two-frame hash/no-hash/relative/override paths, extension off, surface/volume CSV opt-in, volume CSV identity, mixed layers, named multiple-Deep rejection, mono rejection, float-image bake and panel draw contract all passed. [results](ui-final/results.json) |
| GUI/viewport | New note and panel layout visually inspected; viewport displayed “Rendering Done” with Deep enabled; F12 subsequently published EXR and CSV. [results](gui-final/results.json), [enabled panel](gui-final/view-layer-on.png), [disabled panel](gui-final/view-layer-off.png) |
| OptiX background sanity | Same factory cube, 64x64, 16 fixed samples, strict mode, seed 0, no DOF/denoising. Released build, checkpoint 2 and final build produced identical entire Deep EXRs. [identity](optix-original-identity.json), [checkpoint-2 comparison](optix-identity.json) |
| Independent OptiX validation | Unchanged Gaffer validator passed original and final outputs: 81 grid pixels, 232 boundaries, maximum curve error 9.986254545513162e-09, slice error 0, 6,845 points. [result](optix-after/gaffer_validation.json) |
| Source checks | Python compilation, whitespace checks and 1,029 generated-source hashes passed. [status](followup-status.json) |

Interactive tests used the normal non-background render operator invoked by F12,
with a 64x64, 8-sample CPU cube. Cancellation cases used 128x128, 1,024 samples,
four CPU threads and the default 512 MiB Deep budget. Native ESC was delivered
after the test requested cancellation at least two seconds into each active job.
Final handler counts: five starts, three completions, two cancellations.

The first cancellation fixture exceeded the scanline memory budget before ESC.
Its dimensions/sample count were corrected without changing the assertions.
A second harness attempt used simulated ESC, which bypasses Blender's
`G.is_break`; real events are also ignored with `--enable-event-simulate`.
The final run omitted that flag and used native keyboard input. These harness
attempts are not counted as passing tests; their small result JSONs are retained
under `interactive-first-attempt/` and `interactive-simulated-esc/`.

GPU: NVIDIA GeForce RTX 3080, driver 610.88. Each OptiX EXR was 36,129 bytes with
SHA-256 `cc3b98dc07e06fbdc46a3e736bb40d794c68faffd8792a747512d6f4c3b1b615`.
Render settings and executable hashes are in `optix-original/render.json`,
`optix-before/render.json` and `optix-after/render.json`.

## Evidence and remaining scope

This directory retains the reports, settings, identities, result JSONs and small
logs/screenshots needed to review the findings without the temporary directory.
Absolute paths inside historical JSONs/logs record provenance; they are not
required to read the results. No EXRs or screenshots above 1 MB are included.
Large raw outputs and the inspected viewport screenshot are intentionally omitted.

Checkpoint 2's accepted migration and qualified bakery CPU strict/numeric
byte-identity results remain in the copied reports/JSONs. Those long renders
were not repeated after removing the headless gate; the background regression
suite and OptiX before/after checks were rerun on the final build.

F12 remains experimental. Persistent-data sessions, multi-frame interactive
animation, multiple interactive view layers, large capture/spill workloads,
interactive GPU cancellation, and display/context lifetime variations remain
follow-up. Cancellation during final EXR/CSV publication was not fault-injected;
the two files are individually atomic, not an atomic pair. The small tests do
not extend the existing production qualification matrix.

To repeat the interactive test, launch Blender without `-b` or
`--enable-event-simulate`, using `--factory-startup --python
tools/test_blender_deep_interactive.py -- FRESH_OUTPUT_DIRECTORY`. Send ESC to
that Blender window each time `cancel-request.json` changes. Require
`results.json` to contain `passed: true`; a GUI process exiting zero is insufficient.
