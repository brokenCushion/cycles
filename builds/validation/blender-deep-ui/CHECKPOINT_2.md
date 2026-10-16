# Checkpoint 2: passed

The Deep controls now follow agreed layout B. Enable is on the view layer;
shared settings and opt-in diagnostics remain on the scene. The Output panel
contains only the optional path override. Blender-side changes are generated
by `tools/prepare_blender_deep.py`.

## Build and boundaries

- Branch: `blender-deep-ui`, based on `deep-output`.
- Agreed plan moved unchanged and committed: `5b510f900b4acce6c4e314db4bee21d42a111c3c`.
- Implementation and tests remain uncommitted local changes for review.
- Blender pin: `749518deb2f0735a22361a07488b35f7ea5c2fdf`.
- Clean-worktree overlay check passed for 1,029 files. Generated source hashes
  also match the final overlay manifest.
- Full Release build/install passed with clang-cl 20.1.8, CUDA 12.8 and OptiX 8,
  including CUDA sm_86 and OptiX/OSL kernels.
- Executable: `D:/temp/cycles_deep_blender_ui/install/blender.exe`.
- Executable SHA-256: `4e27785d276176bdd41edcacec379b080109433f31343c2f140f965cb3666f96`.
- The original `builds/blender/source` checkout was not modified.
- Scratch was used read-only for the authorized toolchain. Build/install/temp
  and redirected caches are guarded under this task's D:/temp root. The existing
  Windows GUI shader cache was used with the user's explicit exception.
- No pushes, PRs, releases or external publication.

## Verification

| Check | Result | Evidence in this directory |
|---|---|---|
| Clean adapter anchors | Pass, 1,029 files | `overlay-check.log` |
| Full build and install | Pass | `configure.log`, `build.log` |
| Deep immediately below Cryptomatte | Pass, visually inspected | `gui-runtime/view-layer-on.png` |
| Settings disabled when off; shared label; background note when on | Pass, visual and draw assertions | `gui-runtime/view-layer-off.png`, `ui-runtime/results.json` |
| Output Deep visible only when current layer enables Deep | Pass | `gui-runtime/output-on.png`, `output-off.png`, draw assertions |
| Old Render/Film Deep panel removed | Pass, old class absent and registration/draw checks | `ui-runtime/results.json`, `gui-runtime/render-tab.png` |
| Two-frame blank-override paths with hashes, no hashes and relative paths | Pass; separate beauty/Deep files for each frame | `ui-runtime/results.json` |
| Override paths, hashes and no hashes; file extension disabled | Pass | `ui-runtime/results.json` |
| CSV default off, explicit on, surface and volume branches | Pass; volume Deep identity unchanged by CSV switch | `ui-runtime/results.json` |
| Validation helper explicitly enables diagnostics | Pass; both qualified renders produced CSV | `after`, `after-strict` |
| Two Deep layers rejected with both names | Pass | `ui-runtime/results.json` |
| One Deep layer plus ordinary layer | Pass; both beauty layer channels verified | `ui-runtime/results.json` |
| Mono restriction | Pass | `ui-runtime/results.json` |
| Optional null view-layer session argument | Compiled all callers; only final render supplies layer | adapter and build logs |
| Viewport with Deep enabled | Pass; rendered viewport displays Rendering Done | `gui-runtime/viewport-rendered.png` |
| Bake with Deep enabled | Pass; expected linear pixels, no Deep output | `ui-runtime/results.json`, `bake-probe.json` |
| F12 warning and no Deep file | Pass; beauty completed, warning visible, no EXR/CSV | `gui-runtime/results.json`, `f12-reports.png`, `gui-runtime.log` |
| Old saved file migration | Pass; all settings/path preserved, disabled layer unchanged | `migration/results.json` |
| Migration idempotence and explicit disable survives reload | Pass | `migration/results.json` |
| Strict and numeric qualified-scene byte identity | Pass, entire EXR files identical | `identity-strict.json`, `identity-numeric.json` |
| Unchanged Gaffer strict-surface validator | Pass | `after-strict/gaffer_validation.json`, `gaffer-runtime.log` |
| Python syntax and diff whitespace | Pass | Final local checks |

## Exact identity

The qualified Blender 3.5 splash scene was rendered at 664 x 625, 128 samples,
CPU, eight threads, with its original DOF and adaptive sampling. Before/after
settings differ only in the executable hash. The unchanged identity checker
reports no changed deterministic attributes or run metadata. Whole-file SHA-256
also matches, which is stronger than the required payload/header identity rule.

| Mode | Bytes | Identical before/after whole-file SHA-256 |
|---|---:|---|
| Numeric (error 0.001) | 82,469,567 | `fb8147f5aa7795762861186b49a3716b17029907b3cb0755ebc786f61d88058d` |
| Strict | 244,454,070 | `4ab129d6a8ca1a9d1377cef31333e7b6a17ea4422512c059741fb3b700d2191e` |

Gaffer checked 81 grid pixels and 21,064 boundaries. Maximum curve error was
3.0177335630376945e-09; maximum slice error was zero. One million points were
displayed by the unchanged review validator.

## Fixes and limits

The compiler caught missing `std::` qualification on two new diagnostics
`unique_ptr` declarations. That was fixed through the adapter, then the clean
check and build were rerun. No rendering core source changed.

The new bake test initially created an 8-bit sRGB image while asserting linear
pixel values. An independent probe showed identical Deep-on/off behavior and
correct float-image output. With explicit user approval, only the fixture was
changed to `float_buffer=True`; assertion values and tolerance stayed unchanged.
The original failing run and probe remain in this directory.

The legacy Gaffer surface validator requires Z == ZBack and already rejected
the original numeric baseline. It was not altered or relaxed; strict output
passed before and after, while numeric output was independently confirmed
whole-file byte-identical. GPU runtime and the wider support matrix were not
requalified for this UI move.

Checkpoint 3 findings are in `F12_FEASIBILITY.md`. No F12-support implementation
was added; the agreed background-only behavior was verified.
