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

## Step 1: camera-visibility preflight

2026-10-18. Core preflight fix implemented and tested. **Junkshop remains blocked**
by a different existing shader guard. This section supersedes the earlier Hair
BSDF diagnosis for the BG + Robots smoke; the earlier results remain historical.

### Scope and visibility mapping

Only `src/session/deep.cpp` changed in renderer code. A shared helper collects
geometry referenced by at least one object with `PATH_RAY_VISIBILITY_CAMERA`.
A camera-visible instance therefore keeps shared geometry in validation.
The four changed validation loops are:

- Geometry validation in `validate_deep_scene`: skip geometry outside that set,
  because camera-only capture cannot record it. Mesh/subdivision/motion and
  material checks remain unchanged for reachable geometry.
- Object transform/motion/shadow-catcher/caustics validation: skip objects without
  the camera bit, because those object properties cannot affect captured events.
- OSL used-shader collection in `validate_deep_osl`: only collect shaders from
  camera-reachable geometry, retaining the existing analytic-light skip.
- OSL stable-attribute geometry scan: apply the same set so an invisible user of
  a shared shader cannot reject an otherwise valid camera-visible user.

The helper's object scan is the only new loop. ID and holdout manifests,
volume_grid / volume_shader_evaluation detection, volume population bounds and
material-step metadata loops are unchanged. Keeping the metadata loops unchanged
preserves existing step selection and headers. No adapter, capture, kernel,
export, EXR header or material allowlist changes were made.

Code mapping verified in the pinned generated Blender tree:

- `intern/cycles/blender/object.cpp:228`: indirect-only clears
  `PATH_RAY_VISIBILITY_CAMERA`, except for holdouts; line 286 stores visibility.
- `source/blender/makesrna/intern/rna_object.cc:2894`: `visible_camera` is the
  negative `OB_HIDE_CAMERA` flag; `intern/cycles/blender/util.h:726` maps that flag
  to `PATH_RAY_VISIBILITY_CAMERA`.
- `source/blender/blenkernel/intern/layer.cc:1087`: excluded collections return
  before syncing their objects. An object also linked into an included collection
  can still be present.
- Holdout alone does not remove camera visibility. Both shared geometry and
  holdout rejection are covered by the new fixture.
- `src/kernel/deep/surface_cuda.h:34` and
  `src/kernel/device/cpu/kernel_arch_impl.h:170` intersect with this exact mask;
  OptiX `kernel_deep.cu` includes and calls the shared surface implementation.

Other changed files: `tools/test_blender_deep_visibility.py` adds the seven-case
fixture; `docs/deep/SUPPORT.md` and `docs/deep/BLENDER.md` document the contract;
this report records verification. The existing multilayer suite and adapter were
not modified. Untracked user scene/VDB assets are not part of the commit.

### Build and commands

Generated outputs remain under `D:/temp/cycles_deep_multilayer/`; the qualified
build and `D:/CyclesDeepScratch/` are read-only inputs. The existing
`build_guarded.py` redirects temporary files, caches and Blender user resources.
The generated Blender checkout HEAD was verified as
`749518deb2f0735a22361a07488b35f7ea5c2fdf` before resetting it.

```powershell
git -C D:/temp/cycles_deep_multilayer/blender-src checkout -- .
git -C D:/temp/cycles_deep_multilayer/blender-src clean -fd
python tools/prepare_blender_deep.py --source D:/temp/cycles_deep_multilayer/blender-src --check
python tools/prepare_blender_deep.py --source D:/temp/cycles_deep_multilayer/blender-src
python D:/temp/cycles_deep_multilayer/build_guarded.py build
```

`rebuild_step1.py` retained hashes/timestamps before the reset, then restored
1,142 unchanged generated-source timestamps after applying the overlay. The
incremental build recompiled session `deep.cpp` and build information, linked,
and installed; it did not rebuild the capture kernels. Overlay check:
`Validated overlay anchors and 1105 files; no source changes written`.

The first install was blocked by an open Blender executable (`Permission denied`).
After the user closed it, the same guarded build/install succeeded:
`build: exit=0`. The first log is `verification/step1/build-locked.log`;
the final log is `verification/build.log`. No process was forcibly closed.

The standalone project used a fresh `deep-reference-step1` directory, Ninja,
MSVC from the guarded x64 environment, Release and `BUILD_TESTING=ON`:

