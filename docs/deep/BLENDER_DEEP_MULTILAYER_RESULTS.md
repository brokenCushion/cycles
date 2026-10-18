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
