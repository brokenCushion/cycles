# Deep EXR investigation and implementation status

Generated build and review artifacts live under `builds/`. Commands below use
that layout; older milestone notes may retain their original artifact names.

Completion update (2026-09-20): both the optimized full-scene and native VDB
point clouds have now been inspected in live Gaffer viewports. The performance,
full-scene comparison, checkpoint and native VDB qualification/presentation
objective is complete. Earlier pending desktop-access notes are superseded.

Latest checkpoint: **Native Blender full-scene deep render** completed at
664x625, 128 samples. Original evaluated geometry/materials, orthographic camera,
DOF and denoising are retained. Full beauty is identical with deep on/off; Gaffer
validates the deep output and a live million-point review. The full review is
open and its point-cloud viewport has been visually verified after desktop
unlock. The Blender scene render-and-presentation objective is complete. See [Blender integration evidence](BLENDER_DEEP_INTEGRATION.md).

Volume checkpoint: **Native FLOAT VDB scalar absorption** now renders the supplied
grid through CPU and CUDA at 256x256, one sample. Both preserve every beauty RGBA
pixel and pass Gaffer depth cuts and million-point graph reload. Independent
OpenVDB grid checks and a full-render scale-invariance check pass. General volume
shaders, emission/scattering deep RGB and other GPU backends remain unqualified.
See [native VDB evidence](src/deep/NATIVE_VDB_PLAN.md).

Performance continuation: compact indexed/append-only spill storage repeats the
full scene in 247.633 seconds (reference 1862.293 seconds), with a byte-identical
deep EXR and exact beauty RGBA equality. All eight CTest groups and CPU/CUDA
acceptance pass. The optimized Gaffer review is open and validated headlessly;
live viewport verification awaits restored desktop access.
Native VDB render validation passes; live presentation remains pending window
access. See [performance and VDB progress](DEEP_PERFORMANCE_AND_VDB.md).

## Baseline and scope

- Source baseline: `a456b761034dda42c32eef9f4aae0fa5a5c9f604`.
- Branch: `codex/deep-exr`; M0–M3 committed as `703331004`; M4/M5 and Gaffer review tools committed as `a508f1758`; CUDA, adaptive sampling and DOF committed as `2786fbe22`; M7c committed as `659e8dda9`.
- Platform/build evidence: [BASELINE_BUILD.md](BASELINE_BUILD.md).
- Target agreed for this workspace: the official standalone Cycles mirror.
  The user subsequently requested native Blender scene testing; a pinned custom
  Blender build and experimental session/output adapter now render deep previews.
- M0 standalone source/build investigation: complete for planning the reference.
- M1 reconstruction: implemented; standalone and optional parent-build tests pass.
- M2 writer: implemented; OpenEXR round trips, failure tests, OIIO inspection and
  Gaffer 1.7.2.0 deep-image validation pass. Nuke is not available to the user.
- M3 renderer capture: optional CPU opaque capture with a pre-render scene
  allowlist, raw storage budget, complete sample accounting, axial depth and
  reference reconstruction. See [M3 validation](src/deep/CAPTURE_VALIDATION.md).
- M4 scalar transparency: independent CPU visibility traversal with shared
  native/OSL evaluation, complete event chains and bounded experimental storage.
  See [M4 validation](src/deep/TRANSPARENCY_VALIDATION.md).
- M5 CPU storage/reduction/publication: implemented and validated with disk spill,
  bounded scanline export, 0.001 reduction and atomic EXR publication.
  See [M5 validation and limits](src/deep/PRODUCTION_VALIDATION.md).
- M6 CUDA backend: rebuilt and validated on RTX 3080. Eleven CPU/CUDA fixtures,
  Gaffer raw-ledger curve checks and five CTest groups pass. A 640x480 three-mesh
  review has matching depth cuts and live DeepToPointCloud nodes. Performance
  qualification remains in progress. See [CUDA validation](src/deep/CUDA_VALIDATION.md).
- M7a CPU/CUDA adaptive sampling: implemented with independent film population
  accounting, accepted-miss normalization, explicit GPU skipped lanes, CPU
  native/OSL and CUDA native validation. Beauty remains unchanged in the tested
  fixtures. See [adaptive validation](src/deep/ADAPTIVE_VALIDATION.md).
