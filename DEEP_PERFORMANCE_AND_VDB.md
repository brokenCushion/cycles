# Deep performance and native VDB continuation

Completed objective: all four user-approved steps, including live Gaffer review.

1. Optimize host capture storage/export within the memory budget.
2. Repeat the original 664x625, 128-sample scene; compare deep output and beauty
   with the validated reference, and record timing.
3. Present the faster result in Gaffer and commit the integration checkpoint.
4. Implement/qualify native heterogeneous VDB deep capture using the supplied
   test-assets/vdb/firePlume_0000.vdb, and present actual output in Gaffer.

## Completion: live review verified, 2026-09-20

Both outputs were inspected in visible Gaffer windows. The native VDB review
displays the cyan plume through VDBDeepPoints after framing. The optimized
Blender review displays the cyan scene point cloud through SceneDeepPoints;
an additional readback confirms 1,000,000 points and finite scene bounds.
The newer backup of the optimized graph was opened, retaining the user's
Transform/PathFilter edits without saving over the original. A separate copy,
`scene-compact-deep/blender_deep_validated_review.gfr`, presents the validated
graph. VDB review: `native-vdb/final-cpu/native_vdb_review.gfr`.

The earlier window-access blocker is resolved. Launching the reviews on the
interactive desktop outside the restricted launch environment made them
available to the window-control tool. The following chronological notes retain
earlier failures and pending states; they are superseded by this completion.
Implementation checkpoints: `1ac6ce87c` and `8fb124b8d`. No push was performed.

## Current implementation

Capture spill now uses a compact fixed identity/completion index and a separate
append-only stream containing actual events. It no longer reserves max_events
payload space for every sample. Duplicate detection and explicit EMPTY markers
remain in the index. Finalization flushes both streams before publication.

The index uses sixteen 4 KiB LRU pages with dirty-range writes; event readback
uses sixteen 64 KiB pages. The 1,088 KiB cache allocation is included in preflight.
Both CPU and downloaded CUDA records share this host storage. No GPU-thread
allocation was added. Reads followed by resumed writes invalidate event pages,
including a previously cached partial final page.

## Verified evidence

- Standalone and custom Blender compact builds/install exited 0:
  cache-compact-build.log under builds/validation/blender-deep and builds/blender.
- All eight CTest groups passed in 11.18 seconds: cache-compact-ctest.log.
  The eviction test exceeds both caches, crosses event-page boundaries and
  compares every event with memory capture. Existing adaptive tests read data
  before resuming capture; lifecycle/duplicate/failure tests remain enabled.
- CPU/CUDA acceptance passed 23 fixtures and five rejection cases:
  cache-compact-cuda.log and cache-compact-cuda/report.json.
- Same Blender scene at 10% resolution and 128 samples:
  64 KiB fixed-payload cache 10.9582457 seconds (scene-lru-small);
  4 KiB fixed-payload cache 12.0920523 seconds (scene-small-page-small);
  compact payload storage 10.54324 seconds (scene-compact-small).
  These single small-scene timings do not prove a full-scene speedup.
- All three small deep files are byte-identical, SHA256:
  f7bdb6c4d2b8d76226178c4df6c431f5187ba79cc1ece74cd67d25ab81a79776.
  Every RGBA pixel of compact beauty matches the 64 KiB baseline exactly:
  scene-compact-small-beauty.json.

## Completed full-scene comparison

The compact full-scene benchmark completed with exit 0 in 247.6332591 seconds,
versus 1862.2927075 seconds for the validated reference: 7.5204x in this single-run
comparison. Output: scene-compact-deep; log: scene-compact-deep.log. It started
after all builds/tests/other render jobs finished. The entire deep EXR hash
matches the reference and every beauty RGBA pixel matches exactly.
Evidence: compact-performance-comparison.json and scene-compact-beauty-comparison.json.
Gaffer validation/review generation completed with exit 0: 81 diagnostic pixels,
21,076 boundary checks, maximum curve error 3.100437212522067e-9, zero slice error,
one million displayed points and successful graph reload. The optimized review
is open in a separate Gaffer window. Live viewport verification is pending:
capture was black, and fresh selection/activation returned GetCursorPos failed:
Access is denied (0x80070005). The user has been asked to restore desktop access.
Original unsaved Gaffer edits remain intact.

