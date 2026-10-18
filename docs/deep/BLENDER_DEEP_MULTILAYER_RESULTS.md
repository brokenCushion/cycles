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

### Allowlist candidate: qualification stopped (not committed)

The scanner commit is `77d385d9b`. The second commit was **not created** because
its required opacity gate failed. The working tree and test install contain an
unqualified allowlist candidate; they must not be treated as an accepted build.
No push or PR was made.

Candidate renderer changes are confined to `src/session/deep.cpp`: the following
NODE_DEFINE names were checked in `src/scene/shader_nodes.cpp`, and their native
SVM implementations were read. These are candidate justifications, not a claim
that the failed qualification passed:

| Candidate Cycles node | SVM justification |
| --- | --- |
| glossy_bsdf | Closure setup allocates a BSDF without transparent extinction. |
| metallic_bsdf | Closure setup allocates a BSDF without transparent extinction. |
| refraction_bsdf | Sampled refraction contributes no straight camera transparency. |
| sheen_bsdf | Closure setup does not set transparent extinction. |
| toon_bsdf | Closure setup does not set transparent extinction. |
| subsurface_scattering | BSSRDF setup does not set transparent extinction. |
| hair_bsdf | Mesh hair closure setup contributes zero camera transparency; curves remain rejected. |
| principled_hair_bsdf | Hair randomization uses attributes/inputs rather than path RNG; no transparent extinction. |
| vector_math | Stack-only vector arithmetic in svm/math.h. |
| map_range | Stack-only scalar range mapping in svm/map_range.h. |
| separate_xyz | Stack component extraction in svm/sepcomb_vector.h. |
| combine_xyz | Stack component assembly in svm/sepcomb_vector.h. |
| mix_float | Stack-only scalar interpolation in svm/mix.h. |
| mix_vector | Stack-only vector interpolation in svm/mix.h. |
| mix_vector_non_uniform | Stack-only component interpolation in svm/mix.h. |
| vector_rotate | Stack vectors/angles, no tracing or RNG, in svm/vector_rotate.h. |
| float_curve | Immutable table lookup plus stack inputs in svm/ramp.h. |
| vector_curves | Immutable component table lookup plus stack inputs in svm/ramp.h. |
| normal | Stack normal/dot operations and stored direction in svm/normal.h. |
| blackbody | Deterministic temperature and color-space arithmetic in svm/blackbody.h. |
| wavelength | Deterministic wavelength and color-space arithmetic in svm/wavelength.h. |
| vector_transform | Initialized hit object/time and camera transforms in svm/vector_transform.h. |

The ten candidate Light Path outputs are Is Diffuse Ray, Is Glossy Ray,
Is Singular Ray, Is Reflection Ray, Is Transmission Ray, Is Volume Scatter Ray,
Diffuse Depth, Glossy Depth, Transmission Depth and Portal Depth. Native camera
initialization sets camera visibility, no reflect/singular flag and zero bounce
counters. Transparent continuation preserves those flags/counters. Portal closures
remain rejected, and the portal accessor returns zero when its feature is absent.
The proof uses `svm/light_path.h`, `integrator/path_state.h` and
`integrator/state_util.h`, plus both deep surface implementations.

**Transparent Depth remains rejected.** Surface-only CPU/CUDA/OptiX paths increment
transparent_bounce, matching native transparent continuation. However,
`kernel/deep/volume.h` evaluates surfaces without advancing that counter. A global
allowlist addition is therefore not proven. Supporting it in all modes would need
additional work; no kernel change was made. Ray Portal remains excluded because
it redirects rays. Tier 3/4 nodes and vector_map_range remain outside this expansion.
`deep_closure_can_transmit` is unchanged. One-line comments accompany each proposed
entry, and the remaining Light Path diagnostic now names the rejected output.

Candidate non-core files: scanner rule table; visibility regression changes its
previously rejected mesh Hair BSDF to still-rejected Ray Portal; new
`tools/test_blender_deep_allowlist.py` generates the requested color-only/opacity
and rejection fixtures. Documentation changes and this report are also pending.
No capture/export/adapter changes were made.

The generated overlay was reset at the same pinned HEAD, validated (1,105 files),
reapplied and incrementally built through `rebuild_step2.py` / `build_guarded.py`.
1,142 unchanged timestamps were preserved. Build/install exited 0; logs are
`verification/build.log` and `verification/step2/build-before-expansion.log`.

#### Completed candidate tests and failure

All paths below are relative to `D:/temp/cycles_deep_multilayer/`.

| Test | Actual result |
| --- | --- |
| CPU color-only / opaque closure cases | PASS: 32 node/reference deep EXR pairs are whole-file byte-identical. |
| CPU opacity render cases | 24 rendered, but qualification FAILED at the first alpha comparison; remaining comparisons were not run. |
| Still-rejected cases | PASS: Ray Portal, opacity AO, opacity Bevel, Transparent Depth and particle hair curves all rejected with expected diagnostics. |
| CPU multilayer regression | PASS: 24 files rendered and validated, including isolation and qualified single-layer exact bytes. |
| Prior 16b81d7ef full-output byte comparison | NOT RUN before the stop; do not infer it from single-layer identity. |
| Visibility regression | PASS with the still-unsupported Ray Portal material in place of now-candidate Hair BSDF. |
| UI and migration | PASS: both emit their existing PASS markers. |
| Panel | Script exited 0 and produced three screenshots; this candidate run's screenshots were not visually reviewed before the stop. |
| Standalone CTests | PASS: 3/3, 7.78 seconds, fresh deep-reference-step2 directory. |
| Updated scanner drift, OptiX/CUDA candidate smoke | NOT RUN after failure. |
| Junkshop3 effective-settings scan/render | NOT RUN after failure. |
| Production EXR/Gaffer/DeepMerge, Nuke | NOT RUN; Nuke unavailable. |

Commands/evidence: `run_step2.py CPU`, `run_regression_step2.py` modes baseline,
cpu, visibility, ui, migration, panel, and `ctest_step2.py`. The GPU tests were
not launched. Full commands and logs are in `verification/step2/` and its
`regression/` subdirectory.

```text
DEEP_ALLOWLIST_RENDER_PASS CPU 32 opaque 24 opacity 5 rejections
CPU process exit: 1 (last render was an intentional rejection; results.json completed)
DEEP_MULTILAYER_RENDER_PASS CPU 24 files
DEEP_MULTILAYER_VALIDATION_PASS CPU 24 files
DEEP_VISIBILITY_TESTS_PASS 7 cases
DEEP_UI_TESTS_PASS
DEEP_MIGRATION_TESTS_PASS
100% tests passed, 0 tests failed out of 3
```

The validator initially expected numbered beauty filenames. Blender's
`write_still=True` actually wrote literal `beauty_####.exr` and `off_####.exr`.
That measurement-path bug was repaired without rerendering or changing a gate.
The corrected validator then failed:

```text
case: opacity_vector_math
maximum |flattened deep alpha - beauty alpha|: 0.16430927921169047
allowed deep_error: 0.0010000000474974513
```

The fixture uses two overlapping, partially transparent Diffuse/Transparent
surfaces, 32x32, 16 samples, CPU. Deep alpha ranged 0.672949–0.699683; native beauty
alpha ranged 0.529745–0.844454. Axis flips do not explain the error. The existing
transparency qualification uses an independent per-ray opacity oracle for general
stacks and only compares beauty alpha directly in selected fixtures; its ordinary
transparent stack uses Emission/Transparent. A stochastic beauty-alpha estimator
may explain this fixture mismatch, but **that hypothesis has not been qualified**.
No tolerance was relaxed and no failed render was rerun with altered materials.
Further investigation requires authorization under [VALIDATION.md](VALIDATION.md):
“A failed renderer or gate stops work without retry or further investigation
unless the user explicitly authorizes it.”

The prior junkshop2 run exited **1** with `--python-exit-code 1` and an uncaught
render exception, despite publishing Robots frame 1. This candidate's final
intentional render rejection also left process exit 1 even though Python caught
it. These observations cover those invocations only; no claim is made about every
farm command or every Blender failure mode. No exit behavior was changed.

## Step 2b: opacity gate investigation

2026-10-18. Explicit authorization: `D:/temp/cycles_deep_codex/04_opacity_gate_investigation.md`.
This section supersedes the earlier candidate stop status. All investigation
renders used the **uncommitted candidate**; no renderer code, tolerance, material
allowlist or gate threshold was changed during the investigation. All diagnostic
fixtures are 32x32 CPU, with the requested seed/filter/camera settings. Evidence
is under `D:/temp/cycles_deep_multilayer/verification/step2/gate-investigation/`.
The render script reuses the candidate test's setup and vector-math construction
verbatim. Seven diagnostic renders were run once each; no favorable rerun was
selected. The repeated 16-sample case also serves as the sample-scaling baseline.

### Part A: measured results

1. **Repeatability.** The unchanged deep EXR is whole-file byte-identical:
   SHA-256 `5c85c46dbf07910b09b3826b6f6f2c16e035c9866320a8a821f981f6495d611d`.
   Beauty EXR bytes differ, but **every RGBA pixel is exact**. Header differences
   are Date, RenderTime and the three Cycles synchronization/render/total timing
   attributes. Details: `beauty-repeat-details.json`.
2. **Orientation.** `tools/deep_exr.py:read()` directly uses OpenEXR without a
   flip, just like the flat reader. The adapter converts Blender's internal
   bottom-up rows to EXR file rows with `tile.height - 1 - y`; its diagnostic CSV
   uses those same file coordinates. The separate `image_reader()` helper flips
   for Gaffer-style coordinates, but it is not used by this validator.

| Orientation of beauty | Maximum alpha difference |
| --- | --- |
| Unchanged | 0.164309279212 |
| Vertical flip | 0.166253676367 |
| Horizontal flip | 0.167577684122 |
| Both flips | 0.166864387393 |

3. **Controls.** Every control uses the same setup, paths within its own fresh
   directory, seed and samples. C1 removes the texture and fixes mix factor 0.37;
   C2 removes vector_math; C3 removes Front. No renderer changes were made.

| Candidate CPU case | Samples | Max deep/beauty alpha difference | Mean absolute difference |
| --- | --- | --- | --- |
| Original repeated | 16 | 0.164309279212 | 0.047376570989 |
| C1: constant 0.37 | 16 | 0.192614857578 | 0.046564620501 |
| C2: already-allowed Noise/Math | 16 | 0.169891701382 | 0.047289340803 |
| C3: vector_math, one plane | 16 | 0.056168794632 | 0.013926738262 |
| Original, more samples | 64 | 0.091150821752 | 0.023930058534 |

4. **Independent raw-camera oracle.** Diagnostic CSVs contain 81 grid pixels,
   16 camera samples per pixel (1,296 camera chains), and two events per chain.
   Alpha text is restored to FLOAT before computing
   `1 - mean_s(prod_events(1-alpha))`. Every expected sample is required; gaps,
   duplicate/out-of-order events and missing grid points fail validation.

| Candidate CPU case | (a) Oracle vs beauty max | (b) Deep flatten vs oracle max | (c) Old metric on same grid |
| --- | --- | --- | --- |
| Original vector_math | 0.160394676047 | 1.581115572e-8 | 0.160394660722 |
| C2 Noise/Math | 0.147280883858 | 1.692720408e-8 | 0.147280893352 |

The expected exact oracle/beauty equality in the task's diagnostic hypothesis
**does not hold for these fixtures**. Both already-allowed controls exhibit the
same discrepancy. Deep flattening itself agrees with the independent camera-ray
oracle far inside the unchanged 0.001 bound.

5. **Scaling.** Increasing samples 16 to 64 reduces the max discrepancy from
   0.164309 to 0.091151, and mean absolute discrepancy from 0.047377 to 0.023930
   (approximately half). This is consistent with sampling variation, not a
   fixed vector_math-only bias. One pair is not a general convergence proof.
   Signed mean errors were 0.00169553 and 0.00050014, respectively.

Full numeric results: `diagnosis.json`; per-grid measurements:
`oracle-original-grid.json` and `oracle-C2-grid.json`. Diagnostic Blender process
exit was 0. `command.json` records its exact invocation and
`candidate-identities.json` records executable/source/test hashes.

### Part B: decision and validator repair

**Decision table row 1 applies because C1 and C2 fail the same old metric.**
It does not depend on pretending oracle/beauty equality passed. The mistake was
using native stochastic beauty alpha as the deterministic camera-opacity oracle
for arbitrary Diffuse/Transparent mixtures. The formula used to flatten the
published deep EXR was correct; no axis change or alternative deep flattening
formula was needed.

The repaired `tools/test_blender_deep_allowlist.py` follows the established
`validate_transparency_gaffer.py` contract: compare deep flattening to the raw
camera-chain mixture, preserve exact CPU deep-on/off RGBA equality, and retain
the native-beauty alpha discrepancy as informational evidence. It does not change
`deep_error`, the EXR header bound, renderer, sampling, or materials. Noisy beauty
alpha is not relabeled as passing the 0.001 bound. The earlier still-image filename
repair (`beauty_####.exr`, literal hashes) remains in place.