- M7b static perspective depth of field: CPU/CUDA native and CPU OSL checks
  pass, including adaptive DOF, focus invariance, defocus, and polygonal/anamorphic
  apertures. The 640x480 primitives review passes depth cuts on both backends.
  See [DOF validation](src/deep/DOF_VALIDATION.md).
- M7c rigid object/camera motion: CPU native/OSL and CUDA native capture tests pass,
  including opaque capture, adaptive sampling and DOF. Depth uses the camera at
  each accepted ray's shutter time. See [motion validation](src/deep/MOTION_VALIDATION.md).
  Shared-edge duplicate crossings are corrected with topology and orientation
  checks. Both 640x480 outputs pass independent raw-ledger depth cuts below
  1e-6. One cube-edge CPU/CUDA difference also occurs with deep disabled and is
  documented as a native backend precision limitation; strict backend parity
  remains false. The limited rigid-motion capture/reconstruction scope is qualified.
  Deformation, motion scale, rolling shutter and animated FOV remain outside
  this qualified M7 scope.
  M6 isolated performance qualification remains open; volumes remain M8.
- M8a analytic volume reference: interval reconstruction and FLOAT EXR output
  implemented, with homogeneous/overlapping extinction, partial coverage,
  camera-inside and surface-crossing fixtures. All six CTest groups pass.
  Gaffer depth cuts pass below 1e-6; live front/back point-cloud review provided.
  See [volume validation](src/deep/VOLUME_VALIDATION.md).
- M8b CPU homogeneous absorption capture: implemented behind `--deep-volume`,
  with constant scalar absorption on closed convex mesh volumes, static pinhole
  cameras and fixed sampling. Nine actual-render cases pass raw-ledger Gaffer
  cuts below 1e-6 and deep-on/off beauty equality. Disk spill and atomic scanline
  export support intervals. At M8b, CUDA volumes, heterogeneous density/VDB and volume
  DOF/motion/adaptive sampling remain unsupported; M8 is still in progress.
- M8c CUDA homogeneous absorption capture: the CPU/CUDA paths now share interval
  traversal, with bounded preallocated GPU medium storage and explicit overflow.
  Eighteen scenes and thirteen rejection cases pass on each backend, including
  clipping, overlaps and surfaces inside fog. See
  [CUDA volume validation](src/deep/CUDA_VOLUME_VALIDATION.md) for current results,
  floating-point beauty limits and the linked CPU/CUDA Gaffer point-cloud review.
  Heterogeneous/VDB integration and volume camera/sampling extensions remain future
  work; M8 overall is not complete.

## Architecture follow-up before CUDA volumes

Peer review requirements were audited against the current implementation. See
[architecture review and patch order](src/deep/ARCHITECTURE_REVIEW.md).
The first patch now moves settings/preflight into Session, capture ownership into
PathTrace, and final deep delivery into OutputDriver. The standalone application
is a file-writing adapter; a non-file host test consumes reconstructed samples.
See [native output integration](src/deep/NATIVE_OUTPUT_VALIDATION.md) for the API,
lifecycle checks and validation limits. Shared typed records now connect CPU/CUDA
capture and host spill storage, with explicit surface/volume fields and
completion/error states. See [typed record validation](src/deep/TYPED_RECORD_VALIDATION.md).
CUDA staging now uses configured-capacity event planes and one explicit deep
wait per batch. Native lifecycle, surface, adaptive, DOF and motion regressions
pass. See [CUDA storage validation](src/deep/CUDA_STORAGE_VALIDATION.md) for
measurements and remaining performance limits. CUDA homogeneous volumes now use
the shared helper and bounded medium buffer described in the M8c validation.
Controlled heterogeneous integration is next; M8 remains in progress.

## Original inspected source map

The investigation below refers to the pinned baseline. M3 now modifies the
CPU dispatch, film and standalone application at the identified hooks.

