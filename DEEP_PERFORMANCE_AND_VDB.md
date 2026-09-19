# Deep performance and native VDB continuation

Active objective: achieve all four user-approved steps.

1. Optimize host capture storage/export within the memory budget.
2. Repeat the original 664x625, 128-sample scene; compare deep output and beauty
   with the validated reference, and record timing.
3. Present the faster result in Gaffer and commit the integration checkpoint.
4. Implement/qualify native heterogeneous VDB deep capture using the supplied
   test-assets/vdb/firePlume_0000.vdb, and present actual output in Gaffer.

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
It is not a deep render yet. Native cell traversal/integration, bounded device
capture, actual asset validation and Gaffer review remain outstanding. See
src/deep/NATIVE_VDB_PLAN.md. The analytic linear-density reference is not native
VDB capture.

Steps 1-2 have evidence above. This integration checkpoint contains the native
host interface, homogeneous volume work, Blender integration and compact storage.
No push is authorized. Step 3 still needs live viewport verification; step 4
needs native VDB capture and validation. Preserve the user's unsaved Gaffer
Transform/PathFilter edits.