```text
cmake -S C:/Users/jun/Documents/ChatGPT/cycles_deep/src/deep -B D:/temp/cycles_deep_multilayer/deep-reference-step1 -G Ninja -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=D:/CyclesDeepScratch/optimization-phase8a/clangcl/ninja/ninja.exe
cmake --build D:/temp/cycles_deep_multilayer/deep-reference-step1
ctest --test-dir D:/temp/cycles_deep_multilayer/deep-reference-step1 -C Release --output-on-failure
```

Actual toolchain commands/log: `verification/step1/ctest.log`.
`100% tests passed, 0 tests failed out of 3`; test time 8.08 seconds.

```powershell
python D:/temp/cycles_deep_multilayer/run_step1.py baseline
python D:/temp/cycles_deep_multilayer/run_step1.py visibility
python D:/temp/cycles_deep_multilayer/run_step1.py cpu
python D:/temp/cycles_deep_multilayer/run_step1.py optix
python D:/temp/cycles_deep_multilayer/run_step1.py ui
python D:/temp/cycles_deep_multilayer/run_step1.py migration
python D:/temp/cycles_deep_multilayer/run_step1.py panel
python D:/temp/cycles_deep_multilayer/run_step1.py production
python tools/check_markdown_links.py
```

`run_step1.py` records exact arguments in `verification/step1/*-command.json`.
All successful tests used the newly installed executable except `baseline`,
which reran the qualified `D:/temp/cycles_deep_blender_ui/install/blender.exe`.

### Actual test results

| Check | Result | Evidence |
| --- | --- | --- |
| Standalone core CTests | PASS | Capture, reconstruction, analytic density cells: 3/3. |
| Indirect-only hair in A | PASS | Particle hair and Hair BSDF remain synced; A publishes deep. |
| Same hair camera-visible in B | PASS (expected rejection) | `Deep: unsupported deep surface node: hair_bsdf`. |
| Object Camera off / on | PASS | Off publishes; on rejects with the same `hair_bsdf` message. |
| Shared mesh, invisible and visible users | PASS (expected rejection) | Shared datablock without particle modifiers still rejects `hair_bsdf`. |
| Holdout hair material | PASS (expected rejection) | Camera-visible holdout rejects `hair_bsdf`. |
| Excluded hair material | PASS | Excluded collection publishes deep. |
| New successful EXRs | PASS | Existing `check_deep` validates all three files; `DEEP_VISIBILITY_OPENEXR_PASS 3 files`. |
| Full CPU multilayer suite | PASS | `DEEP_MULTILAYER_RENDER_PASS CPU 24 files`; `DEEP_MULTILAYER_VALIDATION_PASS CPU 24 files`. Includes isolation, collision, mixed-layer, animation, IDs, holdouts and opacity/depth checks. |
| Qualified single-layer bytes | PASS | Exact bytes and name against newly rerun qualified baseline; SHA-256 `a18a9745f12023d477b8ad4e51ecd2879f8e77f503ef7f38446a3a5c565c9dc5`. |
| Previous multilayer CPU bytes | PASS | All 24 whole files match the previous build: `PRIOR_CPU_BYTE_IDENTITY_PASS 24 files`. |
| OptiX smoke | PASS | `DEEP_MULTILAYER_RENDER_PASS OPTIX 16 files`; `DEEP_MULTILAYER_VALIDATION_PASS OPTIX 16 files`. No GPU byte-identity claim. |
| UI tests | PASS | `DEEP_UI_TESTS_PASS`; existing path, mixed output, collision and setting-identity checks pass. |
| Migration | PASS | `DEEP_MIGRATION_TESTS_PASS`; all five saved migration/preservation assertions true. |
| Panel | PASS | Exit zero; three screenshots visually inspected; strict mode preserves saved IDs. |
| Junkshop | FAIL / STOPPED | Existing Light Path output guard described below. |

The visibility fixture's initial run omitted a world shader and failed with
`Deep: surface shader must be connected`. The fixture was corrected by adding
its world, without changing renderer validation. That initial attempt is retained
as `verification/step1/visibility-fixture-no-world*`; the final seven-case run is
`visibility/results.json` (`DEEP_VISIBILITY_TESTS_PASS 7 cases`). CPU prior-build
identity results are in `prior-cpu-identity.json`. All these paths are relative
to `verification/step1/` unless explicitly stated otherwise.

### Junkshop result and stop boundary