| Concern | Source and symbols | Finding / integration constraint |
| --- | --- | --- |
| CPU sample dispatch | `src/integrator/path_trace_work_cpu.cpp`, `PathTraceWorkCPU::render_samples`, `render_samples_full_pipeline` | Builds a per-pixel work tile using full-image offsets, scheduled samples and sample offsets; initializes camera state then runs the megakernel. Cancellation can stop the loop. |
| Sample acceptance/identity | `src/kernel/integrator/init_from_camera.h`, `integrator_init_from_camera`; `src/kernel/film/light_passes.h`, `film_write_sample` | Adaptive rejection precedes sample counting. Effective sample identity may come from the atomic per-pixel sample count plus sample offset. Camera cache retries reuse state: do not count them twice. |
| Stored state | `src/kernel/integrator/state_template.h` | Has render pixel index, sample, RNG state, bounce counters and path flags. Buffer pixel index is not alone a globally stable image coordinate. |
| Camera sampling | `src/kernel/integrator/init_from_camera.h`, `integrate_camera_sample`; `src/kernel/camera/camera.h`, `camera_sample` | Filter sample 0 uses the center; other samples use PRNG_FILTER. Raster/time/lens values feed ray creation. Capture must observe these decisions without drawing extra beauty RNG values. |
| Filter estimator | `src/scene/film.cpp`, `filter_table`; `src/kernel/camera/camera.h`, `camera_sample_perspective` | Filter uses an inverse-CDF importance table. The supported static pinhole perspective path returns unit camera throughput. Proposed M3 box-filter ledger uses unit weights; applying the filter again would double-weight it. Other cameras/settings need separate validation. |
| Adaptive stopping | `src/kernel/film/adaptive_sampling.h`, `film_need_sample_pixel` | Checks the adaptive auxiliary buffer before creating a new sample. Rejected scheduling attempts are not misses. M3 will reject adaptive sampling. |
| Hits and misses | `src/kernel/integrator/intersect_closest.h`, `integrator_intersect_next_kernel_after_volume`; `src/kernel/integrator/shade_background.h` | Surface intersections schedule shading; misses schedule background or termination. A geometry miss must complete the ledger regardless of world radiance. Early termination is not automatically a verified miss. |
| Shared material evaluation | `src/kernel/integrator/surface_shader.h`, `surface_shader_eval` | Dispatches OSL or SVM into ShaderData, initializes transparent extinction, and supports texture/cache retries. Capture must avoid duplicating events on retries. |
| Transparent closure semantics | `src/kernel/closure/bsdf_transparent.h`, `bsdf_transparent_setup`; `src/kernel/osl/closures_setup.h`; `src/kernel/svm/closure.h` | OSL and SVM call the shared setup function. It accumulates spectral transparent extinction after a closure-weight cutoff. This is a candidate shared interface, not proof of scalar parity for arbitrary materials. |
| Candidate local opacity | `src/kernel/integrator/surface_shader.h`, `surface_shader_transparency`, `surface_shader_alpha` | Computes saturated spectral (1 - transparency). Volume-only and ray-portal cases have special treatment. Only verified achromatic supported materials can become scalar events; do not average arbitrary colored transmission. |
| Path selection and termination | `src/kernel/integrator/shade_surface.h`, `integrate_surface_bsdf_bssrdf_bounce`; `src/kernel/integrator/path_state.h`, `path_state_next`; `src/kernel/integrator/intersect_closest.h`, `integrator_intersect_terminate` | BSDF/BSSRDF selection consumes random values. Transparent events advance tmin, but other closures redirect paths; transparent limits and stochastic termination can end visibility observation. Naive recording of beauty's observed path does not supply the complete-chain contract. |
| Depth | `src/kernel/camera/camera.h`, `camera_z_depth`; `src/scene/camera.cpp`, `Camera::update` | Perspective/orthographic depth is transformed camera-space **positive Z**, not a negated Cycles Z or raw ray distance. Ray origin moves to the near clip plane. Panorama uses distance. Restrict M3 to static perspective, and verify an off-axis plane before locking export semantics. |
| Existing flat Z | `src/kernel/film/data_passes.h`, `film_write_data_passes` | Writes depth on sample zero with path/pass conditions. It cannot reconstruct all camera samples or transparent chains. |
| Host output | `src/integrator/path_trace.cpp`, `PathTrace::tile_buffer_write`; `src/session/session.cpp`, `Session::wait`; `src/app/cycles_standalone.cpp` | Output passes through OutputDriver; standalone main waits for the session. Later sidecar ownership must distinguish complete frames, tiles and cancellation. |
| Image orientation | `src/app/oiio_output_driver.cpp`, `OIIOOutputDriver::write_render_tile` | Full-buffer flat output uses negative row stride to convert bottom-up to top-down. Future deep output must reproduce image alignment and explicitly preserve windows. |