Earlier direct-mapped and 64 KiB LRU full-scene diagnostics were deliberately
stopped because of excessive I/O. They did not complete and are not performance
results. Session 22926 is terminal; dependent session 97390 correctly stopped
without executing checks because no completed render.json existed. Do not poll
or restart those old sessions.

## Native VDB preparation

The native scene helper tools/create_vdb_deep_scene.py ran successfully against
the actual supplied VDB. Output: builds/validation/native-vdb/scene.blend and
scene.json. It records the unchanged asset hash, both FLOAT grids, transforms,
world bounds, density scale and camera fixture settings. VDB SHA256:
2f8c00e3757b3618b9692b7f37ae411c097f0677cf6cc108a23e71e64b7139b9.

This scene is 256x256, one sample, linear density lookup and scalar absorption.
Its native beauty reference rendered successfully (0.6823 seconds):
`builds/validation/native-vdb/beauty-reference/`. The preview was inspected and
shows the VDB absorption against the white environment, noisy at one sample.
It is not a deep render yet. Native cell traversal/integration, bounded device
capture, actual asset validation and Gaffer review remain outstanding. See
src/deep/NATIVE_VDB_PLAN.md. The analytic linear-density reference is not native
VDB capture.

Native cell arithmetic is now implemented in kernel/deep/density.h and tested
on CPU plus actual CUDA hardware. It derives the full cubic from eight corners,
integrates/clips it and bounds constant-extinction approximation error. Nine
CTest groups pass; CUDA checks 512 cells at 101 probes each. Maximum oracle
error is 1.06581e-14. This helper is not yet called by native grid traversal.

The allocation-free grid cursor is also implemented and passes CPU/CUDA checks
against independent integer-plane enumeration, including negative coordinates,
boundary ties, clipping, a thin voxel feature and explicit capacity failures.
It is now connected to a native NanoVDB corner reader, but not the renderer
capture path. Actual supplied-grid CPU and CUDA checks pass 128 index-space rays
through 89,102 cells (125 nonempty rays), maximum transmittance error
2.6756429294394479e-9 against an independent OpenVDB oracle. All nine CTest groups
pass in 10.92 seconds. Tests currently qualify full-precision FLOAT grids.

The updated native Blender fixture explicitly uses FULL precision and has a
successful beauty reference under `builds/validation/native-vdb/full-precision/`.
The original VDB is unchanged. Camera/object transforms, cubic capture/storage,
deep EXR publication and Gaffer depth-cut checks still need implementation and
qualification before step 4 is complete.

Steps 1-2 have evidence above. Local commit `1ac6ce87c` contains the native
host interface, homogeneous volume work, Blender integration and compact storage.
No push is authorized. Step 3 still needs live viewport verification; step 4
needs native VDB capture and validation. Preserve the user's unsaved Gaffer
Transform/PathFilter edits.

Host cubic-cell interval fitting is implemented in src/deep/volume.cpp. Independent polynomial tests cover interior variation, overlapping/adjacent cells, total extinction, empty/invalid cells and explicit capacity failure. All nine regression groups pass (12.30 seconds). Capture storage and renderer wiring remain incomplete; this does not produce a native VDB deep EXR yet.

Cubic records are now connected to host Capture memory/spill storage and interval reconstruction, with explicit grid mode, coefficient validation, up to 4096 raw events, and extra working-set accounting. Mixed/cubic cache and independent reconstruction tests pass. The previous full rebuild completed successfully. Renderer CPU/CUDA scratch, traversal/shader integration and actual asset EXR qualification remain outstanding. The current 2048 fitted-interval cap still requires actual-asset evaluation.