All **24 opacity cases were rerendered once from the start**, because the preserved
original outputs did not include diagnostic CSVs. All 24 repaired comparisons
passed, with exact CPU deep-on/off RGBA pixels and maximum grid oracle error
**2.4213196336830833e-8**, against unchanged `deep_error = 1e-3`.
The original 32 opaque/color-only byte-identity pairs are preserved, not rerun.

```text
CANDIDATE_ALL24_DIAGNOSTICS_RENDER_PASS 24
DEEP_ALLOWLIST_VALIDATION_PASS CPU 24 opacity cases; max error 2.4213196336830833e-08
```

Evidence: `all24-CPU/validation.json`, `all24-renders.log`, `all24-validation.log`
and `all24-command.json`. Every case's original beauty-alpha discrepancy is kept
in its validation record. This is a measurement repair, not a relaxed threshold.

### Part C: remaining qualification

Candidate CPU prior-build comparison: all 24 complete deep EXRs exactly match
`16b81d7ef`'s saved outputs (`verification/step2/prior-16b81d7ef-identity.json`).
The qualified single-layer comparison, CPU suite, visibility, UI, migration and
3/3 standalone CTests passed in the preceding candidate run; their source and
renderer remain unchanged. The three candidate panel screenshots have now been
visually reviewed: alpha-off/on and strict IDs behave as expected.

The scanner now accepts `--device CPU|CUDA|OPTIX` as a read-only prediction
input. Empty device enumeration yields `WARN: undecidable in background`, without
refreshing, writing or saving preferences. The expanded drift suite checks all
22 new node types and ten new Light Path outputs both visible and indirect-only,
in addition to the prior fixtures. **132 cases, zero false negatives and zero
false positives**. The previous folded vector-Mix false positive disappears
because that type is now allowed. Read-only device-override and five path tests
also pass. Logs: `verification/step2/drift-candidate-final*`.

Candidate OptiX and CUDA qualification also passed: each rendered 32 opaque cases,
24 opacity cases and five expected rejections. GPU byte identity is not claimed.
Maximum deep/oracle errors were **2.2351807293929937e-8** (OptiX) and
**2.421320965950713e-8** (CUDA). The 16-file OptiX multilayer regression also passed.
The final intentional rejection leaves Blender's process exit at 1, while the
completed results.json and validation exit 0 establish the fixture test result;
this is not a silent acceptance of an unexpected render failure.

```text
DEEP_ALLOWLIST_RENDER_PASS OPTIX 32 opaque 24 opacity 5 rejections
DEEP_ALLOWLIST_VALIDATION_PASS OPTIX 24 opacity cases; max error 2.2351807293929937e-08
DEEP_ALLOWLIST_RENDER_PASS CUDA 32 opaque 24 opacity 5 rejections
DEEP_ALLOWLIST_VALIDATION_PASS CUDA 24 opacity cases; max error 2.421320965950713e-08
DEEP_MULTILAYER_VALIDATION_PASS OPTIX 16 files
```

The renderer candidate was not changed to obtain these results. The second
commit includes the unchanged candidate, scanner table/device prediction changes,
expanded drift test, visibility fixture update, oracle validator repair and docs.
Junkshop3 is intentionally run after that commit, as Task 04 specifies; its outcome
is recorded below. These fixtures do not establish arbitrary-scene compatibility.

#### Junkshop3: STOP at compositor output failure

Commit 2 was created before this smoke. Girl deep off, BG + Robots deep on,
10%, four samples, OptiX, frames 1–2, layer-qualified deep override, volume step
0.01, 4096 events and 2048 MiB were applied without saving the source.
Both effective scanner runs had zero BLOCK findings. Full reports:

Frame 1:

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 1
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop3\render\v001\thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
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
Layer Girl: skipped | deep path: -
Layer Robots: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop3\render\v001\thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr
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
Not decidable from RNA:
  WARN: Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).
  WARN: RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.
  WARN: Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.
  WARN: Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.
  WARN: Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.
  WARN: Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.
BLOCK findings: 0
```

Frame 2:

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 2
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop3\render\v001\thejunkshopsplashscreen_deep_v001_BG_0002.deep.exr
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
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
Layer Girl: skipped | deep path: -
Layer Robots: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop3\render\v001\thejunkshopsplashscreen_deep_v001_Robots_0002.deep.exr
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
Not decidable from RNA:
  WARN: Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).
  WARN: RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.
  WARN: Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.
  WARN: Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.
  WARN: Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.
  WARN: Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.
BLOCK findings: 0
```

Blender reported three instances of:

```text
ERROR Invalid path template in File Output node. Skipping writing file.
```

The compositor File Output nodes `bg`, `girl`, and `robots` all retain the source
filename template `{blend_name}_{view_layer}`; only their output directories were
redirected. Object/material: not applicable; this is a compositor path blocker
for flat BG, Girl and Robots outputs, not a deep shader rejection.
The deep override resolved and BG + Robots frame 1 were published. The error did
not raise from the frame-1 render operator: the helper advanced to frame 2 before
the buffered log revealed it. The running process was stopped on observing the
error. No scene repair, bypass or rerun was attempted.

Exact files present under `junkshop3/render/v001/` at the stop:

- `beauty_0001.exr` — 3061 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr` — 433119 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0002.deep.exr` — 433119 bytes.
- `thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr` — 116796 bytes.

The smoke is **incomplete and unqualified**. No compositor outputs were written.
Robots frame 2 and beauty frame 2 are absent. OpenEXR and Gaffer validation,
including BG + Robots DeepMerge, were skipped because the render did not pass.
The Gaffer helper remains unchanged. Nuke was unavailable and untested.

Source SHA-256 before and after is identical:
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`.
The source .blend was never saved. Evidence: `junkshop3/render.json`,
`junkshop3/scanner-1.json`, `junkshop3/scanner-2.json` and
`verification/step2/junkshop3-run/production.log` under the output root.

Exit-code finding: the earlier junkshop2 BG failure with Robots publication
exited **1**, with `--python-exit-code 1` and an uncaught render exception. That
specific farm invocation detects the partial frame. Junkshop3 instead returned
**4294967295** after explicit termination; this is not evidence of natural exit
behavior for compositor failures. Its compositor error did not abort frame 1's
render call. No exit behavior was changed.

Stopped at the first observed outside-scope blocker. No push or PR. Remaining
risk: fixture qualification does not establish production-scene compatibility,
and the deep scanner does not validate compositor templates.


## Step 3: path templates and junkshop4

Task 05 completed on `blender-deep-multilayer`, starting at `c514c52dd`.
No renderer/kernel/build changes. No source .blend save, push or PR.

### Scanner and path drift

The scanner validates render.filepath, File Output directory/file-name/per-item
paths, recursively through compositor node groups, and the deep override.
General variables from pinned `source/blender/blenkernel/intern/path_templates.cc`
are `project_name`, `project_root`, `blend_name`, `blend_dir`, `blend_name_lib`,
`blend_dir_lib`; render variables are `resolution_x`, `resolution_y`, `fps`,
`scene_name`, `camera_name`; node variables add `node_name`. The deep override
alone adds `view_layer`. Availability remains context-dependent: for example,
render.filepath has no owning node. Its native `render.frame_path()` API catches
that and unavailable project variables. Unresolvable deep paths remain BLOCKs.

The pinned RNA/Python sources expose `render.frame_path()` validation through
`rna_scene_api.cc`, used without editing properties. No general File Output/deep
validator is exposed, so those checks mirror the C++ token/format grammar:
escaped braces, balanced variable expressions, hash-based numeric formats, and
no format specifiers on string/path values. Deep path prediction now uses that
same grammar rather than Python format-specifier syntax. Multilayer EXR item
names are channels, not per-item paths; single-layer item suffixes are validated.

Ten isolated real-render fixtures matched scanner verdicts: valid `{node_name}`,
invalid native `{view_layer}`, unbalanced brace, valid deep `{view_layer}`, invalid
numeric format, invalid string format, invalid directory, invalid per-item path,
invalid render.filepath, and valid numeric formats. Expected compositor failures
were confirmed from actual log errors; they may not raise from the operator.
Evidence: `verification/step3-path-final/path-drift.json` and per-case logs.
An initial per-item fixture setup used the wrong format-media enum and did not
render; it was corrected to IMAGE before setting OPEN_EXR. All ten final fixtures
ran afresh, with no changed acceptance criterion.

Existing drift regression: **132 cases, zero false negatives, zero false
positives**; read-only device override and all five deep path predictions passed.
Evidence: `verification/step2/drift-step3.log` and `drift-step3/`.

### Junkshop4 scanner reports

Unchanged-source scan confirms BLOCKs for all three File Output nodes (`bg`,
`girl`, `robots`) using `{view_layer}`. Its other deep findings reflect the
unchanged scene, before the authorized launch overrides. Effective scans apply
Girl deep off, BG + Robots deep on, 10%, four samples, frames 1–2, OptiX, volume
step 0.01, 4096 events, 2048 MiB, and the layer-qualified deep override. File
Output nodes are renamed BG/Girl/Robots and use `{blend_name}_{node_name}_####`.
Both effective scans report **zero BLOCKs**.

Unchanged source:

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 1
  BLOCK: File Output node "bg": unknown path variable {view_layer}; it is only valid in Output > Deep > Path Override. Use {node_name} and name the node after the layer.
  BLOCK: File Output node "girl": unknown path variable {view_layer}; it is only valid in Output > Deep > Path Override. Use {node_name} and name the node after the layer.
  BLOCK: File Output node "robots": unknown path variable {view_layer}; it is only valid in Output > Deep > Path Override. Use {node_name} and name the node after the layer.
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: C:\Users\jun\Desktop\cycles_deep\render\v001\0001.deep.exr
  WARN: undecidable in background: device enumeration is empty; preferences unchanged
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
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
  WARN: undecidable in background: device enumeration is empty; preferences unchanged
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
  WARN: undecidable in background: device enumeration is empty; preferences unchanged
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
BLOCK findings: 8
```

Effective frame 1:

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 1
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop4\render\v001\thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
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
Layer Girl: skipped | deep path: -
Layer Robots: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop4\render\v001\thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr
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
Not decidable from RNA:
  WARN: Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).
  WARN: RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.
  WARN: Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.
  WARN: Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.
  WARN: Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.
  WARN: Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.
BLOCK findings: 0
```

Effective frame 2:

```text
Deep scene scanner prediction; render-time validation is the authority.
Source: C:\Users\jun\Desktop\cycles_deep\thejunkshopsplashscreen_deep_v001.blend | frame 2
Layer Completo: skipped | deep path: -
Layer Viewport: skipped | deep path: -
Layer BG: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop4\render\v001\thejunkshopsplashscreen_deep_v001_BG_0002.deep.exr
  OK-ignored: Face: indirect-only here
  OK-ignored: Eyes: indirect-only here
  OK-ignored: Hat: indirect-only here
  OK-ignored: Body: indirect-only here
  OK-ignored: Vest: indirect-only here
  OK-ignored: Gloves: indirect-only here
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
Layer Girl: skipped | deep path: -
Layer Robots: checked | deep path: D:\temp\cycles_deep_multilayer\junkshop4\render\v001\thejunkshopsplashscreen_deep_v001_Robots_0002.deep.exr
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
Not decidable from RNA:
  WARN: Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).
  WARN: RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.
  WARN: Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.
  WARN: Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.
  WARN: Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.
  WARN: Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.
BLOCK findings: 0
```

### Render, files and validation

`production4.py` under the output root requires all three flat and both deep
outputs after each frame before advancing. A missing file raises immediately
and records the failure in render.json. Both frames passed and background Blender
exited **0**, without File Output errors. The main beauty path was expanded to
an explicit frame filename at launch because write_still uses it literally.

Exact files under `D:/temp/cycles_deep_multilayer/junkshop4/render/v001/`:

- `beauty_0001.exr` — 3061 bytes.
- `beauty_0002.exr` — 3061 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr` — 433119 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0001.exr` — 240715 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0002.deep.exr` — 433119 bytes.
- `thejunkshopsplashscreen_deep_v001_BG_0002.exr` — 240695 bytes.
- `thejunkshopsplashscreen_deep_v001_Girl_0001.exr` — 17066 bytes.
- `thejunkshopsplashscreen_deep_v001_Girl_0002.exr` — 17055 bytes.
- `thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr` — 116796 bytes.
- `thejunkshopsplashscreen_deep_v001_Robots_0001.exr` — 46245 bytes.
- `thejunkshopsplashscreen_deep_v001_Robots_0002.deep.exr` — 116796 bytes.
- `thejunkshopsplashscreen_deep_v001_Robots_0002.exr` — 46257 bytes.

All **four deep EXRs passed** numeric OpenEXR checks. Each is **200 × 100**, equal
to its corresponding BG/Robots flat file. Each BG file has 77,434 deep samples;
each Robots file has 15,492. Evidence: `junkshop4/openexr_validation.json`.

The Gaffer review helper now merges the actual frame's layer list instead of
assuming three layers. Both BG + Robots DeepMerges passed, as did DeepToFlat,
DeepSlice, point-cloud construction and saved-review reload for all four files.
Maximum reader flatten error: **1.1980368497255256e-7**; maximum depth-slice error:
**7.450580596923828e-9**, below the unchanged 0.001 bound. Each file was checked
at 72 grid pixels. Evidence: `junkshop4/gaffer_validation.json` and
`junkshop4/blender_deep_multilayer_review.gfr`. Validation and review processes
both exited 0.

```text
SCANNER_DRIFT_SUMMARY 132 cases 0 false negatives 0 false positives
SCANNER_DEVICE_OVERRIDE_READ_ONLY_PASS
SCANNER_PATHS_PASS 5
JUNKSHOP_MULTILAYER_PASS
JUNKSHOP_OPENEXR_PASS 4 files
MULTILAYER_GAFFER_REVIEW_PASS 4 files; DeepMerge frames 1-2
```

Source SHA-256 before and after:
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`.
The .blend was never saved. Render commands/logs live under
`verification/step2/junkshop4-run/`; scanner reports and render manifest are in
`junkshop4/`. Markdown links and whitespace checks passed before commit.