## Decisions and open risks

1. **Neutral module:** capture/reconstruction use standard C++17. Separate
   targets allow standalone tests; the writer links existing OpenEXR. The optional
   CPU renderer now links the capture target. No speculative adapter ABI.
2. **Depth convention:** retain positive axial depth in the model. The standalone
   source's +Z convention is established; Blender's coordinate conversion and
   target compositor interpretation still require their own evidence.
3. **Transparent capture:** M4 uses a separate visibility traversal with a private
   copy of the accepted camera state/RNG. The restricted ordinary node graphs use
   shared shader evaluation, including OSL. Cache misses and incomplete traversal
   fail export. See the M4 validation contract for evidence and exclusions.
4. **M3 opaque capture:** candidate hook is a validated primary geometry hit or
   miss, tied to an accepted camera sample and completed once. Only verified
   opaque polygon surfaces, static perspective pinhole, fixed samples and box
   filter. Reject volume/portal/refraction/holdout/shadow-catcher cases. No hooks
   were added during M1.
5. **Writer route:** direct existing OpenEXR is the verified M2 choice. Pinned
   headers expose `Imf::DeepScanLineOutputFile`, `DeepFrameBuffer`, and explicit
   sample-count slices. OIIO also exposes deep I/O, but the existing application
   driver is flat RGBA. Direct OpenEXR gives explicit types/counts/windows and
   avoids adapting the flat driver. M2 now links and round-trips NONE/ZIPS with
   OpenEXR 3.4.10 and verifies application-level behavior in Gaffer 1.7.2.0.
6. **Existing implementation search:** searches for DeepScanLine, DeepData and
   deep-image/output terms in this checkout's `src` found no existing writer.
   This does not make a claim about other branches or third-party code.
7. **Storage limits:** M5 spills raw capture to disk and reconstructs one pixel
   at a time into one scanline of output. Deep working buffers have a conservative
   preflight budget; this is not a total-process memory/RSS cap. Strict reduction
   is optional. Farm batch merging and network publication remain unqualified.

## M1 verification

Commands (repository root, Visual Studio 2022 and CMake from baseline):

```powershell
cmake -S src/deep -B builds/build-deep -G "Visual Studio 17 2022" -A x64
cmake --build builds/build-deep --config Release
ctest --test-dir builds/build-deep -C Release --output-on-failure
.\builds\build-deep\Release\cycles_deep_reference_test.exe
```

Configure/build/CTest/executable exits: 0. Six test groups pass, including all
required analytic fixtures, invalid/incomplete inputs, extreme weights,
2,000-event low-opacity stack, 250 deterministic randomized ledgers, storage
permutations, and the expected scalar-coverage limitation. Maximum checked
absolute error: `8.7596596642924851e-14`, below `1e-12`. Disjoint coverage emits
`(Z=2, A=0.5), (Z=8, A=1)` and flattens to 1.

The independent brute-force oracle evaluates both sides of each raw boundary.
Tests use explicit exceptions/checks, not Release-disabled assertions.
Local configure/build logs and CTest output live in ignored build directories.

## M2 verification and handoff

See [src/deep/EXR_VALIDATION.md](src/deep/EXR_VALIDATION.md) for commands,
file contract, fixture inventory, failure coverage, Gaffer reproduction and an
optional Nuke procedure. Commands and observed exits:

```powershell
cmake -S . -B build-baseline -DWITH_CYCLES_DEEP_TESTS=ON -DWITH_CYCLES_DEEP_EXR_TESTS=ON
cmake --build build-baseline --config Release --target cycles_deep_reference_test cycles_deep_exr_test
ctest --test-dir build-baseline -C Release --output-on-failure
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd python src/deep/validate_gaffer.py build-baseline/src/deep/fixtures
```

Configuration, builds and CTest exit 0; 3/3 tests passed. Final CTest wall time
12.87 seconds (startup/file-scanning effects included; not a renderer benchmark).
OpenEXR round-trip maximum transmittance error: `1.1920928910669204e-08`.
Gaffer exits 0 across eight fixtures; maximum flat alpha error
`3.35239146442845e-08`, partial-depth alpha error `1.1920928910669204e-08`.
Both pass the 1e-6 gate. OIIO reads the uncompressed surface fixture with the
expected 270 samples and zero NaN/Inf. Uncompressed surface files are 3,926 bytes;
ZIPS files are 1,390 bytes; all-empty files are 686 bytes with either codec.
These synthetic sizes are not production performance results.

Changes: optional CMake targets; neutral model/reference/tests; OpenEXR writer
and round-trip/failure tests; Gaffer validator; documentation. Renderer sources
were unchanged at M2 and are now modified by M3/M4. M0–M3 are committed locally as
`703331004`; M4/M5 and Gaffer tools form the next CPU checkpoint. Build outputs, fixtures, logs and portable Gaffer
are ignored by Git.

The first Gaffer run exposed Windows backslash substitution in filename plugs;
the validator now supplies forward-slash paths. The final run passed. The saved
`deep_validation.gfr` was loaded successfully in a separate headless check.
Runtime introspection confirms the official package includes Cycles 5.1.0.
Our fork is not installed into Gaffer, and no Gaffer Cycles rendering was tested.

Limitations: no total-process memory cap, GPU tests, volumes, deep RGB, or Nuke
validation. M5 adds a deep working-buffer bound and local atomic publication.
Full Blender baseline and
integration remain outside this standalone checkout. No performance qualification
or upstream acceptance is claimed.

## M3 work package and acceptance contract

Keep this as a small patch series with independent gates:

1. **Configuration and validation:** add one experimental standalone sidecar
   setting in `src/app/cycles_standalone.cpp`, owned by the CLI with a non-owning film pointer.
   Validate CPU, fixed samples, static perspective pinhole, box filter, opaque
   polygon geometry, and the excluded feature set before rendering. Establish
   an explicit material/scene allowlist; fail if opacity cannot be proven.
2. **Sample lifecycle and bounded experimental storage:** extend the CPU work
   owner around `PathTraceWorkCPU::render_samples_full_pipeline`. Assign identity
   from accepted effective camera samples and full-image coordinates, reserve a
   small-scene memory cap, and account for misses/cancellation/retries exactly
   once. Keep storage absent when deep is disabled. Do not use an unbounded
   whole-frame ledger as a production default.
3. **Primary hit/miss adapter:** read the first valid opaque camera hit and axial
   depth at inspected intersection/shading points; record miss completion before
   world shading. Emit neutral events without changing beauty RNG or traversal.
   Execute the scheduled primary intersection once before the megakernel and
   resume from its successor. Reject unexpected camera states; use PRIM_NONE
   for misses. Do not infer completeness from generic path end.
4. **Output and alignment:** hand complete CPU ledgers to reconstruction/writer
   after required rendering completes. Preserve orientation, crop and frame/view
   identity, and report export failure. Keep the output explicitly experimental;
   production atomic pairing stays a later gate.
5. **Acceptance scenes:** frontoparallel off-axis plane, diagonal edge, small
   geometry and near/far disjoint coverage. Compare captured-event oracle,
   expected depth/alpha and deep-on/off beauty under fixed seeds. Load real
   sidecars in the existing Gaffer validator workflow and test depth cuts.

Gaffer's bundled Cycles 5.1.0 is a useful later host, but substituting this fork
requires auditing Gaffer's Cycles revision/build/ABI and rebuilding its adapter.
Do not swap renderer DLLs or equate successful EXR ingestion with integration.
