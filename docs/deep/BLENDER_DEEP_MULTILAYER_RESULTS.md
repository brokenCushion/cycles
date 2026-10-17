# Blender multi-view-layer deep output verification

2026-10-18. Option A1. Branch `blender-deep-multilayer`.
Status: **adapter implemented and built; full acceptance incomplete**.
CPU/OptiX fixtures, UI and migration pass. Cancellation could not be delivered
through Windows native input. Junkshop fails an existing Hair BSDF core guard.
The requested all-tests-pass condition has not been met.

## Change

The host adapter resolves `{view_layer}` through the same helper for preflight
and driver setup. It rejects colliding resolved paths across all deep-enabled
layers, including disabled and Single Layer-skipped layers. Windows comparison
normalizes paths and ignores case. Single-layer naming and the mono guard are
unchanged. The Output Deep panel warns about missing layer tokens.

No files under `src/` changed. No deep RGB, compositor socket, manifest
filtering, new EXR header, or capture-lifetime change was introduced.
The existing untracked scene assets were not included in the commit.

Files changed:

- `tools/prepare_blender_deep.py`: shared path resolver, layer variable,
  collision preflight, UI warning and tooltip.
- `tools/test_blender_deep_multilayer.py`: fixture, CPU/OptiX tests, numerical
  validation and foreground Single Layer/cancellation checks.
- `tools/test_blender_deep_ui.py`: collision/success cases and warning checks.
- `tools/review_blender_deep_multilayer.py`: numeric production review helper;
  syntax checked, but not executed because the production render failed.
- `docs/deep/BLENDER.md`, `docs/deep/SUPPORT.md`,
  `docs/plans/BLENDER_DEEP_MULTILAYER.md`, and this report.

## Build and environment

All generated sources, build/install files, logs, renders and temporary/cache
outputs live under `D:/temp/cycles_deep_multilayer/`. The existing qualified
build and `D:/CyclesDeepScratch/` were read-only inputs.

The retained `D:/temp/cycles_deep_multilayer/build_guarded.py` imports the x64
Visual Studio 2022 Community environment and redirects `TEMP`, `TMP`, `TMPDIR`,
`CUDA_CACHE_PATH`, `OPTIX_CACHE_PATH`, Python bytecode and Blender user resources
under this output root. It uses clang-cl 20.1.8, Ninja, CUDA 12.8.0, MSVC 14.44
as NVCC's host compiler, OptiX 8.0 and `sm_86`.

Commands run from the repository (CMake commands are the helper's expanded
arguments, with Windows separators shown as forward slashes):

```powershell
git switch -c blender-deep-multilayer
git -C builds/blender/source worktree add D:/temp/cycles_deep_multilayer/blender-src 749518deb2f0735a22361a07488b35f7ea5c2fdf
python tools/prepare_blender_deep.py --source D:/temp/cycles_deep_multilayer/blender-src --check
python tools/prepare_blender_deep.py --source D:/temp/cycles_deep_multilayer/blender-src
python D:/temp/cycles_deep_multilayer/build_guarded.py configure
python D:/temp/cycles_deep_multilayer/build_guarded.py build
```

```text
cmake -S D:/temp/cycles_deep_multilayer/blender-src -B D:/temp/cycles_deep_multilayer/build -G Ninja -C C:/Users/jun/Documents/ChatGPT/cycles_deep/tools/blender_deep_build.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=D:/CyclesDeepScratch/optimization-phase8a/clangcl/llvm-20.1.8/bin/clang-cl.exe -DCMAKE_CXX_COMPILER=D:/CyclesDeepScratch/optimization-phase8a/clangcl/llvm-20.1.8/bin/clang-cl.exe -DCMAKE_C_FLAGS=-m64 -DCMAKE_CXX_FLAGS=-m64 -DLIBDIR=C:/Users/jun/Documents/ChatGPT/cycles_deep/lib/windows_x64 -DCMAKE_INSTALL_PREFIX=D:/temp/cycles_deep_multilayer/install -DCMAKE_MAKE_PROGRAM=D:/CyclesDeepScratch/optimization-phase8a/clangcl/ninja/ninja.exe -DOPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCYCLES_RUNTIME_OPTIX_ROOT_DIR=D:/CyclesDeepScratch/optix-dev-8.0 -DCUDA_TOOLKIT_ROOT_DIR=D:/CyclesDeepScratch/optimization-phase8a/clangcl/cuda-12.8.0 "-DCUDA_HOST_COMPILER=C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" -DCYCLES_CUDA_BINARIES_ARCH=sm_86 -DWITH_CYCLES_DEEP_OPAQUE=ON
cmake --build D:/temp/cycles_deep_multilayer/build --target install --parallel 24
```