Skipped/unverified: Nuke is unavailable; no interactive visual Gaffer review or
higher-resolution/sample qualification was requested. Gaffer checks reader
interoperability, not procedural-volume accuracy against a finer-step reference.
The source scene still contains its original invalid flat templates because all
corrections were launch-only; future launches must apply the same overrides or
the user must save corrected node settings. Stopped after this report.

## Step 4: Ribbon hair (M0–M2) and per-layer overrides

### Part A candidate: STOP at first new validation gate (2026-10-18)

Started at `427d5185e` on `blender-deep-multilayer`. Part A is incomplete and
uncommitted. Parts B–E were not started. No push or PR; no junkshop source save.

Candidate files changed:

- `tools/prepare_blender_deep.py`: layer Volume SCENE/ON/OFF and tolerance
  SCENE/CUSTOM properties, session resolution and inactive/effective-value UI.
- `tools/scan_blender_deep_scene.py`: effective per-layer settings and older-build
  property fallback, retaining shader-evaluation and event-capacity guards.
- `tools/test_blender_deep_layer_overrides.py`: two-layer surface/volume fixture,
  scanner agreement, fallback and EXR-header/interval assertions.
- `tools/test_blender_deep_ui.py`, `tools/test_blender_deep_panel.py`,
  `tools/test_blender_deep_migration.py`: extended assertions/screenshots/defaults,
  not run after the failed gate.

Commands used the guarded environment and output root
`D:/temp/cycles_deep_multilayer/`:

1. `run_A_before.py cpu`: unchanged installed build rendered 24 CPU multilayer
   files, exit 0. The copied runner initially looked for the qualified single-layer
   reference under the new A-before directory and raised FileNotFoundError.
   This was a measurement-path error, not an identity mismatch. Per VALIDATION.md's
   parser/import/report repair exception, only preserved-output analysis was rerun:
   `tools/test_blender_deep_multilayer.py verification/step4/A-before/cpu --validate
   --reference verification/step2/regression/baseline`. All 24 files and qualified
   single-layer byte identity passed. No reference was replaced and no render repeated.
2. `rebuild_A.py`: verified pinned Blender HEAD, reset only the authorized generated
   overlay, ran overlay `--check`, applied it and built/installed incrementally
   through `build_guarded.py build`. Exit 0; 1140 unchanged source timestamps
   preserved. Log: `verification/build.log`.
3. `run_A.py overrides`: candidate two-layer render exit 0;
   `DEEP_LAYER_OVERRIDES_RENDER_PASS`. The fixture's effective-settings/scanner
   assertions completed. Both `Surface_0001.deep.exr` and `Volume_0001.deep.exr`
   were published under `verification/step4/A/overrides/`.
4. Preserved-output validator exited **1**, at this gate:

```text
File tools/test_blender_deep_layer_overrides.py, line 20
assert header['cycles:deepScope'] == 'analytic_volume_reference'
AssertionError
```

Evidence: `verification/step4/A/overrides-command.json`, `overrides.log`,
`overrides-validation.log`, and `overrides/scanner.json`. Surface checks and
Volume numeric/depth-tolerance checks preceding that assertion did not raise;
the complete fixture is nevertheless **failed/unqualified**. No repair, rerun,
reference change, threshold change or further failure investigation was performed.
This report does not classify the failure as a renderer bug or a validator bug.

The installed executable now contains the **unqualified Part A adapter candidate**.
No core renderer or beauty code was changed. The working-tree candidate is retained
for review; no Part A commit was made because that part did not pass its gates.

A separate scope conflict was identified by reading the existing guards before
rendering: Part E retains 4096 scene-level events and the prior shader volume
evaluation setting, while Girl is requested in surface mode. Existing guards
require a surface event limit at most 64 and volume on for shader volume evaluation.
Neither guard nor scene-level setting was silently changed. Part E was not run.

Skipped after STOP: candidate CPU regression/identity, UI, visual panel, migration
and full scanner drift gates; all Part B baselines/diagnostics, Part C CPU hair,
Part D GPU beauty/cross-backend gates and OptiX audit, Part E junkshop5, event
histograms, Gaffer and Nuke. No hair support is claimed. The earlier hair plan
milestones remain unimplemented. No SUPPORT.md qualification row was added.

### Task 08: Part A repair and qualification

The authorized validator repair changes only the expected native Blender volume
scope: `analytic_volume_reference` belonged to the standalone writer, while the
preserved non-shader-evaluated Blender fixture uses `native_scalar_extinction`.
The preserved outputs were validated once without rerendering; all later
assertions passed (`DEEP_LAYER_OVERRIDES_PASS`). New fixture manifests identify
shader evaluation so validation expects `shader_evaluated_scalar_extinction`
only when applicable. No renderer result, bound or reference was changed.

Implemented 2a/2b in the adapter and scanner: effective Volume Off passes shader
evaluation false and volume step zero; their UI controls are inactive. Layer
Events Per Sample defaults to Scene or uses an explicit Custom value, with the
same 1–8192 RNA limits as the scene. Existing core event guards are unchanged.
The scanner reports effective values and suggests the layer override on overflow.
Older-build scanner resolution falls back when layer properties are absent.

Commands: `rebuild_A2.py` (authorized reset/check/apply/incremental install), then
`run_A2.py overrides`, `cpu`, `ui`, `migration`, `panel`, `drift`, all under
`D:/temp/cycles_deep_multilayer/`. Build exit 0. Qualification outputs and logs:
`verification/step4/A2/`; preserved repair outputs: `verification/step4/A/overrides/`.

| Gate | Result |
| --- | --- |
| Two-layer overrides | PASS: 32×32; Surface Volume Off / 64 events / tolerance 0; Volume Scene On / 4096 events / shader evaluation / scene tolerance. |
| Headers and interval behavior | PASS: Surface has no volume method or positive tolerance header; Volume has shader_evaluated_scalar_extinction, shader-eval-adaptive, step 0.01, tolerance ~0.0001, and nonzero-depth intervals. Surface/Volume published 484/548 samples. |
| Negative surface Scene events=4096 | PASS: scanner BLOCK; renderer says invalid deep traversal limit; no negative deep file published. The intentional last rejection leaves Blender exit 1; completed fixture manifest plus validator exit 0 establishes the expected result. |
| CPU multilayer | PASS: all 24 outputs valid and whole-file byte-identical to preserved pre-Part-A outputs; qualified single-layer bytes identical too. `cpu-full-identity.json`. |
| UI | PASS: properties/defaults/limits, effective controls, paths, bake and draw assertions. |
| Migration | PASS: previous saved settings unchanged and all three new modes default to Scene, including reload. |
| Panel | PASS: three screenshots inspected: alpha-off overrides inactive; alpha-on Custom values visible; Volume Off disables shader evaluation/step; strict IDs inactive and effective tolerance 0 shown. |
| Scanner drift | PASS: 132 cases, zero false negatives and false positives; device read-only assertion and five path cases pass. Override positive/negative predictions are tested separately by the two-layer fixture. |

Files for commit A: adapter overlay, scanner, layer-override fixture, UI/panel/
migration tests, BLENDER.md and this report. No core renderer or beauty source
changes. No event histogram or OptiX hair audit applies to Part A. Hair support,
Parts B–E and junkshop5 remain pending at this commit. No push or PR.

### Part B candidate: fixture baseline and diagnostic plumbing

The user explicitly authorized diagnostics-only capture/kernel plumbing in B.
Curve admission remains unchanged while the fixtures and instruments are built.
`test_blender_deep_hair.py` defines 43 Ribbon cases: single/multiple segments,
world/plane and constant-opacity controls, dense/folded/adjacent/interleaved
strands, objects/instances/shared geometry, visibility and holdout, occlusion,
changed static frames, grazing/tip/inside starts, legacy reflection/transmission,
Chiang/Huang, the standalone six-node junkshop graph, and 8/16/17/32/64/65-event
chains. No junkshop asset is linked or saved.

