# Blender multi-view-layer deep output verification

2026-10-18. Option A1. Branch `blender-deep-multilayer`.
Status (updated 2026-11-22): **adapter implemented; junkshop renders all three
layers.** The Hair BSDF blocker below was resolved by Steps 2-4; see the
[Step 4 conclusion](#step-4-ribbon-hair-m0m2-and-per-layer-overrides) (junkshop5,
Part E). Multi-layer cancellation was verified manually on 2026-11-22 (see
[Cancellation](#cancellation-2026-11-22)), and a full-size production render is
recorded in [production use](BLENDER.md#verified-production-render). Sections below are a
chronological log; earlier STOP and blocker records are superseded by later steps.

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

> Superseded: the shader blocker was resolved in Steps 2-4, and production
> OpenEXR/Gaffer checks later passed (junkshop4/5). Cancellation passed on 2026-11-22.

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


GPU diagnostics commit: `9665a3771` (`feat(deep): GPU raw-hit diagnostics`).
The pre-commit Markdown check passed: 35 documents, 200 local links, zero broken
links; 41 external links unchecked.

#### GPU Ribbon candidate, build and audits

Candidate renderer changes are limited to `src/kernel/deep/surface_cuda.h` and
`src/session/deep.cpp`: accept packed static Ribbon primitives alongside
triangles, restrict the triangle boundary helper to two triangles, and remove
the temporary CPU-only backend guard. The scanner mirrors candidate admission.
Native tmin/self continuation, beauty traversal/intersectors/shaders, volume
capture and existing unsupported shape/motion/OSL/Hair Info/point rules remain
unchanged. The GPU admission is an uncommitted candidate, not a support promotion.

The hair fixture supports CUDA/OptiX selection, five ordinary deep-OFF controls,
beauty-policy metadata and backend-specific volume rejection checks. The runner
uses the existing all-pixel/known-hit/depth/ID oracles and strict/numeric repeats;
GPU beauty always uses the existing policy. It compares flat headers separately
under Task 14. The volume rejection checks were prepared but not reached.

The guarded overlay reset/check/apply/incremental build passed all 34 targets,
including CUDA, OptiX and the long deep-OSL compilation. No compile repair.
Installed GPU-hair candidate SHA-256:
`a1da1a20e605bfae5bc4aa886e46b4666ab522280b2783f5aedb4a9d158711d9`.
Source isolation passed: no changed shared beauty code outside the established
deep-only files. Resource audit passed all 75 common CUDA records unchanged.
Deep-only record: 168 registers, 9,296 local bytes, zero shared bytes,
maximum/recommended block size 384; deep-on cubin 22,751,168 bytes.

OptiX source inspection before the render gate found the existing path:
`KERNEL_FEATURE_HAIR_RIBBON` selects CUSTOM primitive flags; closest/shadow Ribbon
hit groups register `__intersection__curve_ribbon`; Ribbon instances use the
`PG_HITD_CURVE_RIBBON - PG_HITD` SBT offset (16). The deep pipeline adds the
existing hit/miss groups, copies all non-null SBT headers, and replaces only the
triangle-volume slot at offset 3. Its continuation stack combines deep raygen
and maximum hit/any-hit requirements; launch diagnostics already log resource
values. No missing program was identified and none was added. Runtime compiled
hair-feature/SBT/stack qualification was not reached on OptiX and is not claimed.

Commands: `rebuild_D_hair_task17.py`, `audit_GPU_hair_task17.py`,
`resources_GPU_hair_task17.py`, and regression-tools Python `-B` with
`run_GPU_hair_task17.py CUDA single_1_world`, followed by the remaining CUDA
fixtures in their original order. Every render has a 300-second timeout. Evidence
is under `D:/temp/cycles_deep_multilayer/verification/step4/task17/`.

#### CUDA partial hair results

`single_1_world` completed all scheduled checks: numeric ON/repeat and
strict/repeat raw oracles, 64 analytic interior camera samples per render,
finite/sorted EXR validation, numeric ID ownership, deep repeat identity,
GPU beauty policy against all five ordinary controls, and exact non-run flat
headers. Each deep run has 1,024 pixels / 4,096 camera samples, maximum all-depth
error 1.4901161193847656e-08, median/p95/maximum events 0/1/1, maximum raw hits 1,
and zero overflow pixels. Numeric bound is 0.0010000000474974513; strict is 1e-06.

The next case, `single_1_plane`, passed its initial numeric raw/depth/ID/known-hit
checks, five ordinary controls, initial ON beauty policy and flat headers. Its
numeric repeat also passed the raw oracle and strict deep-identity comparison
against ON. Both deep runs have maximum all-depth error 1.4901161193847656e-08,
64 known-hit samples, median/p95/maximum events 1/1/1, maximum raw hits 1 and zero
overflow pixels.

Excluded metadata for the completed hair flat comparisons is below, target
render -> off0. Each row applies to both beauty.exr and render-passes.exr unless
the Files column specifies otherwise. No allowlist names were added.

| Case | Target | Files | Attribute | Target value | off0 value |
| --- | --- | --- | --- | --- | --- |
| `single_1_world` | on | beauty.exr, render-passes.exr | `Date` | `2026/10/18 18:09:58` | `2026/10/18 18:09:59` |
| `single_1_world` | on | beauty.exr, render-passes.exr | `RenderTime` | `00:00.33` | `00:00.22` |
| `single_1_world` | on | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.08` | `00:00.00` |
| `single_1_world` | on | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.19` | `00:00.17` |
| `single_1_world` | on | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.28` | `00:00.17` |
| `single_1_world` | repeat | beauty.exr, render-passes.exr | `Date` | `2026/10/18 18:10:09` | `2026/10/18 18:09:59` |
| `single_1_world` | repeat | beauty.exr, render-passes.exr | `RenderTime` | `00:00.31` | `00:00.22` |
| `single_1_world` | repeat | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.08` | `00:00.00` |
| `single_1_world` | repeat | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.17` | `00:00.17` |
| `single_1_world` | repeat | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.26` | `00:00.17` |
| `single_1_world` | strict | beauty.exr, render-passes.exr | `Date` | `2026/10/18 18:10:11` | `2026/10/18 18:09:59` |
| `single_1_world` | strict | beauty.exr, render-passes.exr | `RenderTime` | `00:00.38` | `00:00.22` |
| `single_1_world` | strict | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.10` | `00:00.00` |
| `single_1_world` | strict | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.21` | `00:00.17` |
| `single_1_world` | strict | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.31` | `00:00.17` |
| `single_1_world` | strict-repeat | beauty.exr, render-passes.exr | `Date` | `2026/10/18 18:10:15` | `2026/10/18 18:09:59` |
| `single_1_world` | strict-repeat | beauty.exr, render-passes.exr | `RenderTime` | `00:01.05` | `00:00.22` |
| `single_1_world` | strict-repeat | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.33` | `00:00.00` |
| `single_1_world` | strict-repeat | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.61` | `00:00.17` |
| `single_1_world` | strict-repeat | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.95` | `00:00.17` |
| `single_1_plane` | on | beauty.exr, render-passes.exr | `Date` | `2026/10/18 18:10:37` | `2026/10/18 18:10:39` |
| `single_1_plane` | on | beauty.exr, render-passes.exr | `RenderTime` | `00:00.42` | `00:00.22` |
| `single_1_plane` | on | beauty.exr, render-passes.exr | `cycles.Visible.render_time` | `00:00.13` | `00:00.00` |
| `single_1_plane` | on | beauty.exr, render-passes.exr | `cycles.Visible.synchronization_time` | `00:00.23` | `00:00.17` |
| `single_1_plane` | on | beauty.exr, render-passes.exr | `cycles.Visible.total_time` | `00:00.36` | `00:00.17` |

#### First failed gate: CUDA plane repeat GPU beauty policy

The `single_1_plane/repeat` beauty-policy assertion returned FAIL. Its saved
report flags one statistical fallback pixel, file coordinate `(17, 9)`, sample
count 4. Ordinary-control leave-one-out fallback counts are min/median/max 0/0/0,
so the existing allowed fallback count is 0; `statistical_count_passed` is false.
The separate sample-count mismatch gate passed (zero unmatched populations),
image-wide bias passed, and unexplained denoised outlier count is zero.

Fallback calibration is also incomplete: the report records five ordinary
controls versus a minimum of 20 for statistical fallback, missing four
seed-varied controls, `informational_only: true`, and `complete: false`.
Per-channel fallback comparisons therefore report missing calibration rather
than measured bound violations. No claim is made that a measured per-channel
limit was exceeded or that this proves a deep-to-beauty leak. The observed
fallback-count failure remains a failed gate under the unchanged policy.

Evidence:
`task17/hair/CUDA/single_1_plane/repeat/cuda_beauty_validation.json` and its
`.pixels.csv`, `hair-CUDA-remaining.log`, plus per-render validation/identity
JSON. `hair/CUDA/first-case-results.json` preserves the completed world case;
`hair/CUDA/results.json` preserves plane progress before the failing policy.

Stopped at that first real render gate failure. No pixel investigation,
calibration/control additions, snapshot diagnostic, renderer repair, policy
change or retry followed. The plane strict runs and later CUDA fixtures were
not rendered. OptiX hair tests and runtime audit, grazing/tip native controls,
GPU volume-rejection tests, cross-backend alpha comparison, GPU consumer review,
GPU hair commit and Part E were not reached. No junkshop5 files, new source hash,
saved node-name inspection or three-layer merge; Nuke remains unavailable.

HEAD is `9665a3771`, the qualified GPU diagnostics commit. GPU hair renderer,
scanner and test changes and this report remain uncommitted. The installed
executable is the unqualified GPU-hair candidate. The original GPU patch, B1
install/worktree, prior outputs and untracked assets remain preserved. No push
or PR; no source .blend was opened or saved. The final Markdown check after the
GPU-hair candidate/report was not run because the gate failed.


### Task 18: complete GPU calibration and bounded snapshot resolution

Completed CUDA single_1_plane calibration first, as authorized. The five existing
ordinary controls matched the target executable, fixture-source hash and every
beauty-policy setting. Added exactly 15 compatible ordinary controls (20 total)
and four deep-OFF controls with fixed seeds 101, 202, 303 and 404, in one Blender
process. Every new control records executable/source identity, camera, sampling,
backend and settings. All ordinary controls enter the pool; none were selected
or removed for their results. The target fixture script and renderer were not
changed for calibration.

With full calibration, original ON passed without fallback. The preserved repeat
still had one fallback pixel (17,9), versus ordinary leave-one-out min/median/max
0/0/0. Bias and the separate sample-count mismatch gate passed. All channels at
the fallback pixel met their absolute step-2 bounds; the fallback-count condition
still required step 4. Original informational reports and the fully calibrated
unresolved report are preserved separately.

| Channel at (17,9) | Signed difference | Ratio | Absolute limit | Channel passes |
| --- | --- | --- | --- | --- |
| `Visible.Debug Sample Count.X` | 0 | 0 | 4.76837158203125e-07 | True |
| `Visible.Denoising Albedo.R` | 0 | 0 | 4.76837158203125e-07 | True |
| `Visible.Denoising Albedo.G` | 0 | 0 | 4.76837158203125e-07 | True |
| `Visible.Denoising Albedo.B` | 0 | 0 | 4.76837158203125e-07 | True |
| `Visible.Denoising Depth.Z` | 0 | 0 | 1.9073486328125e-06 | True |
| `Visible.Denoising Normal.X` | 0 | 0 | 4.76837158203125e-07 | True |
| `Visible.Denoising Normal.Y` | 1.77635683940025e-15 | 9.926962279806434e-08 | 4.76837158203125e-07 | True |
| `Visible.Denoising Normal.Z` | -5.960464477539062e-08 | 1.452111063296408e-06 | 4.76837158203125e-07 | True |
| `Visible.Combined.R` | 0 | 0 | 1.490116119384766e-08 | True |
| `Visible.Combined.G` | 0 | 0 | 1.490116119384766e-08 | True |
| `Visible.Combined.B` | 0 | 0 | 1.490116119384766e-08 | True |
| `Visible.Combined.A` | 0 | 0 | 4.76837158203125e-07 | True |

Applied only the existing six-file snapshot experiment from `9a017f055` to the
managed Blender overlay, outside the deep repository. Patch check passed. The
161-target diagnostic build passed and installed separately in
`D:/temp/cycles_deep_multilayer/install-snapshot-task18/`. The ordinary install
was never replaced. All six overlay files were restored after installation;
every recorded ordinary executable/kernel hash and the deep repository's
uncommitted diff remained unchanged through the build. Diagnostic patch SHA-256:
`f36f32eec30e36057eb0c76be07dcb8cfcb420a0b75c5241cb14c6ab967738df`. Diagnostic executable SHA-256:
`45dcec6299e4e8982991355b2643be8231fa86a10d5188221e284ba5a7eac99b`. Target executable remains
`a1da1a20e605bfae5bc4aa886e46b4666ab522280b2783f5aedb4a9d158711d9`. The shared build cache now contains diagnostic
objects; restored source timestamps require a normal rebuild before any future
ordinary installation from that cache. Existing ordinary renders continue to use
the preserved ordinary install.

Rendered three independent OFF and three independent ON diagnostic runs, with
factory scene reset between renders. They are excluded from the ordinary pool.
At (17,9), all six runs were **bit-identical across all 12 checked channels**,
accepted sample count 4. The unchanged snapshot reproduced-state rule passed,
resolving the repeat's beauty flag. Calibrated original failure remains recorded;
resolution does not substitute for the target's already-passed deep-alpha oracle.

The plane strict and strict-repeat modes were then completed with the same
20-control/four-seed calibration. Strict-repeat flagged (17,15); the same six
compatible diagnostic runs resolved it by the unchanged four-ULP, exact-count
rule. At this second pixel, agreement passed but **bit identity did not**.
Every diagnostic ON matched at least one complete OFF state across all channels.
The numerical bound and count policy were not changed.

Target numeric capture: error 0.0010000000474974513, requested deep samples 0
(all four), IDs ON, diagnostics ON, effective Volume OFF, tolerance 0, events 64,
memory 512 MiB. Strict targets use error 0 and IDs OFF. The diagnostic ON runs
use the numeric capture options above. Original Task 17 metadata omitted the
three deep-option fields; target EXR headers and the immutable fixture settings
are recorded in `plane-policy/resolution-summary.json`, separately from the
policy's null legacy metadata fields. New target/diagnostic render metadata
records the actual requested options explicitly. Matching beauty/camera/sampling
settings and the diagnostic executable hash were verified by the policy.

Initial new-control fixture/render/export time: 21.563812 seconds;
six diagnostic renders: 8.035674 seconds.
Diagnostic build/install time: 1352.831 seconds, reported separately
from render time. Remaining fixtures and final total render time follow below.

Evidence under `verification/step4/task18/`: `plane-control-plan.json`, its
`-rendered.json`, control command/log/time JSON, `plane-policy/on.json`,
`repeat.json`, `repeat-resolved.json`, `resolution-summary.json`, and
`snapshot/snapshot-build.json`, `preservation.json`, `six-render-records.json`.
Full per-mode plane policy reports are under `policy/CUDA/single_1_plane/`;
all channel ratios/limits and the six diagnostic channel vectors remain saved.
Commands used `run_plane_controls_task18.py`, `validate_plane_calibration_task18.py`,
`build_snapshot_task18.py`, `run_snapshot_plane_task18.py`,
`resolve_plane_snapshot_task18.py`, and `full_hair_task18.py CUDA single_1_plane`.
All generated files/caches are under `D:/temp/cycles_deep_multilayer/` and scratch
runtime dependencies were read-only. No source .blend has been opened or saved.

#### Task 18 stop report: incomplete OptiX artifacts after interruption

**STOP: calibration artifact integrity is unresolved.** The OptiX process/session
was no longer available while preparing the next transmission-constant fixture.
No exit code or renderer error was recovered. Inspection found 39 persistent
NUL-filled JSON files, confined to the transmission-opaque/constant run artifacts.
An OpenEXR read audit then stopped at:
`calibration/OPTIX/transmission_constant/ordinary08/transmission_constant/off0/beauty.exr`.
The reader reported "Unable to open ... for read". That control also lacks
`render.json`. This is invalid/incomplete artifact data, not evidence of a
numerical beauty-policy regression. Its cause has not been established.

The audit read 79 preceding EXRs successfully and stopped on the 80th. Corrupt
files were preserved; no outputs or references were replaced, and no further
renders or qualification gates ran after this unresolved artifact. Evidence:
`interruption-json-audit.json`, `interruption-exr-audit.json`, `OPTIX-full.log`,
and the interrupted fixture's `controls.log`, all under the Task 18 root.

CUDA completed all 43 cases: 41 positive fixtures in ON, repeat, strict and
strict-repeat modes (164 mode results), plus the expected 17/65-chain capacity
rejections without a final deep publication. Known hits, all-pixel raw/depth/ID
oracles, valid EXRs, same-backend repeat identity, flat-header identity and fully
calibrated beauty passed in those completed results. OptiX's durable progress and
policy reports record 29 positive fixtures passing their four modes (116 results),
through `transmission_opaque`. Some supporting run-state, identity and summary
JSON files for that last fixture are among the damaged files; this is explicitly
retained as incomplete phase evidence, not a completed OptiX qualification.

The next fixture, `transmission_constant`, has no completed calibration pool.
Seven ordinary controls retain readable metadata and EXRs; ordinary07's metadata
is NUL-filled, and ordinary08 has the unreadable EXR. No seed controls were
completed for this fixture. The remaining 14 OptiX cases, including this fixture,
are incomplete. Nothing was selected or excluded based on favorable pixel values.

Each completed fixture/backend used all 20 compatible ordinary controls and
exactly four controls with distinct fixed seeds 101/202/303/404. CUDA therefore
used 820 ordinary + 164 seed controls; the 29 completed OptiX fixtures used
580 + 116. Ten CUDA ordinary controls predate Task 18; all 1,670 newly completed
control invocations are included in the timing below. Executable/source/camera/
sampling/settings checks are recorded per control and in the policy reports.
Fixture source SHA-256 remained
`17dc9f4ca9ade8680e09be3d04aa1bd1940781a7e4d87b741f1d8e445fdbb549`;
ordinary executable SHA-256 remained the `a1da1a20...` value recorded above.

The following table uses the original durable calibrated policy reports, plus
any separately saved snapshot resolution. It does not repair corrupt summary
files or rerun a comparison. O/R/S/SR means ON/repeat/strict/strict-repeat;
event statistics and maximum all-depth error are from each completed ON result.
All completed rows used 20 ordinary / 4 seed controls and passed their recorded
mode gates. Complete per-pixel/channel ratios, absolute limits, floors, bias and
count results remain in `policy/{backend}/{fixture}/*-calibrated.json`; snapshot
results are separate `*-resolved.json` files.

| Backend / fixture | Ordinary fallback min/median/max | Deep fallback O/R/S/SR | Events median/p95/max | Max all-depth error |
| --- | --- | --- | --- | --- |
| CUDA `single_1_plane` | 0/0/0 | 0/1/0/1 | 1/1/1 | 1.49011612e-08 |
| CUDA `single_1_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.49011612e-08 |
| CUDA `single_4_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.49011612e-08 |
| CUDA `single_4_plane` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `single_1_constant_world` | 0/0/1 | 0/0/0/0 | 0/1/1 | 1.55874003e-09 |
| CUDA `single_4_constant_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.55874003e-09 |
| CUDA `dense` | 0/1/3 | 2/2/1/2 | 9/15/16 | 6.33239339e-09 |
| CUDA `fold` | 0/0/0 | 1/0/0/0 | 1/2/2 | 1.7491012e-09 |
| CUDA `adjacent` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| CUDA `interleaved` | 0/0/1 | 0/0/0/0 | 1/1/4 | 4.09612322e-09 |
| CUDA `two_objects` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `instance` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `junkshop` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `indirect` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| CUDA `excluded` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| CUDA `camera_off` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| CUDA `shared` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `holdout` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `occluded` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| CUDA `occluding` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `frame1` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `frame2` | 0/0/0 | 0/0/1/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `grazing` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `tip` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `inside` | 0/0/0 | 0/0/0/0 | 0/0/0 | 0 |
| CUDA `legacy_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `legacy_constant` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| CUDA `legacy_varying` | 0/0/0 | 0/0/0/0 | 1/1/2 | 2.98023224e-08 |
| CUDA `transmission_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `transmission_constant` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| CUDA `transmission_varying` | 0/0/0 | 0/0/0/0 | 1/1/2 | 2.98023224e-08 |
| CUDA `chiang_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `chiang_constant` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| CUDA `chiang_varying` | 0/0/0 | 0/0/0/0 | 1/1/2 | 2.98023224e-08 |
| CUDA `huang_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| CUDA `huang_constant` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| CUDA `huang_varying` | 0/0/0 | 0/0/0/0 | 1/1/2 | 2.98023224e-08 |
| CUDA `chain_8` | 0/0/0 | 0/0/0/0 | 0/0/8 | 5.21878252e-09 |
| CUDA `chain_16` | 0/0/1 | 0/0/0/0 | 0/0/16 | 4.53736304e-09 |
| CUDA `chain_32` | 0/0/0 | 0/0/0/0 | 0/0/32 | 5.44637646e-09 |
| CUDA `chain_64` | 0/0/0 | 0/0/0/0 | 0/0/64 | 4.65561689e-09 |
| OPTIX `single_1_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 0 |
| OPTIX `single_1_plane` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| OPTIX `single_4_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.49011612e-08 |
| OPTIX `single_4_plane` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `single_1_constant_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.55874003e-09 |
| OPTIX `single_4_constant_world` | 0/0/0 | 0/0/0/0 | 0/1/1 | 1.55874003e-09 |
| OPTIX `dense` | 0/1/5 | 3/2/1/2 | 9/14/16 | 6.33239339e-09 |
| OPTIX `fold` | 0/0/0 | 0/0/0/0 | 1/2/2 | 1.7491012e-09 |
| OPTIX `adjacent` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| OPTIX `interleaved` | 0/0/0 | 0/0/0/0 | 1/1/4 | 4.09612322e-09 |
| OPTIX `two_objects` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `instance` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `junkshop` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `indirect` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| OPTIX `excluded` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| OPTIX `camera_off` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| OPTIX `shared` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `holdout` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `occluded` | 0/0/0 | 0/0/0/0 | 1/1/1 | 0 |
| OPTIX `occluding` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `frame1` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `frame2` | 0/0/1 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `grazing` | 0/0/0 | 0/0/0/0 | 1/1/1 | 7.4505806e-09 |
| OPTIX `tip` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `inside` | 0/0/0 | 0/0/0/0 | 0/0/0 | 0 |
| OPTIX `legacy_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |
| OPTIX `legacy_constant` | 0/0/0 | 0/0/0/0 | 1/1/2 | 1.55874003e-09 |
| OPTIX `legacy_varying` | 0/0/0 | 0/0/0/0 | 1/1/2 | 2.98023224e-08 |
| OPTIX `transmission_opaque` | 0/0/0 | 0/0/0/0 | 1/1/1 | 1.49011612e-08 |

CUDA `chain_16` and `chain_64` reached exactly 16 and 64 events. Their next-hit
cases rejected explicitly. No overflow pixels occurred in positive published
outputs. Dense-groom maximum events were 16 on each GPU; CPU's earlier 19 is an
informational backend difference until the required cross-backend alpha gate runs.

All four still-flagged CUDA pixel occurrences resolved with the existing six-render
majorant-snapshot procedure. These were three independent fixture groups, each
with three OFF and three ON runs; the plane group serves both plane pixels.
The following distinguishes agreement from bit identity explicitly:

| Fixture / mode | Pixel | Ordinary min/median/max; deep fallback | Six-run agreement / exact counts | Bit-identical |
| --- | --- | --- | --- | --- |
| `single_1_plane/repeat` | (17, 9) | 0/0/0; 1 | Pass / 4 accepted samples | True |
| `single_1_plane/strict-repeat` | (17, 15) | 0/0/0; 1 | Pass / 4 accepted samples | False |
| `fold/on` | (13, 16) | 0/0/0; 1 | Pass / 4 accepted samples | True |
| `frame2/strict` | (19, 22) | 0/0/0; 1 | Pass / 4 accepted samples | True |

No OptiX flag required snapshot resolution before the interruption. Snapshot
vectors, diagnostic options and all six run paths are saved in the resolved
reports. Original failed calibrated reports were retained. No bias, separate
sample-count mismatch or absolute-channel bound was waived; no policy constant,
ULP floor, minimum-control or count rule changed. Snapshot renders remain outside
the ordinary pools and the six-file diagnostic patch remains outside this branch.

Recorded complete calibration fixture/render/export time was **2099.423150 s**:
CUDA 1209.127672 s; OptiX 890.295478 s.
Seven additional interrupted-batch invocations retain logged durations totaling
8.929075 s, giving a recorded lower bound of
**2108.352226 s
(35 min 8.352 s)** spent on Task 18 calibration. The exact full total is unavailable:
ordinary07/08 timing records did not survive. This is not an estimate that silently
counts the missing durations as zero. The 18 separate snapshot render invocations
took 24.326045 s; diagnostic build/install took 1352.830896 s. Reused
Task 17 controls, target qualification renders and idle/interruption time are not
charged as new calibration invocations. Full recorded totals/paths are in
`report-calibration-at-stop.json`.

Partial OptiX audit findings, recorded before STOP: installed ordinary
`kernel_optix.ptx.zst` (564,410 decompressed bytes, SHA-256
`c0d2c5db36a3f3f049f1c07622ffec7efe625938bd8df09334f73d4b736e9d8c`)
and `kernel_optix_deep.ptx.zst` (10,066,866 bytes, SHA-256
`61d8f202c59c1f118de17b59694bb713bca0ae442837b22001526cdd62e358c3`)
both contain `__intersection__curve_ribbon`. Runtime logs report Hair enabled
and native OptiX rendering. Source selects CUSTOM for Ribbon, uses the existing
closest/shadow Ribbon groups at offsets 16/17 (volume/local 18/19 ignored), copies
24 hit records into the deep SBT, and replaces only triangle volume slot 3.
Closest surface tracing uses ray offset 0. Stack sizing includes all linked
closest-hit and intersection+anyhit requirements. Observed first-strand and
instance logs report raygen/continuation 6464 bytes, anyhit/direct-callable 0,
launch parameters 5296 bytes. No additional programs were added. Evidence:
`optix-compiled-symbols.json`, `launches/OPTIX/single_1_world/on.log`,
`launches/OPTIX/instance/on.log`; Task 17's 75 unchanged common CUDA resources
and source audit remain applicable to the unchanged ordinary executable.
The final all-case audit was not run and is not claimed complete.

Standing-rule reporting: the new runner initially assumed frame 0001 and raised
FileNotFoundError on the already-rendered frame-2 EXR. It was repaired once to
require exactly one published deep EXR. Its self-check accepted frame-1/frame-2
files and rejected zero/multiple files (`frame-path-selfcheck/results.json`).
Validation resumed on preserved frame-2 data; no renderer change, bound, case or
reference changed. The report-only generator later hit UnicodeDecodeError from
the Windows default encoding before writing. Explicit UTF-8 fixed that API use;
the actual report decoded successfully and invalid UTF-8 was rejected in its
self-check (`report-encoding-selfcheck.json`). No render artifact changed. An interim summary command later raised JSONDecodeError.
Its initial concurrent-write explanation was superseded by the persistent NUL
findings above. This is not being repaired as a test-code bug: damaged artifacts
were not rewritten or regenerated. The successful report aggregation reads only
surviving original policy/progress records, with the loss stated explicitly.

Commands include `full_hair_task18.py CUDA` (resumed in the recorded fixture
order after the frame-path repair), `full_hair_task18.py OPTIX`,
`inspect_optix_ptx_task18.cjs`, and the read-only artifact/report commands recorded
in the conversation. Plans, exact Blender commands, hashes, elapsed-time records
and stdout are under `verification/step4/task18/`. The later standalone GPU
Gaffer/native/rejection/cross-backend runners and junkshop5 launch scripts were
prepared under the allowed output root but **not executed**.

M2 remains unqualified. Native grazing/tip controls, GPU volume-mode rejection,
GPU Gaffer consumer checks, final OptiX audit, cross-backend alpha and Part E did
not run; no native-backend waiver was attempted. Consequently there is no GPU
hair commit, junkshop5 output list or BG+Girl+Robots merge result. Published scope
stays CPU-only. Existing CPU qualification and GPU diagnostics commit are retained.
No Nuke test, push, PR, source .blend open or source .blend save occurred.
The GPU-hair candidate, prior patch, untracked assets and all damaged evidence
remain intact. HEAD is still `9665a3771`; no commit was made for this stopped part.
The ordinary install is the unqualified GPU-hair candidate; the separate snapshot
install is diagnostic-only. Rebuild ordinary objects before any future ordinary
installation from the shared diagnostic build cache.

Report checks: `python -B tools/check_markdown_links.py` passed (35 documents,
200 local links, no broken links; 41 external links were not checked).
`git diff --check` passed. Evidence: `task18/markdown-final.json`.

### Task 19: crash recovery

Task 19 identifies the Task 18 interruption as a Windows MEMORY_MANAGEMENT
bugcheck **0x1A**, not a renderer/validation result: event 6008 records unexpected
shutdown at 19:12:55 on 2026-10-18; events 41/1001 record the reboot/bugcheck at
19:42, with `C:/WINDOWS/Minidump/101826-10375-01.dmp`. These are the verified
findings supplied in the task file; no additional hardware diagnosis or minidump
inspection was performed. The user is investigating hardware separately.

**STOP at the clean-executable identity gate.** A fresh ordinary build completed
all **8174 steps**, with no renderer/adapter compilation failure.
Its executable hash differs from Task 18. Task 19 explicitly requires stopping
before rebuilding any pool when the executable changes. No qualification render,
new control pool, installation, GPU-hair commit or Part E run followed this gate.

| Artifact | SHA-256 |
| --- | --- |
| Preserved `install/blender.exe` | `a1da1a20e605bfae5bc4aa886e46b4666ab522280b2783f5aedb4a9d158711d9` |
| Fresh `build-task19-clean2/bin/blender.exe` | `fdf12139581daa4240d2e4f5047680b53c5c9a80c7cbb339703f406b3da96d38` |

The original installed executable remains byte-identical and was not replaced.
The fresh directory reused no objects from the shared diagnostic build cache.
All 376 modified/untracked overlay source files and the
set of changed files remained unchanged through the build. The Blender base is
`749518deb2f0735a22361a07488b35f7ea5c2fdf`; the GPU-hair candidate source/tool
blobs match the Task 18 snapshot's recorded patch, and the six restored source
files match the ordinary-source backups. Original configuration **values** all
match; only cache types for `CMAKE_MAKE_PROGRAM`, `CUDA_TOOLKIT_ROOT_DIR` and
`LIBDIR` changed from UNINITIALIZED to STRING when loaded into the fresh cache.
Their values did not change. No policy, source feature or compiler option was
changed to obtain an identity result.

There is a concrete build-metadata difference: ordinary build time
**06:46:59 UTC**, clean build time **11:02:13 UTC**, both
2026-10-18. The same Blender buildinfo generator embeds these values in the
executable (`build_files/cmake/buildinfo.cmake:179-183`). Thus identical code and
option values do not establish byte identity. This records an observed contributor
to the hash change, not proof that timestamps are its only differing bytes.
The fresh build directory also differs. Configure/build wall time was approximately
2407.515 s; no rendering is included.

Evidence: `D:/temp/cycles_deep_multilayer/verification/step4/task19/`
`clean-build-provenance.json`, `clean-build-result.json`, `clean-configure.log`,
`clean-build.log`, the exact configure/build command JSON, cache initializer,
and `ordinary-before-version.txt`. The new executable was not deployed: the
build's own final message requires install/runtime staging. A report-only
`--version` probe on the unstaged build-tree binary raised WinError 14001;
version/build-time reporting was repaired to read generated `buildinfo.h`, with
known-good/invalid-header self-checks. The successful build and SHA measurements
were retained; no install or render was attempted to work around the hash gate.

#### Atomic runner writes and immediate EXR reads

The active qualification entry points now install a shared test-only I/O adapter
under the allowed output root: same-directory temporary file, flush, `os.fsync`,
close, then `os.replace`. It covers their JSON, CSV and log-summary writes,
including imported validators' `Path.open`/`write_text` writes. Captured process
logs remain temporary while running and are published only after completion.
The Node PTX inspector now emits JSON to stdout; its Python wrapper publishes
that JSON atomically. Runner sources/backups and hashes are recorded in
`runner-repair-manifest.json`; the numerical validators and the frozen Blender
fixture source were not edited. Fixture SHA-256 remains
`17dc9f4ca9ade8680e09be3d04aa1bd1940781a7e4d87b741f1d8e445fdbb549`.

Six real child-process mid-write crash checks passed: JSON/CSV/log, each with an
old final file or no final file. Exit 77 during the write left the old final bytes
or no final file, never a partially published final file. Normal replacement also
passed. Evidence: `atomic-selfcheck.json` and its retained test inputs/temporaries.

Blender's native EXR writes are unchanged. After each render/export, the runner
opens every EXR and reads every channel payload in every part before accepting
that render. Failure records `render_failed: true` atomically and stops the run.
The self-check accepted a preserved deep EXR, rejected a truncated EXR and
recorded it as a failed render; a synthetic two-part EXR confirmed both parts'
channels are read. Evidence: `exr-reader-selfcheck.json`,
`multipart-reader-selfcheck.json`, `verify_exr_task19.py` and
`verify_after_render_task19.py`. No GPU renderer was used for these reader tests.

Resume checkpoints are written only for fully verified fixture-stage results.
Existing incomplete fixture directories are blocked, so partial pools cannot
silently resume. A prior active GPU-run record blocks automatic retry and reports
a possible repeated hardware/driver interruption. The clean-build identity guard
also rejects rendering until the executable gate passes. Good-idle and bad-hash/
interrupted-run guard self-checks passed (`restart-selfcheck.json`). These guards
were prepared and checked without launching the remaining GPU work.

Standing test-code repairs were limited to the new runner/report code:

- The first source check compared raw CRLF/LF bytes and rejected equivalent
  exported source. Universal-newline UTF-8 comparison accepts equivalent line
  endings and rejects changed code (`source-check-selfcheck.json`). No source
  file was repaired or altered.
- The first fresh configuration could not find Ninja because the cache loader
  omitted UNINITIALIZED entries. It now preserves the three original values;
  valid Ninja entries and invalid cache text were self-checked. The original
  Ninja version was checked. Failed configuration evidence is retained in
  `configure-before-cache-type-repair.log` and its result JSON. A second,
  genuinely fresh directory was configured and built; no partial cache was reused.
- The unstaged-binary version probe and its reporting repair are described above
  (`version-probe-selfcheck.json`). Runner preparation also normalizes line endings
  before writing adapters and syntax-checks them. No render comparison, bound,
  reference, seed, fixture or assertion was removed or relaxed.

#### Full read-only audit and quarantine

The audit completed **before** moving any Task 18 input. It covered both backends,
all Task 18 artifacts of the requested types, and referenced Task 17 fixture data:
**14,166 files** — 8,699 JSON,
4,250 EXR, 857 CSV and
360 logs. JSON was parsed; EXRs were opened and channel
payloads read, and text was also checked for NULs. Read-only audit time was
35.695 s. A supplemental all-parts payload check confirmed all
4,246 readable EXRs have one part and every channel was read.

**46 findings** are individually listed with exact paths/errors in `damaged.json`
and `damaged.csv`: 39 NUL-damaged JSON, three NUL-damaged policy CSVs, two truncated
OptiX EXRs, and two deliberately invalid earlier frame-path self-check EXRs.
Thus **44 are crash-damaged**; the other two are known negative-test artifacts.
The latter were still quarantined, not relabeled as valid outputs. There was no
actual CUDA fixture damage. Complete records are in `audit.json` and
`all-exr-parts.json`; no damaged input was repaired in place.

Quarantine root:
`D:/temp/cycles_deep_multilayer/verification/step4/task18/quarantine-crash-2026-10-18/`.
All 267 files (4,617,827 bytes) in these 12 units were moved while
retaining their paths relative to Task 18. Absolute source/destination scope was
checked before native PowerShell moves; every moved file's SHA-256 was checked
again at its destination. No files were deleted. This includes the entire partial
constant pool, the affected opaque fixture/supporting data, damaged negative-test
files and old aggregate results. The original aggregate OptiX progress was
quarantined; a new atomic progress file contains only the 28 retained fixtures.

| Quarantined relative path | Files |
| --- | --- |
| `calibration/OPTIX/transmission_opaque` | 126 |
| `calibration/OPTIX/transmission_constant` | 48 |
| `hair/OPTIX/transmission_opaque` | 38 |
| `hair/OPTIX/transmission_constant` | 9 |
| `launches/OPTIX/transmission_opaque` | 20 |
| `launches/OPTIX/transmission_constant` | 5 |
| `policy/OPTIX/transmission_opaque` | 16 |
| `frame-path-selfcheck/ambiguous/a.deep.exr` | 1 |
| `frame-path-selfcheck/ambiguous/b.deep.exr` | 1 |
| `OPTIX-hair-progress.json` | 1 |
| `report-calibration-at-stop.json` | 1 |
| `calibration-summary.json` | 1 |

The complete **per-file** quarantine list, original/new paths, lengths and hashes
is `task19/quarantine-plan.json`; the checked move result is
`task19/quarantine-result.json`. These are under
`D:/temp/cycles_deep_multilayer/verification/step4/`. All 46 audit findings are
covered. The quarantine is retained intact.

#### Kept fixtures, required reruns and remaining work

CUDA retains all **43** fixture-stage results: 41 positive fixtures, four modes
each, plus the two expected capacity rejections. OptiX retains **28** intact
fixtures, four modes each. Every one of the **276** retained beauty-policy
evaluations was recomputed from preserved data and reproduced the original report
in full, excluding only the new output CSV path. This took 193.378 s.
The four CUDA snapshot-resolved pixel occurrences reproduce too, including the
previous distinction between bit identity and agreement. Input controls were not
rerendered, dropped or selected. All policy constants and the existing 20-control/
four-seed rules remain unchanged. Separate new reports are under `task19/policy/`;
`kept-fixtures.json` lists every kept case and original/new report, and
`policy-reevaluation-progress.json` records each comparison.

**No affected fixture has yet been rerendered**, because the clean executable
failed identity first. `transmission_opaque` must rerun all four target modes and
policy evaluation; its complete 20 ordinary/four-seed controls remain preserved
in quarantine, audited, and would be rechecked before reuse only with the matching
executable. `transmission_constant` must receive a fresh 20 ordinary + seeds
101/202/303/404 pool; none of its partial controls can be reused. Along with the
remaining unrun cases, this leaves 15 OptiX fixtures to execute (the originally
remaining 14 plus damaged opaque). Exact planned order is derived from the named
fixture list, never from favorable results.

Task 19 added **zero calibration renders** and **zero target GPU renders**.
Task 18's recorded calibration lower bound of 2108.352226 s remains historical;
its unknown interrupted timing is not invented or counted as zero. No new pool
was rebuilt and no executable identities were mixed. The fresh binary differs
from the old controls' executable, so it was not installed or used to render.

The final all-case OptiX audit, native grazing/tip controls, GPU volume rejection,
GPU Gaffer checks, cross-backend flattened-alpha gate, GPU-hair commit and
junkshop5/Part E are **unrun**. The GPU dense-groom event maximum 16 versus CPU 19
remains informational; no cross-backend waiver was invoked. There are no new
junkshop5 files, Girl event histograms or BG+Girl+Robots merge results to claim.
Nuke was not run. The source junkshop .blend was not opened or saved.

Published support remains CPU-only. The GPU-hair candidate, untracked assets,
original GPU patch and diagnostic install are preserved. No commit, push or PR;
HEAD remains `9665a3771`, branch `blender-deep-multilayer`. The original ordinary
install still has hash `a1da1a20...`; the fresh artifact is in
`D:/temp/cycles_deep_multilayer/build-task19-clean2/`. The original shared build
cache was not used or cleared and still contains the earlier diagnostic objects;
it must not be treated as the new ordinary build directory.

Task 19 final report checks: the repository Markdown link checker passed;
`git diff --check` passed. Outputs are preserved as `task19/markdown-final.json`
and `task19/git-diff-check.log`. These checks did not run rendering or qualification.

#### Task 20: qualification executable decision and replacement checks

The user replaced Task 19's reproducible-executable hash gate. Qualification
continues with the preserved ordinary executable
`a1da1a20e605bfae5bc4aa886e46b4666ab522280b2783f5aedb4a9d158711d9`.
The clean build is never installed or used for controls. The pre-diagnostic
ordinary binary predates the snapshot build; the shared cache cannot change it.

Source identity passed: all 1,105 generated overlay entries reproduce the
recorded `deep-overlay.json` (SHA-256 `ad41b3655e215c5631086e5358593586f125d4892f6f32c51b9986c757836d35`),
and all actual overlay files match. Both modified compiled source blobs match
Task 18's pre-diagnostic candidate patch; every other tracked `src/` file remains
at HEAD `9665a3771`, with no untracked source. All 308 recorded cache options match.
Task 19's runner/atomic changes live outside the Blender source in
`D:/temp/cycles_deep_multilayer/`; none are compiled into Blender.

The initial in-memory checker decoded pristine baseline comments as UTF-8, while
the original generator used Windows cp1252. This test-code error changed only
five Intel trademark comment lines. Reproducing the original decoding made the
manifest exact; no source was edited. Known-good and changed-source checks are
recorded in `task20/identity-checker-selfcheck.json`.

Kernel/data provenance passed: all 13 installed GPU kernels match clean output
SHA-256 values exactly (12 OptiX PTX archives and one CUDA cubin archive).
All 452 Cycles package files are mapped to clean-build installation inputs.
The only 103 differences are OSL `.oso` files: replacing the clean build directory
with the old directory in their `# options` comment makes every byte equal.
No executable shader instruction differs. Full filenames, both hashes and reasons
are in `task20/kernel-data-identity.json`. All 305 shared Blender data files also
match exactly (`task20/shared-data-identity.json`). The supplementary data map
initially missed 49 locale outputs because their CMake rule renames the source
basename to `blender.mo`; honoring RENAME fixed this runner path bug, with
known-good/missing-basename self-checks in `task20/install-map-selfcheck.json`.

The executable comparison is informational, as authorized. Both files are
113,864,192 bytes. PE timestamps are 2026-10-18 07:08:39 UTC (ordinary) and
11:41:35 UTC (clean). Debug GUIDs are `48062d64-b392-fcae-4c4c-44205044422e`
and `861a1b5a-e48e-39fc-4c4c-44205044422e`; PDB paths embed their build directories.
The date string is identical; the time changes from 06:46:59 to 11:02:13 at
file offset 106642091. Differences also occur outside these metadata regions:
8,932,302 bytes in 1,165,837 contiguous ranges. Section counts are `.text`
210,370, `.rdata` 8,485,341, `.data` 4,127, `.pdata` 231,850 and `.reloc` 607;
`.tls` and `.rsrc` are identical. These differences are **not** claimed to be
explained solely by timestamps. Every differing range and its per-file SHA-256
is recorded in `task20/exe-differing-ranges.csv`; PE fields, section counts and
build strings are in `task20/executable-differences.json`. Task 20 explicitly
permits continuing because this clean executable is never used.

The install guard accepts the recorded binary and rejects a known-bad file.
Parent runners check before launching Blender; batched fixture and production
runners check before each render. The replacement gate is
`task20/identity-gate.json`. The repeated-crash marker and atomic output writes
remain active. Affected OptiX fixtures are being rerun; final results follow
below. No GPU-hair commit is made until its gates pass.

The 13 compressed GPU kernels were also decompressed and compared with every
corresponding clean raw PTX/cubin output: all match exactly
(`task20/raw-kernel-identity.json`). No installed fatbin or other GPU archive is
omitted. After this evidence was recorded, automatic approval review rejected
native PowerShell removal of `build-task19-clean2` as "blocked by policy".
The directory is retained; no alternate removal was attempted. It remains
uninstalled and unused for qualification (`task20/clean-build-cleanup.json`).

##### Task 20: Part D qualification completed

Both CUDA and OptiX pass all 43 fixture stages: 41 positive fixtures in all four
modes (`on`, `repeat`, `strict`, `strict-repeat`) and the two expected capacity
rejections. All 328 positive mode results pass the independent raw oracle,
all-depth cuts, known-strand checks where applicable, valid sorted EXR output,
ID ownership and calibrated GPU beauty policy. All 164 numeric/strict repeat
comparisons pass. Every positive output reports zero overflow pixels. The
17-at-16 and 65-at-64 cases fail explicitly without any final Deep EXR; their
failed output cannot provide a published overflow-pixel histogram.

The 28 intact OptiX fixtures and all 43 CUDA fixture stages were retained from
the Task 19 audit/policy reproduction. `transmission_opaque` was rerun in all four
modes; its complete, audited quarantined controls were rechecked against source,
executable and every required setting. `transmission_constant` used a wholly
fresh pool. All 14 originally remaining OptiX fixtures completed; there was no
reuse of the interrupted partial pool. Task 20 rendered 54 fixture targets
(including two expected overflow attempts), 288 fresh calibration controls and
four native-hit controls. There was no repeated system/GPU crash or damaged EXR.

Every positive fixture/backend uses 20 compatible ordinary controls and the four
fixed seed controls 101/202/303/404. No bounds, floors, count rules, references,
fixture script or renderer source were changed during recovery. Per-control
settings and hashes remain in each `render.json`. All channel ratios, absolute
limits, ULP floors, fallback pixels, bias/count checks and denoised explanations
are retained in `task18/policy/{backend}/{fixture}/`. The table below gives the
ordinary leave-one-out fallback min/median/max and deep fallback counts in mode
order on/repeat/strict/strict-repeat. Each row has 20+4 controls and all four
modes passed. Machine-readable rows are in `task20/calibration-results.{json,csv}`.

| Backend | Fixture | Ordinary min/median/max | Deep counts |
| --- | --- | --- | --- |
| CUDA | single_1_plane | 0/0/0 | 0/1/0/1 |
| CUDA | single_1_world | 0/0/0 | 0/0/0/0 |
| CUDA | single_4_world | 0/0/0 | 0/0/0/0 |
| CUDA | single_4_plane | 0/0/0 | 0/0/0/0 |
| CUDA | single_1_constant_world | 0/0/1 | 0/0/0/0 |
| CUDA | single_4_constant_world | 0/0/0 | 0/0/0/0 |
| CUDA | dense | 0/1/3 | 2/2/1/2 |
| CUDA | fold | 0/0/0 | 1/0/0/0 |
| CUDA | adjacent | 0/0/0 | 0/0/0/0 |
| CUDA | interleaved | 0/0/1 | 0/0/0/0 |
| CUDA | two_objects | 0/0/0 | 0/0/0/0 |
| CUDA | instance | 0/0/0 | 0/0/0/0 |
| CUDA | junkshop | 0/0/0 | 0/0/0/0 |
| CUDA | indirect | 0/0/0 | 0/0/0/0 |
| CUDA | excluded | 0/0/0 | 0/0/0/0 |
| CUDA | camera_off | 0/0/0 | 0/0/0/0 |
| CUDA | shared | 0/0/0 | 0/0/0/0 |
| CUDA | holdout | 0/0/0 | 0/0/0/0 |
| CUDA | occluded | 0/0/0 | 0/0/0/0 |
| CUDA | occluding | 0/0/0 | 0/0/0/0 |
| CUDA | frame1 | 0/0/0 | 0/0/0/0 |
| CUDA | frame2 | 0/0/0 | 0/0/1/0 |
| CUDA | grazing | 0/0/0 | 0/0/0/0 |
| CUDA | tip | 0/0/0 | 0/0/0/0 |
| CUDA | inside | 0/0/0 | 0/0/0/0 |
| CUDA | legacy_opaque | 0/0/0 | 0/0/0/0 |
| CUDA | legacy_constant | 0/0/0 | 0/0/0/0 |
| CUDA | legacy_varying | 0/0/0 | 0/0/0/0 |
| CUDA | transmission_opaque | 0/0/0 | 0/0/0/0 |
| CUDA | transmission_constant | 0/0/0 | 0/0/0/0 |
| CUDA | transmission_varying | 0/0/0 | 0/0/0/0 |
| CUDA | chiang_opaque | 0/0/0 | 0/0/0/0 |
| CUDA | chiang_constant | 0/0/0 | 0/0/0/0 |
| CUDA | chiang_varying | 0/0/0 | 0/0/0/0 |
| CUDA | huang_opaque | 0/0/0 | 0/0/0/0 |
| CUDA | huang_constant | 0/0/0 | 0/0/0/0 |
| CUDA | huang_varying | 0/0/0 | 0/0/0/0 |
| CUDA | chain_8 | 0/0/0 | 0/0/0/0 |
| CUDA | chain_16 | 0/0/1 | 0/0/0/0 |
| CUDA | chain_32 | 0/0/0 | 0/0/0/0 |
| CUDA | chain_64 | 0/0/0 | 0/0/0/0 |
| OPTIX | single_1_world | 0/0/0 | 0/0/0/0 |
| OPTIX | single_1_plane | 0/0/0 | 0/0/0/0 |
| OPTIX | single_4_world | 0/0/0 | 0/0/0/0 |
| OPTIX | single_4_plane | 0/0/0 | 0/0/0/0 |
| OPTIX | single_1_constant_world | 0/0/0 | 0/0/0/0 |
| OPTIX | single_4_constant_world | 0/0/0 | 0/0/0/0 |
| OPTIX | dense | 0/1/5 | 3/2/1/2 |
| OPTIX | fold | 0/0/0 | 0/0/0/0 |
| OPTIX | adjacent | 0/0/0 | 0/0/0/0 |
| OPTIX | interleaved | 0/0/0 | 0/0/0/0 |
| OPTIX | two_objects | 0/0/0 | 0/0/0/0 |
| OPTIX | instance | 0/0/0 | 0/0/0/0 |
| OPTIX | junkshop | 0/0/0 | 0/0/0/0 |
| OPTIX | indirect | 0/0/0 | 0/0/0/0 |
| OPTIX | excluded | 0/0/0 | 0/0/0/0 |
| OPTIX | camera_off | 0/0/0 | 0/0/0/0 |
| OPTIX | shared | 0/0/0 | 0/0/0/0 |
| OPTIX | holdout | 0/0/0 | 0/0/0/0 |
| OPTIX | occluded | 0/0/0 | 0/0/0/0 |
| OPTIX | occluding | 0/0/0 | 0/0/0/0 |
| OPTIX | frame1 | 0/0/0 | 0/0/0/0 |
| OPTIX | frame2 | 0/0/1 | 0/0/0/0 |
| OPTIX | grazing | 0/0/0 | 0/0/0/0 |
| OPTIX | tip | 0/0/0 | 0/0/0/0 |
| OPTIX | inside | 0/0/0 | 0/0/0/0 |
| OPTIX | legacy_opaque | 0/0/0 | 0/0/0/0 |
| OPTIX | legacy_constant | 0/0/0 | 0/0/0/0 |
| OPTIX | legacy_varying | 0/0/0 | 0/0/0/0 |
| OPTIX | transmission_opaque | 0/0/0 | 0/0/0/0 |
| OPTIX | transmission_constant | 0/0/0 | 0/0/0/0 |
| OPTIX | transmission_varying | 0/0/0 | 0/0/0/0 |
| OPTIX | chiang_opaque | 0/0/0 | 0/0/0/0 |
| OPTIX | chiang_constant | 0/0/0 | 0/0/0/0 |
| OPTIX | chiang_varying | 0/0/0 | 0/0/0/0 |
| OPTIX | huang_opaque | 0/0/0 | 0/0/0/0 |
| OPTIX | huang_constant | 0/0/0 | 0/0/0/0 |
| OPTIX | huang_varying | 0/0/0 | 0/0/0/0 |
| OPTIX | chain_8 | 0/0/1 | 0/0/0/0 |
| OPTIX | chain_16 | 0/0/0 | 0/0/0/0 |
| OPTIX | chain_32 | 0/0/0 | 0/0/0/0 |
| OPTIX | chain_64 | 0/0/1 | 0/0/0/0 |

The four original CUDA flags remain resolved by the preserved Task 18 six-render
majorant-snapshot evidence: `single_1_plane/repeat` (17,9), bit-identical;
`single_1_plane/strict-repeat` (17,15), agreement within the unchanged four-ULP
rule, **not** bit-identical; `fold/on` (13,16) and `frame2/strict` (19,22), both
bit-identical. Each compares all 12 raw channels with equal counts against one
unchanged diagnostic deep-off state. The diagnostic executable remains
`45dcec6299e4e8982991355b2643be8231fa86a10d5188221e284ba5a7eac99b`;
the separate diagnostic patch remains out of this branch and the ordinary pool.
The original flagged channels/ratios/limits and target/diagnostic options remain
in their calibrated/resolved reports and the preceding Task 18 record. No new
Task 20 flag or diagnostic render was needed. Original flags are not erased.

Event histograms below are median/p95/max per camera sample, numeric `on` mode;
all positive cases also pass strict mode. Dense GPU maximum 16 versus CPU 19 is
informational. No cross-backend event-count or depth equality was imposed.

| Fixture | CUDA median/p95/max | OptiX median/p95/max |
| --- | --- | --- |
| single_1_plane | 1/1/1 | 1/1/1 |
| single_1_world | 0/1/1 | 0/1/1 |
| single_4_world | 0/1/1 | 0/1/1 |
| single_4_plane | 1/1/1 | 1/1/1 |
| single_1_constant_world | 0/1/1 | 0/1/1 |
| single_4_constant_world | 0/1/1 | 0/1/1 |
| dense | 9/15/16 | 9/14/16 |
| fold | 1/2/2 | 1/2/2 |
| adjacent | 1/1/2 | 1/1/2 |
| interleaved | 1/1/4 | 1/1/4 |
| two_objects | 1/1/1 | 1/1/1 |
| instance | 1/1/1 | 1/1/1 |
| junkshop | 1/1/1 | 1/1/1 |
| indirect | 1/1/1 | 1/1/1 |
| excluded | 1/1/1 | 1/1/1 |
| camera_off | 1/1/1 | 1/1/1 |
| shared | 1/1/1 | 1/1/1 |
| holdout | 1/1/1 | 1/1/1 |
| occluded | 1/1/1 | 1/1/1 |
| occluding | 1/1/1 | 1/1/1 |
| frame1 | 1/1/1 | 1/1/1 |
| frame2 | 1/1/1 | 1/1/1 |
| grazing | 1/1/1 | 1/1/1 |
| tip | 1/1/1 | 1/1/1 |
| inside | 0/0/0 | 0/0/0 |
| legacy_opaque | 1/1/1 | 1/1/1 |
| legacy_constant | 1/1/2 | 1/1/2 |
| legacy_varying | 1/1/2 | 1/1/2 |
| transmission_opaque | 1/1/1 | 1/1/1 |
| transmission_constant | 1/1/2 | 1/1/2 |
| transmission_varying | 1/1/2 | 1/1/2 |
| chiang_opaque | 1/1/1 | 1/1/1 |
| chiang_constant | 1/1/2 | 1/1/2 |
| chiang_varying | 1/1/2 | 1/1/2 |
| huang_opaque | 1/1/1 | 1/1/1 |
| huang_constant | 1/1/2 | 1/1/2 |
| huang_varying | 1/1/2 | 1/1/2 |
| chain_8 | 0/0/8 | 0/0/8 |
| chain_16 | 0/0/16 | 0/0/16 |
| chain_17 | Explicit overflow; no final EXR | Explicit overflow; no final EXR |
| chain_32 | 0/0/32 | 0/0/32 |
| chain_64 | 0/0/64 | 0/0/64 |
| chain_65 | Explicit overflow; no final EXR | Explicit overflow; no final EXR |

Maximum all-depth oracle error across all GPU modes: 2.98023223877e-08.

The OptiX audit passes all 41 positive fixtures: 36 contain raw Ribbon hits.
Both compiled ordinary/deep PTX modules include `__intersection__curve_ribbon`.
Existing closest/shadow/volume/local Ribbon SBT offsets are 16/17/18/19 of 24 hit
groups, with CUSTOM primitive flags. No program was added. Runtime records are
raygen stack 6464, any-hit stack 0, continuation stack 6464, callable stack 0 and
queue launch bytes 5296. The source stack formula includes linked closest-hit,
intersection/any-hit and deep any-hit requirements. Holdout/ID manifests passed
for both GPUs. The preceding Task 17 audit's 75 common beauty kernel resource
records remain unchanged; Task 20 made no compiled-source edit. Source identity
was additionally rechecked against all 376 modified/untracked Blender source
files from Task 19 provenance. The original overlay manifest was written at
06:46:50 UTC, before the ordinary build's 06:46:59 build-info timestamp.

Native grazing/tip controls pass on both GPUs, checking all 1,024 pixels and
4,096 camera samples per fixture. Grazing: 122 Ribbon hits, 3,937 plane hits and
37 misses. Tip: 119 Ribbon hits, 3,940 plane hits and 37 misses. Maximum depth
difference is 3.5762786865234375e-7 on each backend/fixture, within the existing
four-FLOAT-ULP comparison. These deep-off controls disable denoising-follow-
reflections so the native pass records the first camera-surface depth; they are
excluded from every beauty pool.

A new-validator bug initially caused IndexError by indexing a nonexistent hit
on legitimate finite-plane misses. Under Task 12, one validation-only repair
and retry retained **all** samples: hits require one event and first native t;
misses require no event and the native background FLT_MAX overwrite, normalized
by four. `src/kernel/film/denoising_passes.h:291` specifies that sentinel. The
four-ULP bound is unchanged. Known-hit/miss cases pass; a missing raw hit for a
published event and a wrong finite miss depth reject. Self-check and original
failure are preserved in `task20/native-validator-repair-selfcheck.json` and
`task20/rest-results-before-native-repair.json`. No control was rerendered.

GPU volume-mode hair rejection passes on CUDA and OptiX, with scanner isolation,
clear Deep Volume off errors and no final Deep EXR. Gaffer DeepToFlat, DeepSlice
and DeepMerge pass all 164 positive mode files per GPU, including saved review
scenes under `task18/gaffer/{CUDA,OPTIX}/`. Cross-backend flattened-alpha passes
all 246 comparisons (41 cases x two modes x CPU/CUDA/OptiX pairs), maximum
1.49011611938e-08, below 1e-4.
No native-backend waiver was needed. Full results: `task18/optix-audit.json`,
`holdout-manifests.json`, `native-hit/`, `rejections/`, `gaffer/` and
`cross-backend.json`; commands, times and the repaired validation retry are
recorded in `task20/rest-results.json` and per-stage logs.

Task 20 calibration render invocation time is 397.411425 seconds
for 288 new controls. Including Task 18's recorded lower bound, total calibration
time is at least 2505.763651 seconds; the interrupted
Task 18 invocation's unrecorded duration remains unknown. No time is invented
for it. Fixture/review/verification wall time is separate. Timings and counts
are in `task20/render-time.json`; the OptiX recovery driver took 1144.907683
seconds including validation and integrity reads.

Qualified scope is promoted to CPU/CUDA/OptiX static SVM Ribbon, surface mode,
tolerance zero; hair motion, other shapes, OSL/Hair Info, point clouds and hair
in volume mode stay rejected. Part C's CPU regression proof and earlier
qualified diagnostics remain retained. No Nuke claim. Part E follows the GPU
hair commit; its result will be recorded separately.

##### Task 20: Part E junkshop5 completed

GPU hair was committed after every Part D gate passed as
`4f4dffd63173ff8169097248ab108ef0495abe83` (`feat(deep): qualify GPU Ribbon hair`).
Its message records qualification executable SHA-256
`a1da1a20e605bfae5bc4aa886e46b4666ab522280b2783f5aedb4a9d158711d9`.
The compiled source blobs remain exactly those proved before qualification;
Task 20's repairs affect only external runners/validators. No push or PR.

Junkshop5 completed at 10%, four samples, frames 1-2, OptiX, resolution 200x100.
All three layers have Deep Alpha on. Girl is surface-only with Volume OFF,
tolerance CUSTOM 0 and events CUSTOM 64. BG/Robots retain SCENE volume mode,
scene volume on, shader evaluation on, step .01, 4096 events and 2048 MiB.
Both effective scanner runs report zero BLOCK findings. All outputs were
redirected under `D:/temp/cycles_deep_multilayer/junkshop5/` with the
`{view_layer}` deep template and fixed File Output template.

Source file `C:/Users/jun/Desktop/cycles_deep/thejunkshopsplashscreen_deep_v001.blend`
SHA-256 before and after:
`b83b37c58b2e39c0d64a8d906daf7f72d8c9142a7cf53e912ab582f85e5c22aa`.
The saved File Output nodes are **bg, girl, robots**, each already using
`{blend_name}_{node_name}_####`; the authorized name correction to **BG, Girl,
Robots** was therefore applied in memory. The file was never saved.
Production process wall time was 66.740344 seconds, separate from calibration.

There are exactly six Deep EXRs and six flat EXRs, all channels readable
immediately after rendering. File list, relative to
`D:/temp/cycles_deep_multilayer/junkshop5/render/v001/`:

| File | Bytes |
| --- | --- |
| thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr | 433119 |
| thejunkshopsplashscreen_deep_v001_BG_0001.exr | 240687 |
| thejunkshopsplashscreen_deep_v001_BG_0002.deep.exr | 433119 |
| thejunkshopsplashscreen_deep_v001_BG_0002.exr | 240698 |
| thejunkshopsplashscreen_deep_v001_Girl_0001.deep.exr | 27283 |
| thejunkshopsplashscreen_deep_v001_Girl_0001.exr | 17056 |
| thejunkshopsplashscreen_deep_v001_Girl_0002.deep.exr | 27283 |
| thejunkshopsplashscreen_deep_v001_Girl_0002.exr | 17057 |
| thejunkshopsplashscreen_deep_v001_Robots_0001.deep.exr | 116796 |
| thejunkshopsplashscreen_deep_v001_Robots_0001.exr | 46244 |
| thejunkshopsplashscreen_deep_v001_Robots_0002.deep.exr | 116796 |
| thejunkshopsplashscreen_deep_v001_Robots_0002.exr | 46246 |

All six pass OpenEXR structural/finite/sorted checks and resolution agreement.
Gaffer reader, DeepToFlat, DeepSlice, point-cloud and BG+Girl+Robots DeepMerge
checks pass on both frames; each file uses 72 grid pixels and five depth cuts.
The saved graph was reloaded and its three merge inputs checked explicitly.
The maximum reader flattening error is 1.19803684973e-07
and maximum slice error is 7.45058059692e-09, within the published bounds.
The review is `junkshop5/blender_deep_multilayer_review.gfr`.

The initial Task 19 `runpy` wrapper omitted the repository tools directory,
so importing `compare_deep_ids` failed before reading output. Under Task 12,
adding the same tools path that direct script invocation supplies repaired this
new runner-path bug. The known-good module import succeeds; a known-bad module
and the incorrect runner-directory module path reject. Gaffer passed on its one
validation-only retry. No image/reference/threshold change or rerender occurred.
The failed launch, retry and self-check remain in
`task20/junkshop5-gaffer.log`, `junkshop5-gaffer-retry.log` and
`review-wrapper-repair-selfcheck.json`.

BG and Robots, each frame, also pass the encoded Deep EXR identity comparator
against junkshop4 and have **zero** maximum flattened-alpha difference. They
contain respectively 77,434 and 15,492 deep samples per frame, unchanged from
junkshop4. These are reported cross-build/source-file comparisons; same-build
repeat identity is not newly asserted across builds. Full deterministic/run
attribute details are in `junkshop5/junkshop4-comparison.json`. No Part E GPU
beauty pixel comparison was requested or performed, so no smoke beauty-policy
qualification is claimed; Part D already qualified both GPU backends.

Girl's independent all-pixel/all-depth raw oracle, ID ownership and terminal
checks pass on both frames: 20,000 pixels / 80,000 camera samples per frame,
median/p95/max accepted events **0/0/1**, raw maximum 1, **zero** overflow pixels,
477 Ribbon events and maximum all-depth error **7.450580596923828e-9**. The EXR
contains 3,340 deep samples per frame. The histogram includes misses.

A retained Ribbon event is demonstrated in the reloaded Gaffer merge on both
frames: file pixel (118,45), Gaffer pixel (118,54), Z=ZBack=9.855746269226074,
encoded alpha .25, ahead of nearest BG depth 12.629141807556152. It remains at
merged sample index 4 of 6. The raw Ribbon event's object manifest name is
`Face` (object ID 2899238735); this is object ownership, **not** a strand ID.
The graph inputs are exactly BG/Girl/Robots for each frame.
`girl-oracle-histogram.json`, `hair-front-proof.json` and `gaffer-hair-front.json`
record the event population, raw provenance and actual merged sample.

Part E is complete. M0-M2 static SVM Ribbon scope is qualified on CPU/CUDA/OptiX;
M3 mixed hair/volume, other shapes, Hair Info, OSL on hair, hair motion and point
clouds remain outside scope. No full-resolution render or Nuke test was run.
All untracked source assets remain preserved. The installed qualification
executable is unchanged. The clean-build directory remains unused because its
removal was blocked by automatic approval review; that cleanup limitation is
recorded above. Stop here after Part E, with no further gate or production run.

Final report checks: Markdown checker passed (35 documents, 200 local links,
zero broken; 41 external links not fetched) and `git diff --check` passed.
The Part E report is committed separately after the GPU-hair qualification commit.

## Cancellation (2026-11-22)

The deep-enabled install (`D:\Projects\cycles_deep-data\blender-deep`, copy of
the qualified executable) opened the unchanged junkshop scene in the GUI. A
setup script applied the full-render per-layer settings at 25% (500x250), with
output under `D:\temp\cycles_deep_phase2\cancel\run\render`; the scene was
not saved. The user started F12 and pressed Esc once rendering moved on to Girl.
A folder listing taken every second recorded the result:

- BG's file appeared first as `...BG_0001.deep.exr.partial-*` and became
  `thejunkshopsplashscreen_deep_v001_BG_0001.deep.exr` (23,961,542 bytes) within
  one second.
- After cancellation and exit, that was the only file. No Girl or Robots deep
  file and no `.partial` file remained.
- Reading it back in full with OpenImageIO gave a 500x250 deep image with channels
  A, Z, ZBack and id, samples in all 125,000 pixels, and 3,385,766 samples in total.

The cancelled layer is from the user's report; the Blender log does not name the
active layer. This closes acceptance check 8.