Overlay output: `Validated overlay anchors and 1105 files; no source changes written`.
Configure and build/install both exited zero. Full logs: `verification/configure.log` and
`verification/build.log` under the output root.

## Tests

The retained `run_checks.py` uses the same redirected environment. Each mode
records its exact Blender/Python command in `verification/<mode>-command.json`
and stdout/stderr in `verification/<mode>.log`. The OpenEXR validator uses the
existing read-only `D:/CyclesDeepScratch/regression-tools/Scripts/python.exe`.

```powershell
python D:/temp/cycles_deep_multilayer/run_checks.py baseline
python D:/temp/cycles_deep_multilayer/run_checks.py cpu
python D:/temp/cycles_deep_multilayer/run_checks.py optix
python D:/temp/cycles_deep_multilayer/run_checks.py ui
python D:/temp/cycles_deep_multilayer/run_checks.py migration
python D:/temp/cycles_deep_multilayer/run_checks.py panel
python D:/temp/cycles_deep_multilayer/run_checks.py cancel
python D:/temp/cycles_deep_multilayer/run_checks.py production
python tools/check_markdown_links.py
```

| Required check | Result | Actual evidence |
| --- | --- | --- |
| Build / clean overlay | PASS | 1,105 overlay files; `configure: exit=0`; `build: exit=0`. |
| 1. CPU isolation | PASS | A, B and C whole-file bytes match combined versus separate renders; `tests.isolation = byte-identical`. |
| 2. Single-layer regression | PASS | Same `single/shot_0001.deep.exr` name and bytes as the qualified build. SHA-256 `a18a9745f12023d477b8ad4e51ecd2879f8e77f503ef7f38446a3a5c565c9dc5`. |
| 3. Ownership | PASS | All 24 CPU and 16 OptiX files pass native UINT manifest/hash/visibility checks. A omits indirect-only samples; B omits excluded samples and manifest entry; only A marks Holdout. |
| 4. Depth / alpha | PASS | Existing `check_deep` and `curve_error` helpers pass; both probe pixels contain multiple samples. Maximum fixture opacity-curve error is 0 on CPU and OptiX. Plane depths are checked within `1e-5` before snapping one-ULP intersection rounding for the discontinuous opacity oracle. |
| 5. Collision preflight | PASS | Blank, token-free, Single Layer, disabled-layer and Windows A/a collision cases reject with the requested message and create no destination directory. Disabled layers with unique paths pass. Foreground Single Layer also rejects collisions; a unique-path foreground run produces only A while B/C remain render-enabled. |
| 6. Mixed layers | PASS | Deep plus normal fixture succeeds; existing UI test confirms both Combined channel sets in the flat multilayer EXR. |
| 7. Animation | PASS | Nine correctly numbered deep files for A/B/C on frames 1–3, on each backend. |
| 8. Cancellation during B | BLOCKED / NOT PASSED | Two native Escape attempts failed with `GetCursorPos failed: Access is denied. (0x80070005)`. The render completed instead: `complete=1, cancel=0`; B/C files exist. This is not cancellation evidence. |
| 9. Existing UI / panel / migration | PASS | `DEEP_UI_TESTS_PASS`; `DEEP_MIGRATION_TESTS_PASS`; panel exits zero, records three screenshots, and preserves strict-mode IDs. Alpha-off/on and strict screenshots were inspected. |
| 10. OptiX smoke | PASS | `DEEP_MULTILAYER_RENDER_PASS OPTIX 16 files`; `DEEP_MULTILAYER_VALIDATION_PASS OPTIX 16 files`. No GPU byte-identity claim. |
| 11. Junkshop smoke | FAIL, existing core limitation | `production exit 1`; `Deep: unsupported deep surface node: hair_bsdf`. Frame 1 fails before publication; frame 2 is not reached. |
| Production OpenEXR / Gaffer review | NOT RUN | No production EXRs exist. The Gaffer environment/import probe passed, but it is not a reader or review test. |
| Markdown links | PASS | Final link-check output is retained in `verification/markdown-links.json`; no broken links. |

Key successful output lines:

```text
DEEP_MULTILAYER_RENDER_PASS CPU 24 files
DEEP_MULTILAYER_VALIDATION_PASS CPU 24 files
DEEP_MULTILAYER_RENDER_PASS OPTIX 16 files
DEEP_MULTILAYER_VALIDATION_PASS OPTIX 16 files
DEEP_UI_TESTS_PASS D:\temp\cycles_deep_multilayer\verification\ui
DEEP_MIGRATION_TESTS_PASS D:\temp\cycles_deep_multilayer\verification\migration
```