Baseline command: `python D:/temp/cycles_deep_multilayer/run_B_baseline.py`.
Preserved final baseline: `verification/step4/B4/baseline/results.json` and
`baseline.log`. **43/43 passed**: 40 visible-hair cases rejected with the existing
static-polygon-mesh message and no deep publication; indirect, excluded and
camera-off cases rendered with matching scanner predictions. Blender exit 1
reflects the intentional preflight rejections; the completed 43-case manifest
and `HAIR_BASELINE_PASS 43` record the test result. Earlier setup attempts in
`B/` and `B1/` used incorrect RNA property/enum names, corrected before those
fixtures reached render; `B2/` preserves the preceding 41-case baseline.
`B3/` preserves the initial 43-case run. B4 additionally uses exact material
inputs/components from the read-only six-node extraction in `B-extract.log`;
source SHA-256 remained
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`
before/after. Fixture data is in `tools/test_blender_deep_hair_material.json`.

The candidate `.hits.csv` side channel separates raw intersections, accepted
opacity events and terminal sample status, retaining native type, primitive,
segment, u/v, t, facing, object and object ID. It records all accepted camera
samples, including misses, only when diagnostics are enabled. It is explicitly
diagnostic evidence, never an EXR completion marker. GPU staging is budgeted
separately and bounded to 129 rows per camera sample (64 raw/accepted pairs and
one overflow hit); excess duplicate-boundary diagnostics fail explicitly rather
than truncate. Ordinary capture/event/spill/EXR layouts are unchanged.

The new independent validator checks every pixel and the cuts immediately before
and after every raw/exported depth, plus native-ID ownership and event histograms.
Its synthetic self-check passes. The source audit of ten changed shared files
passes: bytes outside guarded deep code are unchanged (`B-source-identity.json`).
The oracle self-test command was `python tools/validate_blender_deep_hair.py`;
the source audit was `python D:/temp/cycles_deep_multilayer/audit_B_sources.py`.
The Markdown check passed: 35 documents, 199 local links, zero broken links;
41 external links were not checked.

#### STOP: Part B build failed

`python D:/temp/cycles_deep_multilayer/rebuild_B.py` completed overlay reset,
anchor check and application (1,105 overlay files; 1,133 unchanged timestamps
restored), but the incremental build exited 1. Preserved log:
`D:/temp/cycles_deep_multilayer/verification/step4/B-build-failed.log`.
The compiler reports two narrowing errors at
`src/integrator/path_trace_work_cpu.cpp:186`: `work_tile.x` and `work_tile.y`
are unsigned, while the new diagnostic `TraceContext` aggregate stores `int`.

```text
error: non-constant-expression cannot be narrowed from type 'uint'
(aka 'unsigned int') to 'int' in initializer list [-Wc++11-narrowing]
} context{capture, work_tile.x, work_tile.y, uint32_t(state->path.sample)};
2 errors generated.
ninja: build stopped: subcommand failed.
```

Stopped on this first failed build gate. No compiler repair, retry, follow-up
renderer investigation or further qualification was performed. No Part B
executable was linked/installed. The build used the initial CPU diagnostics
snapshot; the later GPU diagnostic plumbing and object-ID CSV extension in the
working tree were not built. The final 16-event settings for chain_8/16/17 were
also added after the B4 rejection baseline and remain unqualified.

Part A is committed as `2bc14c94e`. Part B remains **uncommitted and unqualified**.
Its candidate files are the two hair test/validator scripts and material JSON;
capture implementation/header; CPU kernel declaration, registration type and
implementation; CPU/GPU path-trace host code and GPU header; diagnostic types,
CUDA surface instrumentation, private OptiX launch ABI/queue/wrapper;
OutputDriver interface; Blender adapter; and this report. Curve admission and
the source junkshop file are unchanged. Existing untracked test assets were
preserved. No push or PR.

Prepared but not run: `run_B_control.py CPU|CUDA|OPTIX` and `run_B_resources.py`.
The former includes all-pixel raw/depth/ID checks, exact CPU beauty and saved
passes, and the existing GPU beauty policy with five independent deep-off
controls per backend. None of those candidate checks has passed a render yet.
There is therefore no qualified event histogram, overflow count, GPU resource
result, OptiX hair audit, Gaffer hair review, or junkshop5 file/merge result.
Parts C–E were not started. Nuke remains unavailable and unverified.


### Task 09: restore B1/B2 boundaries

GPU diagnostics were preserved in
`D:/temp/cycles_deep_multilayer/patches/partB-gpu-diagnostics.patch`.
Both reverse application (before removal) and forward application (after removal)
passed `git apply --check`. Moved hunks:

- `src/device/optix/queue.cpp`: private launch argument count, offsets and hit pointer.
- `src/integrator/path_trace_work_gpu.cpp`: hit buffer construction/allocation/free,
  launch arguments, readback, diagnostic emission and memory accounting.
- `src/integrator/path_trace_work_gpu.h`: hit buffer member.
- `src/kernel/deep/optix.h`: private diagnostic pointer and launch ABI size.
- `src/kernel/deep/surface_cuda.h`: bounded diagnostic writer, raw/accepted hit
  instrumentation, dispatch pointer and counter initialization.
- `src/kernel/device/optix/kernel_deep.cu`: diagnostic launch pointer forwarding.
- `src/kernel/deep/types.h`: diagnostic-capacity error and GPU record limit only.
- `src/integrator/path_trace.cpp`: GPU diagnostic staging budget only.
- `src/deep/capture.cpp`: GPU diagnostic-capacity error text only.

No GPU-only hunks existed in `capture.h` or `output_driver.h`. Shared hit types,
CPU tracing, capture CSV writing and adapter opt-in remain candidate changes.
No references to the removed GPU-only symbols remain in `src/`.

B1 candidate baseline command: `python D:/temp/cycles_deep_multilayer/run_B_baseline.py`.
Evidence: `verification/step4/task09-B1/baseline.log` and `baseline/results.json`
under the permitted output root. All **43/43** final fixtures passed on the
unchanged Part A installation: **40** visible-hair cases rejected with the
existing static-polygon-mesh message and no final deep file; **3** invisible
cases rendered with scanner agreement. This includes chain_8/16/17 at the final
16-event setting. Blender exits 1 after expected preflight rejections; the full
43-case manifest and `HAIR_BASELINE_PASS 43` establish the baseline outcome.
B1 commits only the three fixture/oracle/material files and this report.
GPU diagnostics are deferred to Part D. No hair support is qualified yet.


B1 commit: `d0d72fb25` (fixtures/oracle/material and report only).

B2 candidate compile repair: explicit `int(work_tile.x)` and `int(work_tile.y)`
in the new `TraceContext` aggregate in `path_trace_work_cpu.cpp`, as authorized
by task 09. No other repair has been made. Guarded build command:
`python D:/temp/cycles_deep_multilayer/rebuild_B2.py`.
CPU-only source audit: `python D:/temp/cycles_deep_multilayer/audit_B_sources.py`
passed all seven shared-file comparisons outside guarded deep code.
Standalone candidate command `python D:/temp/cycles_deep_multilayer/ctest_B2.py`
configured and rebuilt in `deep-reference-task09-B2`; all three standalone
reference/capture/density-cell CTests passed (9.27 seconds). EXR-enabled tests
are not part of that standalone configuration.


#### Task 09 B2 candidate results and STOP

The guarded overlay reset/check/apply and incremental build/install **passed**.
The only compile repair was the two authorized coordinate casts above. The
installed executable is now the **uncommitted B2 candidate**, not Part A.
Build log and executable SHA-256 are preserved under
`D:/temp/cycles_deep_multilayer/verification/step4/task09-B2/` as `build.log`
and `candidate-install.json`.

Commands, in order after the build (all runner paths below the permitted root):

- `python run_B2_suite.py cpu`: pass, 24 deep files byte-identical to
  `verification/step4/A2/cpu`; qualified single-layer identity also passed.
- `python run_B2_suite.py visibility`: pass, seven visibility cases;
  all three successful deep files byte-identical to the Step 2 reference.
- `python run_B2_suite.py drift`: pass, 132 cases, zero false negatives and
  zero false positives, read-only device override and five path checks passed.
- `python run_B2_suite.py allowlist-off`: pass, 32 opaque cases (including
  repeat outputs), 24 opacity cases and five expected rejections; all 88 deep
  files byte-identical to `verification/step2/nodes-CPU`. The runner uses the
  existing fixture source with only its diagnostics switch set off; no cases
  or references were changed. No hit CSV appeared in the off identity suites.
- `python run_B2_suite.py allowlist-on`: pass, the same render cases and
  rejections; existing 24 opacity oracle/beauty validations passed, maximum
  transmittance error `2.4213196336830833e-08`.
- `python run_B2_control.py CPU`: render passed; **validation failed**.

The triangle control rendered ordinary deep-off (`off0`), deep enabled with
 diagnostics off (`diagnostics-off`), and deep enabled with diagnostics on
(`on`). The existing control validator failed its exact deep-on versus
ordinary deep-off pixel comparison:

```text
File "tools/test_blender_deep_hair.py", line 226, in validate_control
    assert all(np.array_equal(before[k].pixels, after[k].pixels) for k in before)
AssertionError
```

Evidence: `controls/CPU-validation.log`, `controls/CPU.log`,
`controls/CPU-command.json`, and the three preserved render directories under
`verification/step4/task09-B2`. The assertion does not identify which image or
channel differs. No root-cause claim is made, and no follow-up measurement,
rerender, bound change or repair was attempted after this first failed gate.
The earlier all-pixel raw/depth/ID validator returned before this assertion,
but the combined control validation did not pass and published no histogram.

**STOP.** B2 has no commit. Its CPU/shared diagnostics and adapter candidate
remain in the working tree. B1 is committed as `d0d72fb25`. GPU plumbing remains
in the saved patch for Part D; no GPU diagnostics were reapplied. No push or PR.

Not run after the failure: the separate diagnostics-on versus diagnostics-off
beauty/deep identity comparison, `.hits.csv`/`.samples.csv` event agreement,
CUDA resource check, further Markdown validation, and all Parts C–E. There is
no qualified hair event histogram, overflow result, OptiX hair audit, Gaffer
hair review or junkshop5 output/merge. Nuke remains unavailable. The source
junkshop file and existing untracked test assets were not changed.


### Step 4c: CPU control beauty investigation

Task 10 authorized diagnosis only. **Root cause: the B1 control changes its EXR
color precision between renders. This is a control-export bug, not evidence of
a B2 beauty regression.** The first render is FLOAT; subsequent renders are
HALF for color channels. Deep enablement happened to follow the same order.
No renderer or fixture fix, gate change, bound change or reference replacement
was made. B2 remains an uncommitted, unqualified candidate.

#### Separate B1 reproduction and preserved B2 evidence

Built committed Cycles `d0d72fb25ff11974ea8aa517eeb6022117afc21c` over pinned
Blender `749518deb2f0735a22361a07488b35f7ea5c2fdf` using fresh detached worktrees
`repo-B1` and `blender-src-B1`, separate `build-B1` and `install-B1`, all under
`D:/temp/cycles_deep_multilayer/`. The same guarded environment, compiler and
build configuration were used. Overlay check/application, configure, full build
and install passed. The original B2 overlay and installation were not replaced.
B1 executable SHA-256:
`74a787d492299129f32ecfc21f36ad7d45de0ab9f04bdf2436466291fdf1c2ef`.
B2 executable SHA-256:
`d1054bad46d7b35993c6ec6114b7c2c922f47aeb7a5853507082f4bdbb2f7353`.

Evidence root: `D:/temp/cycles_deep_multilayer/verification/step4/task10/`.
`B1-configure.log`, `B1-build.log`, `build-identities.json`,
`B2-preserved-channels.json`, each experiment's `channel-comparison.json` and
`settings-trace.json`, and `trigger-matrix.json` retain the measurements.
Original Task 09 EXRs were only read. The new B1 baseline uses the committed
control function with read-only setting snapshots; it changes no render setting.

| Comparison | Beauty RGB, each channel | Saved Combined RGB, each channel | Denoising Albedo RGB, each channel | Other channel values |
| --- | --- | --- | --- | --- |
| B1 original control: off0 vs on | 1024 differing pixels; max 8.545815944671631e-6 | Same | 1024 differing pixels; max 1.9532442092895508e-4 | Identical, including A |
| B2 preserved: off0 vs on | 1024 differing pixels; max 8.545815944671631e-6 | Same | 1024 differing pixels; max 1.9532442092895508e-4 | Identical, including A |
| B2 preserved: on vs diagnostics-off | Exact | Exact | Exact | Exact |
| B1 diagnostic: 32-bit export for both renders | Exact | Exact | Exact | Exact |
| B2 candidate diagnostic: 32-bit export for both renders | Exact | Exact | Exact | Exact |

The two 32-bit experiments compare four beauty channels and all 22 saved-pass
channels with unchanged `numpy.array_equal` semantics: every maximum difference
is zero. They are diagnostic experiments, not promotion of the failed B2 gate.
B1 reproduces the original mismatch exactly, so task 10 step 2 applies; no B2
hunk bisection was necessary.

#### Fixture differences, listed before the variant runs

Both controls derive from `test_scan_blender_deep_scene.fixture` and already use
the same plain Diffuse material, World Background, plane, 32x32 orthographic
camera, transparent film, four CPU threads, and disabled denoising/adaptive
sampling/compositing/sequencer. Ribbon is also the factory curve-shape default.
The hair control removes its strand object before rendering.

Compared with the allowlist setup (`tools/test_blender_deep_allowlist.py:129`),
the control uses 4 versus 16 samples, seed 0 versus 123, BLACKMAN_HARRIS/1.5 versus
BOX/1 filtering, enabled Denoising Data and Sample Count, enabled deep IDs,
128 versus 8 transparent bounces, explicit per-layer surface/zero-tolerance/64-event
overrides versus equivalent scene settings, initially enabled diagnostics, and
unused groom/material datablocks. Crucially, it exports multilayer passes and
switches media/file formats after every render; the allowlist does not do that.
The allowlist rebuilds its fixture for each case and enables diagnostics for its
opacity cases. `fixture-differences.json` records this comparison before the
variant runs.

Each row below changes only the named setting or export operation from the B1
control, except the explicitly listed export-order experiments. Material/world
rows are repeat controls: their settings were already identical. Each invocation
contains two small renders and has a 300-second timeout. All renders completed.

| B1 experiment | Beauty exact? | Saved passes exact? | Maximum saved-pass difference |
| --- | --- | --- | --- |
| Original control, 4 samples | No | No | 1.9532442092895508e-4 |
| Omit saved-pass export and its format switches | Yes | Not exported | N/A |
| Disable Denoising Data only | No | No | 8.545815944671631e-6 |
| Disable diagnostic Sample Count only | No | No | 1.9532442092895508e-4 |
| 16 samples | No | No | 1.9544363021850586e-4 |
| Plain Diffuse (already the control material) | No | No | 1.9532442092895508e-4 |
| Allowlist world (already the control world) | No | No | 1.9532442092895508e-4 |
| Seed 123 | No | No | 1.9532442092895508e-4 |
| BOX filter, width 1 | No | No | 1.9532442092895508e-4 |
| Disable deep IDs | No | No | 1.9532442092895508e-4 |
| Transparent bounce limit 8 | No | No | 1.9532442092895508e-4 |
| Scene defaults for layer overrides | No | No | 1.9532442092895508e-4 |
| Disable deep diagnostics | No | No | 1.9532442092895508e-4 |
| Remove unused groom collection/data/material | No | No | 1.9532442092895508e-4 |
| Omit save_render call, retain format switches | No | Not exported | N/A |
| Keep deep OFF for both renders | No | No | 1.9532442092895508e-4 |
| Reverse order: deep ON first, OFF second | No | No | 1.9532442092895508e-4 |
| Set export color_depth=32 before each render | Yes | Yes | 0 |

Every non-exact beauty row has maximum RGB difference
`8.545815944671631e-6`; alpha remains exact. Disabling denoising-data storage
removes those passes but does not remove the Combined/beauty mismatch. Omitting
only `save_render` also does not remove it: the format assignments are sufficient.
Both deep-off-twice and reversed-order experiments establish that the difference
tracks render order rather than the deep switch. Holding just export bit depth
at 32 removes the difference with both Denoising Data and Sample Count enabled.

#### Mechanism and source references

The runtime trace is:

```text
first render:                       IMAGE / OPEN_EXR / 32
switch to MULTI_LAYER_IMAGE:         MULTI_LAYER_IMAGE / OPEN_EXR_MULTILAYER / 32
save render-passes.exr:              FLOAT color channels
set media_type = IMAGE:              IMAGE / PNG / 16
set file_format = OPEN_EXR:          IMAGE / OPEN_EXR / 16
second render and saved passes:      HALF color channels
```

The control sets 32-bit depth once in `tools/test_blender_deep_hair.py:86`.
Its pass-export sequence at lines 193-198 restores media type and file format
but never restores bit depth. The triggering assignment is line 197.
Upstream references below are relative to `blender-src-B1`, at the pinned Blender
commit above:

1. `source/blender/makesrna/intern/rna_scene.cc:1500` forwards the RNA media-type
   assignment to `BKE_image_format_media_type_set`.
2. `source/blender/blenkernel/intern/image_format.cc:91` implements that setter;
   line 100 selects PNG when switching a multilayer image to IMAGE.
3. `image_format.cc:151` validates the current depth against the new format.
   PNG supports only 8/16-bit (`image_format.cc:408`); the first valid choice is
   16-bit (`image_format.cc:434`). Selecting OpenEXR afterward keeps 16 because
   EXR permits both 16 and 32 (`image_format.cc:400`).
4. `source/blender/blenkernel/intern/image_save.cc:954` selects HALF only for
   color passes when the format depth is 16; non-color passes stay FLOAT.
   This explains why Combined and Denoising Albedo differ, while Sample Count,
   normals, depth and other data passes retain their values.

The saved values show the quantization directly: beauty
`0.03999999910593033` becomes `0.040008544921875`, and albedo
`0.800000011920929` becomes `0.7998046875`. Every affected preserved color value
matches the corresponding FLOAT value rounded to HALF. This rounding check is
diagnostic evidence only; it does not alter or relax the exact beauty gate.
No RNG/sample-offset, shader-state, kernel-feature or denoising computation change
is required to explain this failure. Equal-precision B1 and B2 renders are exact.

#### Qualified production exposure and proposed repair

The Task 09 allowlist passes are consistent with this diagnosis: their paired
beauty saves do not switch through multilayer/IMAGE formats between renders.
The qualified junkshop4 launcher
`D:/temp/cycles_deep_multilayer/production4.py:103` renders frames without this
save-render/format-switch sequence; its File Output nodes are separate settings.
It does not use the triggering sequence between frames.

The general production helper has the same restoration sequence at
`tools/render_blender_deep_scene.py:205`, but sets 32-bit depth at line 96 and
performs only one render (line 197). Its beauty and pass files are already saved
before the depth-changing restoration at line 212. The regression launcher
starts a separate Blender process per invocation (`tools/run_deep_regression.py:137`).
Thus the inspected qualified production paths are not exposed to a later render
with that changed precision. Reusing that helper's restoration sequence in a
multi-render session would expose the same latent issue. No production scene
was rerendered and no claim is made about unrelated scripts or GPU hair support.

Proposed fix, **not implemented**: explicitly preserve/restore the intended
32-bit color depth after the control's media/file-format restoration, or set it
before every control render. Keep all current equality checks. Add a precision
assertion or record actual saved channel types so unequal export precision
cannot masquerade as a beauty comparison. This is a low-risk fixture-only
repair; later control artifacts will intentionally become FLOAT, so the failed
gate must be rerun into a fresh directory. Preserve the historical outputs and
references. The same control function is intended for future GPU tests, so its
multi-render loop needs the repair before Part D qualification. Do not disable
Denoising Data or accept HALF-rounded equality as a workaround.

#### Reproduction and final state

Commands used include B1 `prepare_blender_deep.py --source .../blender-src-B1
--check` and application, then `python build_B1_investigation.py configure` and
`build`; `python run_task10.py baseline`; `python run_task10_matrix.py`; and
`python run_task10.py export_depth32 B2`. Diagnostic helpers live under the
permitted output root, not in repository source. Each experiment was measured
with `D:/CyclesDeepScratch/regression-tools/Scripts/python.exe measure_task10.py
EXPERIMENT_DIRECTORY`. Full command arrays and render logs are retained.
`settings-trace.json` is authoritative for variant settings; the original
control's fixed metadata fields are retained and explicitly marked as such in
`experiment.json`.

Before/after hashes confirm all 12 recorded B2 candidate/adapter files, GPU patch
and installed executable entries are unchanged (`preserved-before.json` and
`preserved-after.json`). The B1 source worktree remains clean. Only this report
was changed in the main repository during Task 10. No fix, commit, push or PR.
Stopped after diagnosis/report: no B2 hunk bisection, reference promotion,
remaining B2 qualification or Parts C-E. The new equal-precision diagnostic
results do not by themselves complete B2.

Report validation: Markdown link check passed (35 documents, 199 local links, zero broken; 41 external links not checked).


### Task 11: control export repair candidate — STOP at guard self-check

Task 11 authorized the export measurement repair. The **uncommitted candidate**
in `tools/test_blender_deep_hair.py` explicitly selects 32-bit color and lossless
ZIP after EXR format switches and immediately before `write_still`/`save_render`.
The IMAGE restoration temporarily selects PNG, which cannot accept 32-bit depth;
that pair restores OPEN_EXR first, then immediately sets 32-bit/ZIP. The draft
validator reads compared EXR headers and intends to reject non-FLOAT channels
with `export precision mismatch` before comparing beauty. This guard has not
passed validation (failure below).

Format-assignment audit covered all 13 Python files matching `media_type` or
`file_format` under `tools/`; the same search found no setters under `src/deep/`:

| File checked | Audit finding/action |
| --- | --- |
| `tools/test_blender_deep_hair.py` | Repeated render/export roundtrip changes precision; candidate repair and header guard added. |
| `tools/test_blender_deep_ui.py` | Switches PNG to multilayer EXR between test renders; explicit 32-bit/ZIP and a FLOAT channel-header assertion added for those EXRs. |
| `tools/render_blender_deep_scene.py` | One render, explicitly 32-bit; restoration happens after both outputs. No later render and no affected written output; unchanged. |
| `tools/test_blender_deep_allowlist.py` | Fresh setup selects EXR/32 before renders, with no inter-render format roundtrip; unchanged. |
| `tools/test_blender_deep_multilayer.py` | Sets EXR/32 during setup; no inter-render format switch; unchanged. |
| `tools/test_blender_deep_layer_overrides.py` | Configures format before render; no paired flat-beauty precision comparison or inter-render switch; unchanged. |
| `tools/test_blender_deep_path_templates.py` | Node-format changes occur before the single render per process; path tests, no inter-render beauty comparison; unchanged. |
| `tools/test_scan_blender_deep_scene.py` | Fresh scene per path fixture; no sequential export roundtrip; unchanged. |
| `tools/scan_blender_deep_scene.py` | Reads format for scanner decisions; no format setter; unchanged. |
| `tools/create_vdb_deep_scene.py` | Initial EXR/32 scene configuration; unchanged. |
| `tools/create_osl_deep_cases.py` | Writes a texture image, not sequential render results; unchanged. |
| `tools/create_osl_volume_cases.py` | Writes a texture image, not sequential render results; unchanged. |
| `tools/blender_beauty_preview.py` | Intentionally writes an 8-bit PNG preview; unchanged. |

Only the hair control and the UI mixed-layer flat EXRs have affected written
outputs. The UI suite was rerun into a fresh directory with
`python D:/temp/cycles_deep_multilayer/run_task11_suite.py ui` and **passed**,
including the new FLOAT checks. Evidence:
`D:/temp/cycles_deep_multilayer/verification/step4/task11/ui.log` and `ui/`.
Existing outputs and references were retained.

#### First failed gate: new precision-guard self-check

A small check called `float_channels` first on the preserved, known-FLOAT
`verification/step4/task09-B2/controls/CPU/off0/beauty.exr`, intending next to
confirm rejection of the preserved HALF file. The first call failed:

```text
File "tools/test_blender_deep_hair.py", line 229, in float_channels
    assert all(channel.type() == OpenEXR.FLOAT for channel in image.header()['channels'])