The source was opened with `--background --disable-autoexec` and never saved.
BG and Robots deep were enabled; Girl deep was disabled while its flat render
remained enabled. Beauty and all three File Output directories were redirected
under `junkshop2/render/v001/`; the deep override was
`D:/temp/cycles_deep_multilayer/junkshop2/render/v001/{blend_name}_{view_layer}_####`.
Settings were OptiX, 10%, four samples, requested frames 1-2, volume step 0.01,
4,096 events and 2,048 MiB. Materials and geometry were untouched.

Frame 1 returned this exact error:

```text
Deep: unsupported deep surface Light Path output
```

Read-only scene inspection identified the sole unsupported Light Path socket:
**layer BG, object `Cristal`, material `Cristal`, collection `Accesories`, node
`Light Path`, output `Is Reflection Ray` connected to `Math:Value`**. The object
has camera visibility in BG. The current guard permits Is Camera Ray, Is Shadow
Ray, Ray Length and Ray Depth, and rejects this output. Inspection evidence:
`junkshop2/blocker-inspection.json`. No guard was bypassed, material edited,
second render attempted or frame 2 started.

Blender continued far enough within the single frame-1 render call to publish
one file before returning the error:

```text
D:/temp/cycles_deep_multilayer/junkshop2/render/v001/thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr
```

It is 116,796 bytes and retained as **unqualified partial-run output**.
No BG deep, flat beauty or flat File Output EXRs were produced. The expected
four deep and six compositor EXRs were not achieved. Publication is per file,
so a failing layer does not imply zero files from the entire render call.

Source SHA-256 before/after the render and after read-only inspection:
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`.
The failure report records `source_unchanged: true` in `junkshop2/render.json`.

Production OpenEXR qualification, Gaffer review and BG + Robots DeepMerge were
not run because the required smoke failed and work stopped at the requested
guard boundary. The review helper was not changed or qualified for two layers.
Nuke is unavailable and was not tested. Cancellation remains unverified from
the earlier task and was not rerun. The new visibility fixture exercises SVM;
OSL filtering compiled successfully but no dedicated OSL visibility fixture was
run. These fixture results do not establish arbitrary-scene qualification.

Markdown link check: **PASS**, 34 documents, 194 local links, zero broken links;
41 external links were not checked. Results: `verification/step1/markdown-links.json`.

## Step 2: scanner and allowlist

2026-10-18. Scanner commit (no changes under `src/`). The scanner is a
**prediction**, not the render-time authority. It walks each selected layer's
evaluated depsgraph and instances, uses the instancer's holdout/indirect flags
and both object/parent camera visibility, follows group interfaces and muted
bypasses, and separates opacity dependencies from BSDF-only inputs.

Files: `tools/scan_blender_deep_scene.py`, `tools/test_scan_blender_deep_scene.py`,
`docs/deep/BLENDER.md`, this report. The table at the top of the scanner mirrors
`validate_shader` / `deep_closure_can_transmit`; settings and geometry mirror
`validate_deep_scene` and the Blender synchronization code. Volume mode detection
continues to include invisible synchronized geometry, matching the core.

Verification before allowlist expansion: `run_step2.py drift -final` completed
68 scanner/render comparisons with **zero false negatives and one documented
false positive**. `mix_vector-visible` has identical vector A/B inputs and is
folded by Cycles, so the renderer passes while the scanner conservatively BLOCKs
`mix_vector`. Five path predictions also matched actual filenames (frame padding,
no hashes, view layer, blend name, explicit EXR extension). Read-only frame,
layer, filepath and datablock-count assertions passed. Tests include nested
material groups, muted/unreachable nodes, collection instances, holdouts, shared
meshes, Camera off, hair, adaptive subdivision, opacity AO/Bevel and settings.

Key output:

```text
SCANNER_DRIFT_SUMMARY 68 cases 0 false negatives 1 false positives
SCANNER_PATHS_PASS 5
```

Logs and JSON: `D:/temp/cycles_deep_multilayer/verification/step2/drift-final*`.
Earlier attempts are retained: the first used obsolete feature_set RNA; another
needed depsgraph initialization after changing layers; the instance fixture needed
a linked wrapper collection. These were scanner/fixture repairs, not core edits.
The initial two Mix constant-folding false positives were reduced to one by making
the Float test inputs distinct. No failed prediction is counted as a pass.

Unchanged junkshop scan command is recorded in `verification/step2/scan-command.json`.
Process exit **2** means BLOCK. The scanner did not refresh device preferences;
the current background process reports no configured supported GPU backend.
The original source SHA-256 remains
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`.
All BLOCKs and all ignored/WARN entries are in the complete report below.