Shared native grid emission now writes bounded strided event/coefficient buffers and rejects overflow. Actual-grid CPU/CUDA checks pass 128 rays; max transmittance error remains 2.6756429294394479e-9. Measured maximum 807 occupied records / 11169 fitted intervals, so grid Capture now separately budgets a 16384 interval limit (other modes remain 2048). Capture/publication tests pass. This still uses synthetic affine depth; native camera transforms, shader metadata and renderer invocation are outstanding.

Native Volume preflight now validates density-Fac scalar absorption and populates shader metadata; CPU/CUDA buffer interfaces are connected. Default CPU stale-signature link failure was diagnosed with dumpbin and fixed by forcing recompilation. Standalone INSTALL completed with exit 0. Blender adapter/overlay and --deep-volume render helper are updated; Blender INSTALL is still running. Actual VDB render and depth/beauty/Gaffer checks remain outstanding.

Added direct reconstruction for one camera sample with ordered non-overlapping volume intervals, avoiding quadratic repeated curve scans. Added 12000-interval and dense-optical-depth checks; all nine rebuilt regression groups pass in 13.18 seconds. Updated volume.cpp and capture_test.cpp in the Blender overlay while its build continues; rerun INSTALL after that build completes to guarantee these latest files are linked. Gaffer optimized review window still exists, but current activation again returns GetCursorPos Access is denied (0x80070005); live viewport verification remains pending.

Native Blender probe now renders the supplied VDB to a real deep EXR: full-precision/deep-probe, 10x10, 1 sample, 0.7061196 seconds, 161125 bytes. This required accepting static camera motion slots/zero pinhole focus and enabling volume publication in the Blender adapter. The complete 256x256 deep-off beauty succeeded (beauty-current); deep-full failed with DEEP_ERROR_DEPTH before publication. Native grid capture currently stores FLOAT front/back too early, so thin occupied cells can collapse. Next fix: preserve higher-precision native cell depths through capture and bound final FLOAT conversion, rather than suppressing the failure. Last Blender build/install completed, no render/build jobs running. Full native VDB output and Gaffer validation remain incomplete.

The FLOAT curve error validator now uses ordered cursors instead of quadratic rescans; all nine tests pass in 1.04 seconds (volume test 0.33 seconds). This is in the installed Blender binary. The probe artifact exists but is not the full-frame acceptance result.

Thin native cell depth fix implemented: companion records now retain double front/back plus FLOAT controls (32 bytes), CUDA grid batches capped at 8192 slots, memory accounting follows new sizeof. FLOAT export permits collapsed steps only after complete-curve error validation; significant unresolved opacity still fails atomically. Exact memory/spill thin-depth tests and bounded/rejected export fixtures pass; 9 CTests pass in 1.00s and actual CUDA VDB capture retains 2.6756429294394479e-9 max endpoint error. Blender INSTALL rebuild for this fix is active; full 256x256 retry remains pending.

Full native VDB render SUCCESS: full-precision/deep-full, 256x256, 1 sample, 10.2145797 seconds, 112780431-byte scene.deep.exr, SHA256 69d2449d9ceb61fe8723ea7b0aad11b88d7b83c8a7b69d554c988cb591c4b431. The source VDB SHA256 is unchanged. Gaffer reader/beauty/slice validation passes: every beauty RGBA pixel equals deep-off, 81 diagnostic pixels, five cuts, maximum stored-curve slice error 2.461312005319627e-7; one million valid deep-derived points, saved/reloaded focus. Review: full-precision/deep-full/native_vdb_review.gfr. Beauty preview inspected; it shows the noisy absorption plume at one sample. Review launch requested; visible viewport not yet verified because desktop access was denied earlier. This reader check is not an independent camera/grid transform oracle; that qualification and CPU/CUDA renderer parity remain outstanding before broad native VDB claims.