IndexError: invalid axis: 1 (ndim = 1)
```

OpenEXR raised this exception when the new guard called `type()` on a channel
from the header. The negative HALF check was never reached. Failure details are
preserved in `verification/step4/task11/precision-guard-failure.log`. This is a
failure in the new validation code, not a newly observed renderer difference.
Stopped immediately: no API investigation, correction or retry after this gate.

No fixture-repair commit and no B2 commit were made. The draft export repair,
UI test change and this report remain uncommitted. The existing B2 renderer and
adapter candidate, GPU patch, original measurement outputs and references remain
intact. `install-B1` and `repo-B1` are retained. No push or PR.

Not run: the repaired control rerender, diagnostics-on/off byte identity,
`.hits.csv`/`.samples.csv` agreement, Markdown link check, or Parts C-E.
Previously passed Task 09 suites were not rerun except the affected UI suite.
There is no new hair qualification, event histogram, OptiX audit, Gaffer review
or junkshop5 result. The newly edited junkshop source was not opened or saved;
its new hash and saved node names remain to be recorded in Part E. Nuke remains
unavailable. B2 is still unqualified, and the new precision guard itself requires
repair before continuation.


### Task 12: dtype guard repair and continuation

Replaced the new guard's failing header-channel `type()` API call with the
user-authorized pixel-dtype check. It retains separate channels and rejects any
channel whose dtype is not float32 with `export precision mismatch`. No bounds,
references, cases or assertions were relaxed. Before: known-FLOAT input raised
`IndexError` inside the new validator. After: both preserved `off0/beauty.exr`
and `off0/render-passes.exr` are accepted; both preserved `on/beauty.exr` and
`on/render-passes.exr` are rejected with the required precision message.
Evidence: `verification/step4/task12/precision-guard-selfcheck.json` under the
permitted output root. The UI test's already-passed struct-based FLOAT guard is
retained unchanged. Its Task 11 successful rerun remains valid.

This repair uses task 12 section 1; the standing test-code repair rule is now
accepted for subsequent new-test bugs, once per bug with good/bad self-checks.
Real-render comparison failures still stop. The standalone fixture-repair commit
contains only the hair control, UI test and accumulated report. The Task 11 audit
list and affected-output scope above still apply.

#### Task 12 gate results and STOP

The standalone fixture repair is committed as `241a7e381`,
`fix(test): keep FLOAT EXR precision across control format switches`.
The commit message records the Task 10 evidence. No additional test-code bugs
were repaired under the section 2 standing rule during this continuation.

The repaired `run_B2_control.py CPU` passed on the existing B2 candidate build.
All-pixel raw-hit, depth and ID validation passed, as did exact CPU beauty and
saved-pass channel comparisons between deep ON and ordinary deep OFF. Compared
files passed the FLOAT guard. The result contains 1,024 pixels and 4,096 camera
samples, maximum all-depth error 0, median/p95/maximum accepted events 1/1/1,
maximum raw hits 1, and zero overflow pixels. Evidence is under
`D:/temp/cycles_deep_multilayer/verification/step4/task12/controls/`, including
`CPU.log`, `CPU-validation.log` and `CPU/validation.json`.

The next gate, diagnostics ON versus OFF, **failed**. Both beauty files passed
the FLOAT guard, channel-name agreement and exact pixel-array comparison, but
the required whole-file byte comparison failed:

```text
AssertionError: ('diagnostics on/off file bytes differ', 'beauty.exr')
```

Evidence:
`D:/temp/cycles_deep_multilayer/verification/step4/task12/diagnostics-validation.log`.
This is a failing assertion on real render output and is explicitly excluded
from Task 12's standing test-code repair rule. Stopped without diagnosing the
file difference, changing the gate or retrying. Its cause remains undetermined.

Not reached: diagnostics ON/OFF saved-pass and deep-EXR identity, triangle
`.hits.csv`/`.samples.csv` agreement, Markdown link check, or Parts C-E. Previously
passed Task 09 suites were not rerun. No B2 commit was made; the B2 candidate
remains uncommitted and unqualified. The GPU patch remains preserved at
`D:/temp/cycles_deep_multilayer/patches/partB-gpu-diagnostics.patch`.
This final report addition is uncommitted. Existing work, measurement artifacts
and references are retained; `install-B1` and `repo-B1` are kept. No push or PR.
The junkshop source was not opened or saved, so its new hash and saved node names
remain for Part E. No new hair qualification, OptiX audit, Gaffer review or
junkshop5 result is available; Nuke remains unavailable.

### Task 13: corrected flat identity and B2 qualification

Task 13 authorizes exact flat-EXR channel names, FLOAT types, pixels and headers,
except explicitly named render timings for the exact layer `Visible`. The hair
validator now provides `flat_identity` / `flat_header_identity`; no deep comparator
metadata was changed. Every other header attribute remains mandatory, including
Date, channel sampling and compression. Synthetic self-checks accepted timing-only
changes and rejected changed Date, an unlisted layer timing, a changed data window,
changed pixels and HALF channels. No repair under the standing test-code rule was
needed.

Only validation was rerun on preserved Task 12 outputs. Both `beauty.exr` and
`render-passes.exr` passed. Each excluded attribute is listed below; values are
identical between the two file pairs (before = diagnostics OFF, after = ON):

| Attribute | Before, both files | After, both files |
| --- | --- | --- |
| `RenderTime` | `00:00.05` | `00:00.15` |
| `cycles.Visible.render_time` | `00:00.04` | `00:00.14` |
| `cycles.Visible.total_time` | `00:00.05` | `00:00.14` |
| `cycles.Visible.synchronization_time` | `00:00.00` | `00:00.00` |

The deep EXR passed whole-file byte equality and `compare_deep_identity.compare`.
Diagnostics OFF produced no CSV. Triangle ledger agreement passed for 25 files:
one control (4,096 camera samples, 324 sampled-grid entries, 316 accepted grid
hits) and 24 allowlist fixtures (16,384 camera samples, 1,296 grid entries and
2,592 accepted grid hits each). All raw ledgers cover every camera sample; the
legacy `.samples.csv` grid agrees in event order, depth, alpha and accepted count.
CSV presence was never treated as publication: the final EXR must also exist.
The previously passed Task 12 CPU control and Task 09 regression/source-audit/
CTest evidence remain applicable; no renderer rebuild or rerender was performed.

Commands: regression-tools Python with `-B` ran
`D:/temp/cycles_deep_multilayer/selfcheck_flat_task13.py` and
`D:/temp/cycles_deep_multilayer/validate_B2_diagnostics_task13.py`.
Evidence: `D:/temp/cycles_deep_multilayer/verification/step4/task13/`, including
`flat-gate-selfcheck.json`, `flat-excluded-timing.json`, `deep-identity.json`,
`diagnostics-agreement.json` and `diagnostics-validation.log`.

Markdown link check passed: 35 documents, 199 local links, zero broken links;
41 external links are not checked by this tool. B2 is ready for its CPU-only
commit, including the corrected flat gate. GPU diagnostics remain deferred to
Part D in the preserved patch. B1 install/worktree are retained. No push or PR.

B2 commit: `fce1fc258` (`feat(deep): add CPU-only raw-hit diagnostics`).

#### Part C candidate: CPU static Ribbon

Candidate files: `src/session/deep.cpp`,
`src/kernel/device/cpu/kernel_arch_impl.h`, `tools/scan_blender_deep_scene.py`
and `tools/test_blender_deep_hair.py`. Preflight admits CPU static Ribbon only,
with effective volume OFF and depth tolerance zero. Other shapes, hair motion,
OSL on hair, Hair Info and point clouds reject; GPU hair remains blocked until
Part D. Mesh-only casts remain in the mesh branch; volume capture is unchanged.
The deep CPU surface loop accepts the packed static Ribbon primitive, and calls
the triangle boundary helper only for two triangles. Native continuation and
shader setup are unchanged. Scanner rules mirror those restrictions.

The new hair runner reuses the existing fixtures and oracles, with FLOAT beauty
and saved passes, numeric and strict repeats, and expected capacity failures.
Each render is bounded to 300 seconds. This is an uncommitted candidate, not a
new supported scope; qualification results follow below.

#### Part C build and first failed gate (STOP)

Guarded overlay reset/check/apply and incremental build passed, via
`python -B D:/temp/cycles_deep_multilayer/rebuild_C.py` (11 build/install steps).
Pinned Blender source remains `749518deb2f0735a22361a07488b35f7ea5c2fdf`.
Installed candidate executable SHA-256:
`5e220d0042556e8b09fe956e73faf5f5745e7a2724fa32ae407a1f5235799f6f`.
Build evidence: `verification/step4/task13/build-C-driver.log` and
`verification/build.log` under the permitted output root.

Regression-tools Python with `-B` ran `run_C_hair.py single_1_world`, then the
remaining fixture list in its original order. Each subprocess uses factory
startup, background mode, auto-execution disabled and Python exit code 1.
The first case passed all scheduled checks: numeric ON/repeat and strict/repeat
raw oracle, known-hit checks (64 interior camera samples each), all-depth cuts,
finite/sorted EXR validation, numeric object-ID ownership, and strict comparator
repeat identity. CPU beauty and saved-pass FLOAT pixels and headers matched
between deep ON and OFF under the Task 13 timing rule.

All four world-case deep runs: 1,024 pixels, 4,096 camera samples, maximum
all-depth error `1.4901161193847656e-08`, median/p95/maximum events `0/1/1`,
maximum raw hits 1, zero overflow pixels. Numeric header bound is
`0.0010000000474974513`; strict header bound is `1e-06`.
Excluded timing values for **both** world-case flat file pairs, before = deep ON,
after = ordinary deep OFF:

| Attribute | Before, both files | After, both files |
| --- | --- | --- |
| `RenderTime` | `00:00.07` | `00:00.00` |
| `cycles.Visible.render_time` | `00:00.06` | `00:00.00` |
| `cycles.Visible.total_time` | `00:00.07` | `00:00.00` |
| `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |

The next case, `single_1_plane`, passed its numeric ON deep oracle and 64
known-hit samples. Its 1,024 pixels / 4,096 samples had maximum all-depth error
`1.4901161193847656e-08`, median/p95/maximum events `1/1/1`, maximum raw hits 1,
and zero overflow pixels. The ordinary deep-OFF render completed. Beauty FLOAT
channels and pixels matched, but the subsequent exact-header gate failed:

```text
AssertionError: ('flat EXR header differs', 'Date')
```

`Date` is not in Task 13's explicitly authorized flat timing allowlist. This is
an assertion on real output, not a new-test API/runner bug, so the standing repair
rule does not apply. Stopped immediately without inspecting the differing Date
values, broadening the allowlist, retrying or rendering additional fixtures.
The plane saved-pass comparison and repeats were not reached.

Evidence: `verification/step4/task13/C-first-gate.log`,
`C-remaining-gates.log`, `C/first-case-results.json`, and per-render logs, scanner
reports, validation JSON and EXRs below `C/`. All paths are under
`D:/temp/cycles_deep_multilayer/`. The second runner's `C/results.json` contains
only its progress; the completed first case is preserved separately.

No Part C commit: its source, scanner, fixture and report changes remain
uncommitted. No compile repair or standing-rule test repair was needed. B2 is
committed and qualified; the installed executable is now the **unqualified Part C
candidate**. The GPU patch is retained and has not been reapplied. Existing
untracked assets and earlier evidence remain intact; no push or PR.

Not reached: the rest of the hair fixtures, capacity 16/64 gates, unsupported-case
render/scanner drift qualification, holdout qualification, Gaffer review, full
Part C CPU regression/source/resource audit, visibility/UI/panel/migration suite,
CTests and post-Part-C Markdown check. Parts D/E were not started: no OptiX audit,
GPU qualification, junkshop5 files, new source hash/node-name inspection or
three-layer merge. The junkshop source was not opened or saved. Nuke is unavailable.


### Task 14: flat run metadata, CPU fixture results and STOP

Task 14's exact flat-EXR allowlist is implemented in `flat_header_identity`:
Date, RenderTime, Memory, PeakMemory, Hostname, and the exact supplied layer's
render_time, total_time and synchronization_time attributes. Optional attributes
are excluded only when present. All other attributes remain compared, including
Time, Frame, File, Camera, Scene, Software, layer sample count, image/channel
layout and compression. Deep identity is unchanged. No name was added under the
standing metadata extension. No renderer changes or rebuild were made this turn.

Self-checks accepted all eight listed run-metadata names for one layer, and
rejected Time, Frame, sample count, an unlisted layer's timing, data-window,
pixel and HALF-channel differences. The preserved `single_1_plane` ON/OFF beauty
and saved passes were revalidated first and passed without rerendering. Its
remaining repeat/strict renders then passed. Remaining fixtures followed the
original order. The candidate executable remains the Task 13 build, SHA-256
`5e220d0042556e8b09fe956e73faf5f5745e7a2724fa32ae407a1f5235799f6f`.

Commands used regression-tools Python with `-B` for `selfcheck_flat_task14.py`,
the preserved-file validation, `resume_C_plane_task14.py single_1_plane`, and
`run_C_hair_task14.py` in the original remaining fixture order. The rejection
suite used `python -B D:/temp/cycles_deep_multilayer/run_C_rejections_task14.py`.
All runners and evidence are under `D:/temp/cycles_deep_multilayer/`; Task 14
summaries are in `verification/step4/task14/`. Existing evidence was retained.

#### Test-code repairs under the standing rule

1. Before the capacity cases, source inspection found that the new overflow test
   searched for `capacity`, but `DEEP_ERROR_CAPACITY` reports
   `deep traversal limit exceeded`. The assertion now requires that exact
   diagnostic and no final EXR. Self-check accepted that error with no publication,
   and rejected a different error, missing error, and a partial final EXR.
   Evidence: `capacity-message-selfcheck.json`. No limit or fixture changed.
2. The expected 17-event rejection was caught by the test, but Blender returned 1.
   The new external runner had unconditionally required exit 0. It now permits
   exit 1 only for the designated overflow case/mode with the exact capacity
   diagnostic, recorded overflow and no recorded or actual final deep EXR. The
   preserved result passed; wrong error, partial publication, wrong case and a
   crash exit all failed the self-check (`overflow-runner-selfcheck.json`). No
   rerender of the 17-event case was needed. Continuation resumed at chain_32.

#### Positive and capacity gates

All 43 original fixtures completed: 41 non-overflow cases passed their scheduled
CPU deep ON/OFF beauty and saved-pass identity, FLOAT guard, all-pixel raw/depth
oracle, valid/sorted EXRs, numeric object-ID ownership and strict/numeric repeat
identity. Single-strand analytic checks each covered 64 interior camera samples.
The 16- and 64-event chains passed exactly at their configured limits. The
17- and 65-event chains failed explicitly with `deep traversal limit exceeded`
and no final deep EXR. No complete-output histogram is claimed for those failures.
The successful cases each have 1,024 pixels and 4,096 camera samples. Their ON
histograms and maximum all-depth error are below; complete per-mode values are
preserved in `all-fixture-results.json` and per-render `validation.json` files.

| Fixture | Median events | p95 | Max events | Max raw hits | Overflow pixels | Maximum all-depth error |
| --- | --- | --- | --- | --- | --- | --- |
| `single_1_world` | 0 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `single_1_plane` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `single_4_world` | 0 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `single_4_plane` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `single_1_constant_world` | 0 | 1 | 1 | 1 | 0 | 1.87998983048e-09 |
| `single_4_constant_world` | 0 | 1 | 1 | 1 | 0 | 1.87998983048e-09 |
| `dense` | 9 | 15 | 19 | 19 | 0 | 9.73727154463e-09 |
| `fold` | 1 | 2 | 2 | 2 | 0 | 1.74910119721e-09 |
| `adjacent` | 1 | 1 | 2 | 2 | 0 | 1.87998983048e-09 |
| `interleaved` | 1 | 1 | 4 | 4 | 0 | 4.09612321839e-09 |
| `two_objects` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `instance` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `junkshop` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `indirect` | 1 | 1 | 1 | 1 | 0 | 0 |
| `excluded` | 1 | 1 | 1 | 1 | 0 | 0 |
| `camera_off` | 1 | 1 | 1 | 1 | 0 | 0 |
| `shared` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `holdout` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `occluded` | 1 | 1 | 1 | 1 | 0 | 0 |
| `occluding` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `frame1` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `frame2` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `grazing` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `tip` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `inside` | 0 | 0 | 0 | 0 | 0 | 0 |
| `legacy_opaque` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `legacy_constant` | 1 | 1 | 2 | 2 | 0 | 1.55874002594e-09 |
| `legacy_varying` | 1 | 1 | 2 | 2 | 0 | 1.76624386228e-08 |
| `transmission_opaque` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `transmission_constant` | 1 | 1 | 2 | 2 | 0 | 1.55874002594e-09 |
| `transmission_varying` | 1 | 1 | 2 | 2 | 0 | 1.76624386228e-08 |
| `chiang_opaque` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `chiang_constant` | 1 | 1 | 2 | 2 | 0 | 1.55874002594e-09 |
| `chiang_varying` | 1 | 1 | 2 | 2 | 0 | 1.76624386228e-08 |
| `huang_opaque` | 1 | 1 | 1 | 1 | 0 | 1.49011611938e-08 |
| `huang_constant` | 1 | 1 | 2 | 2 | 0 | 1.55874002594e-09 |
| `huang_varying` | 1 | 1 | 2 | 2 | 0 | 1.76624386228e-08 |
| `chain_8` | 0 | 0 | 8 | 8 | 0 | 5.44637634992e-09 |
| `chain_16` | 0 | 0 | 16 | 16 | 0 | 5.50974960101e-09 |
| `chain_32` | 0 | 0 | 32 | 32 | 0 | 5.70208347206e-09 |
| `chain_64` | 0 | 0 | 64 | 64 | 0 | 4.43047043497e-09 |

#### Excluded metadata values

Every excluded attribute from this turn's passed flat comparisons is recorded
below. Values are **deep ON -> ordinary deep OFF**. Both flat files share the
listed values unless the Files column says otherwise. The already-completed
`single_1_world` comparison remains recorded under Task 13; it was not rerun.
No Memory, PeakMemory or Hostname attribute occurred in these comparisons.