### Unchanged junkshop, before allowlist expansion

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 1
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr
  BLOCK: requires single CPU, CUDA or OptiX
  WARN: Device enumeration is empty; scanner does not refresh preferences
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
  BLOCK: Cristal / Cristal / Light Path / light_path: unsupported deep surface Light Path output: Is Reflection Ray
  OK-ignored: MrCatPC: indirect-only here
  OK-ignored: Cloth Pipes: indirect-only here
  OK-ignored: Wheel Robot: indirect-only here
  OK-ignored: Cat Helmet: indirect-only here
  OK-ignored: Smoke.001: indirect-only here
  OK-ignored: Area.027: camera-invisible or hidden for render
  OK-ignored: Rim Light Girl 02: camera-invisible or hidden for render
  OK-ignored: Area.020: camera-invisible or hidden for render
  OK-ignored: Area.007: camera-invisible or hidden for render
  OK-ignored: No Uso.001: camera-invisible or hidden for render
  OK-ignored: Converstaion Light: camera-invisible or hidden for render
  OK-ignored: Right Commodore Cat: camera-invisible or hidden for render
  OK-ignored: PO Box Red: camera-invisible or hidden for render
  OK-ignored: No Uso 2: camera-invisible or hidden for render
  OK-ignored: Radio Luz 03: camera-invisible or hidden for render
  OK-ignored: Radio Luz 02: camera-invisible or hidden for render
  OK-ignored: RadioLuz: camera-invisible or hidden for render
  OK-ignored: Rim Light Girl: camera-invisible or hidden for render
  OK-ignored: Shop Frontal Left: camera-invisible or hidden for render
  OK-ignored: No Uso: camera-invisible or hidden for render
  OK-ignored: Bounce Light Left Robot: camera-invisible or hidden for render
  OK-ignored: Bounce Light Cat Head: camera-invisible or hidden for render
  OK-ignored: Bounce Enviroment: camera-invisible or hidden for render
  OK-ignored: Area Red Right: camera-invisible or hidden for render
  OK-ignored: Background Blue: camera-invisible or hidden for render
  BLOCK: Deep layers BG, Girl, Robots would all write C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr; add {view_layer} to Output > Deep > Path Override
Layer Girl: checked | deep path: C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr
  BLOCK: requires single CPU, CUDA or OptiX
  WARN: Device enumeration is empty; scanner does not refresh preferences
  OK-ignored: Background Pipes: indirect-only here
  OK-ignored: Background Pipes Closer: indirect-only here
  OK-ignored: Background Wall: indirect-only here
  OK-ignored: Background Cylinders: indirect-only here
  OK-ignored: Background Cube: indirect-only here
  OK-ignored: Gears: indirect-only here
  OK-ignored: Floor Background: indirect-only here
  OK-ignored: POBox: indirect-only here
  OK-ignored: Store Wall: indirect-only here
  OK-ignored: Ceiling: indirect-only here
  OK-ignored: Furniture: indirect-only here
  OK-ignored: Table cloth: indirect-only here
  OK-ignored: Tools: indirect-only here
  OK-ignored: Ads: indirect-only here
  OK-ignored: Floor Front: indirect-only here
  OK-ignored: Front Pipes: indirect-only here
  OK-ignored: Props: indirect-only here
  OK-ignored: Stoplight: indirect-only here
  OK-ignored: Table: indirect-only here
  OK-ignored: Tickets: indirect-only here
  BLOCK: Face: particle hair curve geometry is unsupported
  BLOCK: Face / Hair / Hair BSDF.001 / hair_bsdf: unsupported deep surface node: hair_bsdf
  BLOCK: Face / Hair / Hair BSDF / hair_bsdf: unsupported deep surface node: hair_bsdf
  OK-ignored: Old Robots: indirect-only here
  OK-ignored: RadioNes: indirect-only here
  OK-ignored: Cristal: indirect-only here
  OK-ignored: Weigth: indirect-only here
  OK-ignored: Old Robots.001: indirect-only here
  OK-ignored: MrCatPC: indirect-only here
  OK-ignored: Cloth Pipes: indirect-only here
  OK-ignored: Wheel Robot: indirect-only here
  OK-ignored: Cat Helmet: indirect-only here
  OK-ignored: Smoke.001: indirect-only here
  OK-ignored: Area.027: indirect-only here
  OK-ignored: Rim Light Girl 02: indirect-only here
  OK-ignored: Area.020: indirect-only here
  OK-ignored: Area.007: indirect-only here
  OK-ignored: No Uso.001: indirect-only here
  OK-ignored: Converstaion Light: indirect-only here
  OK-ignored: Right Commodore Cat: indirect-only here
  OK-ignored: PO Box Red: indirect-only here
  OK-ignored: No Uso 2: indirect-only here
  OK-ignored: Radio Luz 03: indirect-only here
  OK-ignored: Radio Luz 02: indirect-only here
  OK-ignored: RadioLuz: indirect-only here
  OK-ignored: Rim Light Girl: indirect-only here
  OK-ignored: Shop Frontal Left: indirect-only here
  OK-ignored: No Uso: indirect-only here
  OK-ignored: Bounce Light Left Robot: indirect-only here
  OK-ignored: Bounce Light Cat Head: indirect-only here
  OK-ignored: Bounce Enviroment: indirect-only here
  OK-ignored: Area Red Right: indirect-only here
  OK-ignored: Background Blue: indirect-only here
  OK-ignored: Light Block: indirect-only here
  BLOCK: Deep layers BG, Girl, Robots would all write C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr; add {view_layer} to Output > Deep > Path Override