Native transform qualification passed: scaled-exact/deep-full scales volume and camera translation by 2, keeps camera rotation bit-for-bit, halves density and doubles clip distances. Gaffer compared all 16308564 deep samples against the original: offsets and alpha identical; Z and ZBack exactly twice baseline. Evidence: scale-validation.json; tools/scale_vdb_deep_fixture.py and src/deep/validate_vdb_scale_gaffer.py reproduce it. An initial matrix-assignment fixture perturbed camera rotation during Blender decomposition, so scaled-exact uses component scaling to express exact physical equivalence. The user VDB hash remains unchanged.

CUDA Blender probe is running through the configured toolkit (session 70392); ptxas is live and accumulating CPU time. Do not restart it while compiling. Native Gaffer review process 54128 exists/responds but its window title is empty and it is not returned as a named review window; do not claim visible presentation complete. Existing original unsaved Gaffer graph remains untouched.

CUDA probe session 70392 failed terminally: ptxas could not open its output cache file under AppData outside writable roots. Retry session 31816 uses BLENDER_USER_RESOURCES pointing to builds/blender/user-resources; its cache/kernels directory exists and ptxas is live. Do not restart this active retry. No CUDA-render result yet.

Audit correction: src/deep/exr_writer.cpp now reserves 3.4e-7 of the 1e-6 volume curve budget (1e-7 cubic fitting + 2e-7 sample reconstruction + 4e-8 coefficient rounding) rather than only 2e-7. Updated volume/publication tests pass. This latest host writer change is NOT yet copied/built into Blender because the CUDA render is active. After that process ends, copy exr_writer.cpp, rebuild INSTALL, repeat CPU full render and check its deep hash against 69d2449d9ceb61fe8723ea7b0aad11b88d7b83c8a7b69d554c988cb591c4b431, then run CUDA full paired renders. Do not install over the running Blender.

## Final native VDB qualification (2026-09-20)

The full CPU and CUDA renderer paths now pass on the supplied VDB at 256x256,
one sample, scalar absorption. Final directories: `builds/validation/native-vdb/final-cpu`
and `final-cuda`, each containing the deep EXR and validated `native_vdb_review.gfr`.
CPU output is byte-identical to the earlier full-frame result (SHA256 69d2449d9ceb61fe8723ea7b0aad11b88d7b83c8a7b69d554c988cb591c4b431).
CUDA SHA256: 1ba2992de0c6668a4a4788269850ab7c8fea7b7c86d4a56e0e2d0dbbaddeacad.
The respective qualification run times are 10.0517 and 61.1818 seconds; these
are not a controlled CPU/GPU benchmark. CUDA's bounded capture batches are small.

Both device beauty images match their deep-disabled references in every RGBA
pixel. Gaffer depth-cut error is at most 2.4614e-7 CPU / 2.7566e-7 CUDA over 81
diagnostic pixels and five cuts. Each saved/reloaded review generates one million
valid deep-derived points. CPU/CUDA boundary/midpoint comparison checks 45,004
probes across 81 pixels: max transmittance difference 5.8071e-7, below 1e-6.
Evidence: final-{cpu,cuda}/gaffer_validation.json and backend-validation.json.
The earlier full scale-invariance and independent OpenVDB grid results still apply.

The first stricter publication retry correctly failed its curve budget and left
the existing file intact. Final shared error allocations are 1e-7 cubic fitting,
5e-8 reconstruction, 4e-8 coefficient rounding, leaving 8.1e-7 for FLOAT export
within the unchanged total 1e-6 bound. Tightening reconstruction required raising
its non-grid cap from 2048 to 4096 intervals; the same cap drives memory preflight.
All nine regression groups pass (1.08 seconds). Blender and standalone overlays
are synchronized. An attempted replacement of the old EXR was denied while it
was in use, so final results use fresh directories; original review remains intact.

Live Gaffer presentation is still incomplete: fresh selection/activation and
capture fail with `foreground window did not report a process id`. The original
unsaved graph remains untouched. No all-backend or emission/scattering deep-RGB
claim is made. Commit the native qualification checkpoint after final build checks;
keep the overall objective active until live presentation is verified.