| Fixture | Files | Attribute | ON | OFF |
| --- | --- | --- | --- | --- |
| `single_1_plane` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 15:10:01` | `2026/10/18 15:10:02` |
| `single_1_plane` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `single_1_plane` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `single_1_plane` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `single_1_plane` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `single_4_world` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:34` | `2026/10/18 16:09:34` |
| `single_4_world` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.07` | `00:00.00` |
| `single_4_world` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.06` | `00:00.00` |
| `single_4_world` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `single_4_world` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.07` | `00:00.00` |
| `single_4_plane` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:37` | `2026/10/18 16:09:38` |
| `single_4_plane` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `single_4_plane` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `single_4_plane` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `single_4_plane` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `single_1_constant_world` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:41` | `2026/10/18 16:09:42` |
| `single_1_constant_world` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.07` | `00:00.00` |
| `single_1_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.06` | `00:00.00` |
| `single_1_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `single_1_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.07` | `00:00.00` |
| `single_4_constant_world` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:45` | `2026/10/18 16:09:46` |
| `single_4_constant_world` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.07` | `00:00.00` |
| `single_4_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.06` | `00:00.00` |
| `single_4_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `single_4_constant_world` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.07` | `00:00.00` |
| `dense` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:49` | `2026/10/18 16:09:51` |
| `dense` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.52` | `00:00.01` |
| `dense` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.51` | `00:00.01` |
| `dense` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `dense` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.52` | `00:00.01` |
| `fold` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:09:57` | `2026/10/18 16:09:58` |
| `fold` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `fold` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `fold` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `fold` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `adjacent` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:02` | `2026/10/18 16:10:02` |
| `adjacent` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `adjacent` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `adjacent` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `adjacent` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `interleaved` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:06` | `2026/10/18 16:10:07` |
| `interleaved` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `interleaved` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.15` | `00:00.00` |
| `interleaved` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `interleaved` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `two_objects` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:10` | `2026/10/18 16:10:11` |
| `two_objects` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `two_objects` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `two_objects` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `two_objects` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `instance` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:15` | `2026/10/18 16:10:15` |
| `instance` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `instance` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `instance` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `instance` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `junkshop` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:19` | `2026/10/18 16:10:20` |
| `junkshop` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `junkshop` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.15` | `00:00.00` |
| `junkshop` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `junkshop` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `indirect` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:23` | `2026/10/18 16:10:24` |
| `indirect` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `indirect` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `indirect` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `indirect` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `excluded` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:28` | `2026/10/18 16:10:28` |
| `excluded` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `excluded` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `excluded` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `excluded` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `camera_off` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:32` | `2026/10/18 16:10:33` |
| `camera_off` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `camera_off` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.12` | `00:00.00` |
| `camera_off` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `camera_off` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `shared` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:36` | `2026/10/18 16:10:37` |
| `shared` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `shared` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `shared` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `shared` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `holdout` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:41` | `2026/10/18 16:10:42` |
| `holdout` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `holdout` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `holdout` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `holdout` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `occluded` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:45` | `2026/10/18 16:10:46` |
| `occluded` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `occluded` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `occluded` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `occluded` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `occluding` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:49` | `2026/10/18 16:10:50` |
| `occluding` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `occluding` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `occluding` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `occluding` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `frame1` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:54` | `2026/10/18 16:10:54` |
| `frame1` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `frame1` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `frame1` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `frame1` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `frame2` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:10:58` | `2026/10/18 16:10:59` |
| `frame2` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `frame2` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.15` | `00:00.00` |
| `frame2` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `frame2` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `grazing` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:03` | `2026/10/18 16:11:03` |
| `grazing` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `grazing` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `grazing` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `grazing` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `tip` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:07` | `2026/10/18 16:11:08` |
| `tip` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `tip` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `tip` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `tip` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `inside` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:11` | `2026/10/18 16:11:12` |
| `inside` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.06` | `00:00.00` |
| `inside` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.05` | `00:00.00` |
| `inside` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `inside` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.06` | `00:00.00` |
| `legacy_opaque` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:15` | `2026/10/18 16:11:16` |
| `legacy_opaque` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `legacy_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.12` | `00:00.00` |
| `legacy_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `legacy_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `legacy_constant` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:20` | `2026/10/18 16:11:21` |
| `legacy_constant` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `legacy_constant` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `legacy_constant` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `legacy_constant` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `legacy_varying` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:25` | `2026/10/18 16:11:26` |
| `legacy_varying` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.25` | `00:00.00` |
| `legacy_varying` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.23` | `00:00.00` |
| `legacy_varying` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `legacy_varying` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.25` | `00:00.00` |
| `transmission_opaque` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:31` | `2026/10/18 16:11:32` |
| `transmission_opaque` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.21` | `00:00.00` |
| `transmission_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.19` | `00:00.00` |
| `transmission_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `transmission_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.20` | `00:00.00` |
| `transmission_constant` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:36` | `2026/10/18 16:11:36` |
| `transmission_constant` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `transmission_constant` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.15` | `00:00.00` |
| `transmission_constant` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `transmission_constant` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `transmission_varying` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:40` | `2026/10/18 16:11:41` |
| `transmission_varying` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.17` | `00:00.00` |
| `transmission_varying` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.15` | `00:00.00` |
| `transmission_varying` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `transmission_varying` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.16` | `00:00.00` |
| `chiang_opaque` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:45` | `2026/10/18 16:11:46` |
| `chiang_opaque` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.20` | `00:00.00` |
| `chiang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.18` | `00:00.00` |
| `chiang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chiang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.19` | `00:00.00` |
| `chiang_constant` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:50` | `2026/10/18 16:11:51` |
| `chiang_constant` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `chiang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `chiang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chiang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `chiang_varying` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:55` | `2026/10/18 16:11:55` |
| `chiang_varying` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `chiang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `chiang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chiang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `huang_opaque` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:11:59` | `2026/10/18 16:12:00` |
| `huang_opaque` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.15` | `00:00.00` |
| `huang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `huang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `huang_opaque` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `huang_constant` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:12:04` | `2026/10/18 16:12:04` |
| `huang_constant` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.16` | `00:00.00` |
| `huang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| `huang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `huang_constant` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| `huang_varying` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:12:08` | `2026/10/18 16:12:09` |
| `huang_varying` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.14` | `00:00.00` |
| `huang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `huang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `huang_varying` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |
| `chain_8` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:12:13` | `2026/10/18 16:12:13` |
| `chain_8` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.11` | `00:00.00` |
| `chain_8` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.09` | `00:00.00` |
| `chain_8` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chain_8` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.11` | `00:00.00` |
| `chain_16` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:12:16` | `2026/10/18 16:12:17` |
| `chain_16` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.11` | `00:00.00` |
| `chain_16` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.10` | `00:00.00` |
| `chain_16` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chain_16` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.11` | `00:00.00` |
| `chain_32` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:12:55` | `2026/10/18 16:12:56` |
| `chain_32` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.17` | `00:00.00` |
| `chain_32` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.16` | `00:00.00` |
| `chain_32` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chain_32` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.17` | `00:00.00` |
| `chain_64` | beauty.exr, render-passes.exr | `Date` | `2026/10/18 16:13:00` | `2026/10/18 16:13:01` |
| `chain_64` | beauty.exr, render-passes.exr | `RenderTime` | `00:00.25` | `00:00.00` |
| `chain_64` | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.24` | `00:00.00` |
| `chain_64` | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.01` | `00:00.00` |
| `chain_64` | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.25` | `00:00.00` |

#### First real failed gate: motion rejection diagnostic

The new rejection suite passed THICK, THICK_LINEAR, hair-volume and positive
hair-tolerance rejection checks, including scanner BLOCK predictions and no
final EXR. Its next case (`motion`) failed the required diagnostic assertion:

```text
Expected: hair motion is unsupported
Actual: Error: Deep: only static polygon meshes and background lighting are supported: Plane
```

The scanner reported `hair_motion`; the renderer rejected with the mesh message.
No final deep EXR was produced. This is a comparison against actual renderer
output, not a wrong API/path or a run-metadata discrepancy. Stopped immediately;
no diagnosis, assertion relaxation, fixture changes or renderer fixes followed.
Evidence: `verification/step4/task14/rejections/baseline.log` and
`rejections/cases/results.json`. Later OSL/Hair Info/point-cloud rejection cases
were not reached. The new rejection test remains in the uncommitted candidate.

No Part C commit; HEAD remains `fce1fc258`. Part C source/scanner/tests and report
remain uncommitted, and the installed executable remains unqualified for hair.
The GPU patch was kept and not reapplied. No push or PR. No source .blend was
opened or saved. B1 install/worktree and existing untracked assets remain intact.

Not reached: remaining rejection checks, full CPU regression and source/resource
audit, visibility/UI/panel/migration/scanner-drift suites, standalone CTests,
Gaffer DeepToFlat/DeepSlice/DeepMerge, further holdout-manifest checks and the
final Markdown check. Parts D/E were not started: no GPU diagnostics/hair
qualification, OptiX SBT/stack audit, junkshop5 files or three-layer merge, or
new junkshop source hash/saved-node-name inspection. Nuke remains unavailable.

### Task 15: isolated rejection fixtures and remaining CPU gates

Applied the authorized fixture fix in `tools/test_blender_deep_hair.py`: the
motion case disables Cycles deformation motion on every non-hair mesh, retaining
hair motion settings. The original Task 14 message was
`Deep: only static polygon meshes and background lighting are supported: Plane`.
No renderer guard changed. No further isolation under the standing extension was
needed, and the optional combined mesh/hair-motion case was not added.

Every rejection case now calls `rejection_isolation` before rendering. It requires
exactly one scanner BLOCK for the expected rule and target object; Hair Info also
requires the exact node type. Failure is labeled `rejection fixture not isolated`.
Scanner JSON is saved before the assertion. Synthetic self-check accepted one
correct target BLOCK and rejected zero blocks, two blocks, the wrong object and
the wrong feature. Evidence: `verification/step4/task15/isolation-selfcheck.json`.

The entire rejection suite was rerun from THICK. All eight cases passed the
isolation self-check, returned their required renderer diagnostic and published
no deep EXR:

| Case | Sole BLOCK rule | Target | Required renderer diagnostic (passed) |
| --- | --- | --- | --- |
| THICK | hair_shape | Strands | Ribbon shape required |
| THICK_LINEAR | hair_shape | Strands | Ribbon shape required |
| volume | hair_volume | Strands | Deep Volume off required |
| tolerance | hair_tolerance | Strands | Surface Depth Tolerance 0 required |
| motion | hair_motion | Strands | hair motion is unsupported |
| osl | hair_osl | Strands | OSL on hair is unsupported |
| hair_info | node, node_type=hair_info | Strands | Hair Info is unsupported |
| pointcloud | geometry | Points | point clouds are unsupported |

The external launcher accepts Blender's known exit 1 from handled rejections
only when the final `HAIR_REJECTIONS_PASS 8` marker is present, reusing the
expected-rejection handling established in Task 14. No test repair/retry or
metadata-allowlist extension was needed this turn. All exact messages and scanner
findings are in `task15/rejections/cases/results.json`; the log is
`task15/rejections/baseline.log` under the Step 4 evidence root.

#### Remaining Part C gates completed

- CPU multilayer validation passed: 24 files, byte-identical to the Part A
  reference; qualified single-layer reference validation also passed.
- Source audit passed: the only changed shared source is CPU kernel_arch_impl.h;
  code outside guarded deep blocks is identical. The established
  `record_beauty_build.py` deep-host exclusions cover session/deep.cpp.
- Resource audit passed: all 75 common CUDA kernel records unchanged.
- Visibility suite passed, with three deep files byte-identical to its reference.
- UI and migration suites passed.
- Scanner drift suite passed, including five path cases.
- Panel test passed. All three screenshots were visually inspected: Alpha OFF
  disables deep settings; Alpha ON enables overrides while volume-only controls
  stay disabled for Volume OFF; strict mode leaves the saved ID checkbox set but
  inactive and displays its explanatory message.

Commands: `run_C_rejections_task15.py`, `run_C_suite_task15.py cpu`,
`audit_C_sources_task15.py`, `run_C_resources_task15.py`,
`run_C_suite_task15.py visibility`, `run_C_ui_task15.py ui`,
`run_C_ui_task15.py migration`, `run_C_suite_task15.py drift`, and
`run_C_ui_task15.py panel`, each through `python -B` under
`D:/temp/cycles_deep_multilayer/`. Outputs are in
`D:/temp/cycles_deep_multilayer/verification/step4/task15/`.
No renderer build or hair-positive fixture rerender was required; the Task 14
histograms, beauty/metadata values and repeat-oracle results remain applicable.

#### First failed gate: allowlist regression particle_hair expectation

`run_C_suite_task15.py allowlist-off` stopped in the allowlist test's
`particle_hair` case before rendering that case:

```text
prediction=scan(s);assert prediction['blocks'],case
AssertionError: particle_hair
```

The scanner returned no BLOCK where that regression expected one. The suite did
not reach its final PASS marker; its launcher correctly failed. This is a failed
scanner assertion, not a pre-existing legitimate renderer guard on a non-target
object, so Task 15's isolation extension does not cover it. Stopped without
investigation, changing the expectation, dropping the case or retrying.
Evidence: `task15/allowlist-off.log` (copied test line 194), and
`run_C_suite_task15.py` launch output. The earlier allowlist render markers are
partial progress only; the allowlist-off suite and its final identity check are
not marked passed. The subsequent allowlist-on command was not run.

No Part C commit: HEAD remains `fce1fc258`. The Part C candidate, report changes,
GPU patch and pre-existing untracked assets remain preserved. The installed
executable is still the unqualified Part C candidate. No push or PR; no junkshop
.blend opened or saved. B1 install/worktree are retained.

Not reached: completed allowlist regression, standalone CTests, Gaffer
DeepToFlat/DeepSlice/DeepMerge, remaining holdout-manifest checks and Markdown
link check. Parts D/E were not started: no GPU qualification, OptiX audit,
junkshop5 files, updated source hash/saved node names or three-layer merge.
Nuke remains unavailable.


### Task 16: obsolete hair expectations updated; Part C completion

Changed the current allowlist `particle_hair` expectation from unconditional
`static polygon meshes` rejection to a CPU positive render with zero scanner
BLOCKs and exact FLOAT beauty pixels/headers under Task 14. Added
`particle_hair_tolerance` with the same groom and tolerance 1e-4; it requires
exactly one `hair_tolerance` BLOCK on Feature, the exact renderer message
`hair requires Surface Depth Tolerance 0 for this layer`, and no final EXR.
The shared isolation helper now accepts an explicit target name; prior callers
retain their existing targets. Non-hair expectations were not weakened.

Searched existing tools and deep tests/validators for hair/curve rejection
assertions. No other current-build obsolete assertion was found. The scanner
drift suite compares predictions to rendering without hard-coded hair rejection;
its Task 15 result remains valid. `baseline()` in the hair fixture intentionally
records the pre-curve Part A/B1 rejection baseline and remains historical, not a
current-support gate. Existing explicit unsupported hair cases remain negative.
No renderer rebuild or additional standing-rule repair was needed.

Both affected allowlist suites were rerun from their start via
`python -B D:/temp/cycles_deep_multilayer/run_C_suite_task16.py allowlist-off`
and `allowlist-on`. Both passed 32 opaque cases, 24 opacity cases, the new
particle-hair positive and five rejections. All 88 pre-existing diagnostics-off
deep files remain byte-identical to their reference. The one newly added hair
file is checked separately, with no existing reference removed or replaced.
Both positive beauty pairs passed; the rejection variant passed isolation and
its required diagnostic. No already-passed Task 15 suite was rerun.

Excluded flat metadata, ON -> ordinary deep OFF:

| Suite | Attribute | ON | OFF |
| --- | --- | --- | --- |
| allowlist-off | `Date` | `2026/10/18 17:07:32` | `2026/10/18 17:07:32` |
| allowlist-off | `RenderTime` | `00:00.15` | `00:00.00` |
| allowlist-off | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| allowlist-off | `cycles.Visible.synchronization_time` | `00:00.00` | `00:00.00` |
| allowlist-off | `cycles.Visible.total_time` | `00:00.15` | `00:00.00` |
| allowlist-on | `Date` | `2026/10/18 17:08:05` | `2026/10/18 17:08:05` |
| allowlist-on | `RenderTime` | `00:00.15` | `00:00.00` |
| allowlist-on | `cycles.Visible.render_time` | `00:00.14` | `00:00.00` |
| allowlist-on | `cycles.Visible.synchronization_time` | `00:00.00` | `00:00.00` |
| allowlist-on | `cycles.Visible.total_time` | `00:00.14` | `00:00.00` |

No additional metadata names were introduced. Allowlist opacity maximum raw-oracle
error: 2.42131963368e-08.

Standalone CTests passed 3/3 (capture, reference, density_cell), 7.30 seconds.
This existing standalone configuration does not enable the optional OpenEXR tests;
OpenEXR outputs are independently checked by the hair/allowlist validators.
Gaffer 1.7.2.0 passed all 164 preserved numeric/strict hair files (41 cases x four
outputs), 64 grid pixels each, five depth cuts, and DeepMerge against the first
strand fixture. Maximum errors: flatten 2.53865757638e-07,
slice 1.4860508879e-07,
merge 2.53865757638e-07, each within the source
file's existing bound. The reusable review is `task16/gaffer/hair_review.gfr`;
this is computed consumer validation, not a claim of interactive review.

Holdout-manifest checks passed on the preserved holdout and ordinary strand
fixtures: Strands hash `707e4504` occurs in published IDs in both; only holdout
marks it in `cycles:deepIDHoldoutManifest`, whose keys are a subset of the ID
manifest. Evidence: `task16/holdout-manifests.json`. Event histograms, exact CPU
beauty, repeat identity and all-depth results remain those recorded in Task 14.

Commands also include `ctest_C_task16.py` and `run_gaffer_C_task16.py`, using
existing scratch dependencies read-only. All artifacts/logs are under
`D:/temp/cycles_deep_multilayer/verification/step4/task16/`. CPU-only scope and
rejections are documented in SUPPORT.md and BLENDER.md; M0/M1 results are recorded
in the hair plan. Part D/E and Nuke have not run at this point.

Final Part C Markdown check passed: 35 documents, 200 local links, zero broken; 41 external links unchecked. All Part C gates are complete. No push or PR.

Part C commit: b2ead8e39 (feat(deep): qualify CPU static Ribbon hair). Part D began by reapplying the preserved GPU diagnostics patch after git apply --check passed. Hair remains CPU-only while GPU diagnostics qualify. The guarded incremental rebuild is logged in task16/build-D-driver.log and verification/build.log.

#### Part D: GPU diagnostics build and first failed gate (STOP)

The saved GPU diagnostics patch was reapplied without changing it. The guarded
reset/check/overlay/incremental build passed all 55 build/install steps, including
the long OptiX deep-OSL compilation. No compile repair was needed. The installed
GPU-diagnostics candidate executable SHA-256 is
`a6ec55e06b28ebbaa16825053a91129956606c7927bcc19050ac4439a54447e7`.
GPU hair admission has not been implemented; preflight/scanner still restrict
hair to CPU. The patch file itself remains preserved.

`audit_D_sources.py` passed: all four changed shared files are identical outside
guarded deep code. `run_D_resources.py` passed all 75 common CUDA kernel records
unchanged. Deep-only resource record: 168 registers, 9,296 local bytes, zero
shared bytes, maximum/recommended block threads 384; deep-on cubin 22,747,968
bytes. These are diagnostics-build records, not a completed hair OptiX audit.

`run_D_control.py CUDA` rendered five ordinary deep-OFF controls, one deep-ON
with diagnostics OFF, and one deep-ON with diagnostics ON. The ordinary controls
and deep-ON run passed the existing GPU beauty policy via the unchanged control
validator. Its independent raw/depth/ID checks passed: 1,024 pixels, 4,096 camera
samples, maximum all-depth error 0, median/p95/maximum events 1/1/1, maximum raw
hits 1 and zero overflow pixels. The header bound is 0.0010000000474974513.
Evidence: `task16/D-controls/CUDA.log`, `CUDA-validation.log`,
`CUDA/validation.json` and the saved beauty-policy report within `CUDA/on/`.

The subsequent diagnostics ON/OFF flat identity gate failed on the first file,
`beauty.exr`, after the FLOAT/channel checks and before header comparison:

```text
AssertionError: flat EXR pixels differ
```

Command: regression-tools Python with `-B` ran
`D:/temp/cycles_deep_multilayer/validate_D_diagnostics.py CUDA`.
Evidence: `verification/step4/task16/D-CUDA-diagnostics.log`. This compares the
same-build diagnostics OFF and ON outputs using the Task 14 flat rule. The GPU
beauty-policy PASS above remains a separate result; it does not establish exact
pixel identity for this pair. No claim is made about the cause or size of the
difference. No metadata exclusions were reached in this failing comparison.

Stopped immediately: this is a real pixel comparison failure, outside the
standing metadata, test-code repair, rejection-isolation and obsolete-expectation
extensions. No investigation, repair, gate relaxation or rerender followed.
Diagnostics ON/OFF saved-pass/header identity, deep byte identity and triangle
CSV agreement were not reached. OptiX controls, GPU diagnostic qualification,
GPU hair support and its full SBT/feature/stack audit remain uncompleted. Only a
read-only source look at the existing OptiX pipeline occurred while building;
that is not qualification. Part E was not started: no junkshop5 files, updated
source hash/node-name inspection or BG+Girl+Robots merge. Nuke remains unavailable.

Part C is committed as `b2ead8e39`; there is no Part D commit. Applied GPU
plumbing and this report remain uncommitted. The installed executable is now an
**unqualified Part D diagnostics candidate**, not the Part C executable. The GPU
patch, B1 install/worktree, prior outputs and pre-existing untracked assets remain
intact. No push or PR, and no source .blend opened or saved.


### Task 17: corrected GPU diagnostics gate

Task 17 replaces the inappropriate exact GPU flat-pixel comparison with the
unchanged calibrated GPU beauty policy. Both diagnostics-OFF and diagnostics-ON
must independently pass against the same five ordinary deep-OFF controls. The
hair validator now provides `gpu_diagnostics_flat`: all flat channels remain
FLOAT; headers compare under Task 14, and ON/OFF and ordinary-OFF-pair pixel
spreads are informational. CPU identity gates and deep comparator semantics
are unchanged.

Only validation was rerun on the preserved Task 16 CUDA outputs. Fresh OptiX
controls were rendered with the same candidate executable. Both backends passed
both beauty-policy checks, exact non-run flat headers, whole-file deep byte
identity and strict `compare_deep_identity` identity. Triangle hits/sample CSV
agreement passed on both, with full camera ledgers and matching legacy grid
samples/depths/alpha/event order. CSV presence never establishes publication.

| Backend | Flat file | ON/OFF max abs spread | Max ordinary-OFF pair spread | Both beauty policies |
| --- | --- | --- | --- | --- |
| CUDA | beauty.exr | 3.725290298461914e-09 | 3.725290298461914e-09 | PASS |
| CUDA | render-passes.exr | 3.725290298461914e-09 | 3.725290298461914e-09 | PASS |
| OPTIX | beauty.exr | 3.725290298461914e-09 | 3.725290298461914e-09 | PASS |
| OPTIX | render-passes.exr | 3.725290298461914e-09 | 3.725290298461914e-09 | PASS |

Excluded metadata below applies to both beauty.exr and render-passes.exr for each
backend, before = diagnostics OFF, after = diagnostics ON. No allowlist names
were added.

| Backend | Attribute | Before | After |
| --- | --- | --- | --- |
| CUDA | `Date` | `2026/10/18 17:31:28` | `2026/10/18 17:31:28` |
| CUDA | `RenderTime` | `00:00.27` | `00:00.41` |
| CUDA | `cycles.Visible.render_time` | `00:00.05` | `00:00.12` |
| CUDA | `cycles.Visible.synchronization_time` | `00:00.17` | `00:00.23` |
| CUDA | `cycles.Visible.total_time` | `00:00.22` | `00:00.36` |
| OPTIX | `Date` | `2026/10/18 17:45:17` | `2026/10/18 17:45:18` |
| OPTIX | `RenderTime` | `00:00.40` | `00:00.47` |
| OPTIX | `cycles.Visible.render_time` | `00:00.04` | `00:00.13` |
| OPTIX | `cycles.Visible.synchronization_time` | `00:00.29` | `00:00.26` |
| OPTIX | `cycles.Visible.total_time` | `00:00.34` | `00:00.40` |

Standing test-code repair: after the new beauty-policy call wrote
`cuda_beauty_validation.pixels.csv` in diagnostics-off, the old runner's
`glob('*.csv')` incorrectly treated that analysis report as renderer diagnostics.
The runner now checks only `*.deep.exr.hits.csv` and `*.deep.exr.samples.csv`.
The self-check accepts a policy-report CSV and detects both renderer sidecars.
This is one wrong-path-filter repair; no render assertion, bound or fixture was
changed. The preserved CUDA validation then passed, with no rerender.
Evidence: `task17/CUDA-diagnostics.log`,
`CUDA-diagnostics-after-filter.log`, `sidecar-filter-selfcheck.json` and
`D-validation/CUDA/`. OptiX evidence is `task17/D-controls/OPTIX*`,
`OPTIX-diagnostics.log` and `D-validation/OPTIX/`. The exact policy reports are
saved in each control directory's `on/` and `diagnostics-off/` folders.

Commands: regression-tools Python with `-B` ran
`validate_D_diagnostics_task17.py CUDA`; `python -B run_D_control_task17.py OPTIX`
rendered the fresh controls, then regression-tools Python ran the OptiX validator.
All runners and artifacts remain under `D:/temp/cycles_deep_multilayer/`.
The Task 16 guarded build, source isolation and 75-common-kernel resource audit
remain applicable. No rebuild or renderer change was made for the gate correction.
GPU diagnostics are qualified on CUDA and OptiX and ready for their separate
commit. GPU Ribbon admission, its compiled OptiX audit and Part E follow only
after that commit. No push or PR.