Representative actual collision output:

```text
Deep layers A, a would all write D:\temp\cycles_deep_multilayer\verification\cpu\collision-case\A_0001.deep.exr; add {view_layer} to Output > Deep > Path Override
```

Detailed successful results are `verification/cpu/validation.json`,
`verification/optix/validation.json`, `verification/ui/results.json`,
`verification/migration/results.json` and `verification/panel/results.json`.
Final CPU and OptiX suites were rerun after the fixture's GUI context correction.
Earlier attempts are retained: the initial strict-boundary oracle needed a
one-ULP depth correction; GUI fixture setup needed the active view-layer object;
the first cancellation workload finished too quickly; a higher-sample retry
hit the existing 4,096-sample limit. The final fixture uses 512x512, 4,096 beauty
samples, deep8, 2,048 MiB and one sample for A/C. It still requires working native
Escape input. GUI exit zero alone is explicitly insufficient.

## Produced EXRs

All paths below are under `D:/temp/cycles_deep_multilayer/`:

- `verification/cpu/all/{A,B,C}_0001.deep.exr`
- `verification/cpu/alone-{A,B,C}/{A,B,C}_0001.deep.exr` (matching layer in each directory)
- `verification/cpu/animation/{A,B,C}_{0001,0002,0003}.deep.exr`
- `verification/cpu/single/shot_0001.deep.exr` and
  `verification/baseline/single/shot_0001.deep.exr`
- `verification/optix/all/{A,B,C}_0001.deep.exr`, corresponding `alone-*` files,
  `single/shot_0001.deep.exr`, and nine files under `verification/optix/animation/`.
- Additional path/mixed fixtures are enumerated in each `results.json`;
  existing UI outputs are enumerated in `verification/ui/results.json`.
- `verification/exr-inventory.json` lists every produced EXR by absolute path,
  including earlier attempts and unqualified cancellation outputs.

**Junkshop produced zero deep EXRs and zero flat File Output EXRs.** There are
therefore no actual production flat filenames to report. The retained
`junkshop/render.json` records the three redirected nodes (`bg`, `girl`,
`robots`) and their original `{blend_name}_{node_name}` filename templates.
Its intended deep destination was
`junkshop/render/v001/{blend_name}_{view_layer}_####` (an absolute redirection of
the requested blend-relative template). Beauty and all File Output directories
were redirected to the same new output area without saving the scene.

The smoke used OptiX, 10%, four samples and requested frames 1–2. It also supplied
a `0.01` world-unit step for the existing nongrid procedural smoke, 4,096 events
and a 2,048 MiB deep memory budget. It did not alter geometry or materials. The
source `.blend` SHA-256 was checked after failure and remains unchanged.

## Remaining blockers

The shader whitelist at [src/session/deep.cpp](../../src/session/deep.cpp)
rejects `hair_bsdf`. Supporting the unchanged junkshop scene requires reviewing
and extending deep shader support under `src/`, outside this task's approved
host-only scope. Work stopped at that boundary; the guard was not bypassed and
the scene's materials were not replaced to obtain a passing result.

Cancellation must be rerun with Windows native input available. The foreground
Single Layer subtests passed, but the layer-B cancellation acceptance criterion
remains open. Production OpenEXR and Gaffer checks can only run after the shader
blocker is resolved. The prepared `production-validate` and `gaffer` runner
modes were not run and are not counted as passes.

## Reader and qualification limits

`cycles:deepIDHoldoutManifest` remains a subset of `cycles:deepIDManifest`.
The ownership test checks that only held-out objects are marked in that subset;
holdout samples still carry IDs present in the complete manifest. Removing
those IDs from the complete manifest would violate the existing file contract.

The documented `validate_blender_deep_gaffer.py` requires strict surface-only
CSV diagnostics (`Z == ZBack`). Numeric junkshop output with procedural smoke
instead uses `tools/review_blender_deep_multilayer.py`: the same Gaffer reader,
DeepSlice, DeepToFlat and DeepToPointCloud approach, plus per-frame DeepMerge.
This is an interoperability smoke check, not a new volume-integration accuracy
qualification. A four-times-finer procedural smoke reference is not part of
this task.

Nuke DeepRead, DeepRecolor and DeepMerge cannot be tested here and were not
tested. No Nuke compatibility claim is made.