Layer Robots: checked | deep path: C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr
  BLOCK: requires single CPU, CUDA or OptiX
  WARN: Device enumeration is empty; scanner does not refresh preferences
  OK-ignored: Background Pipes: indirect-only here
  OK-ignored: Background Pipes Closer: indirect-only here
  OK-ignored: Background Wall: indirect-only here
  OK-ignored: Background Cylinders: indirect-only here
  OK-ignored: Background Cube: indirect-only here
  OK-ignored: Gears: indirect-only here
  OK-ignored: Floor Background: indirect-only here
  OK-ignored: POBox: indirect-only here
  OK-ignored: Store Wall: indirect-only here
  OK-ignored: Ceiling: indirect-only here
  OK-ignored: Furniture: indirect-only here
  OK-ignored: Table cloth: indirect-only here
  OK-ignored: Tools: indirect-only here
  OK-ignored: Ads: indirect-only here
  OK-ignored: Floor Front: indirect-only here
  OK-ignored: Front Pipes: indirect-only here
  OK-ignored: Props: indirect-only here
  OK-ignored: Stoplight: indirect-only here
  OK-ignored: Table: indirect-only here
  OK-ignored: Tickets: indirect-only here
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
  OK-ignored: Old Robots: indirect-only here
  OK-ignored: RadioNes: indirect-only here
  OK-ignored: Cristal: indirect-only here
  OK-ignored: Weigth: indirect-only here
  OK-ignored: Old Robots.001: indirect-only here
  BLOCK: Smoke.001 / Smoke: shader-evaluated volume without a grid requires volume step
  WARN: Smoke.001 / Smoke / Material Output / output: Volume shader requires render-time extinction qualification. SUPPORT.md: | Geometry | Polygon surfaces/rigid instances; static homogeneous boundaries and native scalar NanoVDB density with linear interpolation. |
  OK-ignored: Area.027: indirect-only here
  OK-ignored: Rim Light Girl 02: indirect-only here
  OK-ignored: Area.020: indirect-only here
  OK-ignored: Area.007: indirect-only here
  OK-ignored: No Uso.001: indirect-only here
  OK-ignored: Converstaion Light: indirect-only here
  OK-ignored: Right Commodore Cat: indirect-only here
  OK-ignored: PO Box Red: indirect-only here
  OK-ignored: No Uso 2: indirect-only here
  OK-ignored: Radio Luz 03: indirect-only here
  OK-ignored: Radio Luz 02: indirect-only here
  OK-ignored: RadioLuz: indirect-only here
  OK-ignored: Rim Light Girl: indirect-only here
  OK-ignored: Shop Frontal Left: indirect-only here
  OK-ignored: No Uso: indirect-only here
  OK-ignored: Bounce Light Left Robot: indirect-only here
  OK-ignored: Bounce Light Cat Head: indirect-only here
  OK-ignored: Bounce Enviroment: indirect-only here
  OK-ignored: Area Red Right: indirect-only here
  OK-ignored: Background Blue: indirect-only here
  OK-ignored: Light Block: indirect-only here
  BLOCK: Deep layers BG, Girl, Robots would all write C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr; add {view_layer} to Output > Deep > Path Override
Not decidable from RNA:
  WARN: Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).
  WARN: RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.
  WARN: Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.
  WARN: Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.
  WARN: Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.
  WARN: Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.
BLOCK findings: 11
```
