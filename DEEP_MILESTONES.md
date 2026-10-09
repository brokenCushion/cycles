# Deep output milestones

## Agreed scope

M0-M9 are ten numbered milestones. M0-M7 describe the foundations already
developed. **M8 is the release gate for production-ready deep alpha.** M9 holds
future development outside that release scope. The historical M8 matrix passed
and the user approved its Gaffer scene on 2026-09-29. **The expanded M8 landscape
production qualification remains open**, as recorded in
[implementation status](DEEP_IMPLEMENTATION_STATUS.md).

Deep alpha means camera visibility represented by **Z, ZBack and A**. Beauty
remains a separate native render. Deep RGB is not required to complete M8.

| Milestone | Purpose |
| --- | --- |
| M0 | Repository, baseline and build setup |
| M1 | Deep-sample reconstruction |
| M2 | Deep EXR writing |
| M3 | Renderer capture |
| M4 | Scalar surface transparency |
| M5 | Bounded storage, reduction and atomic publication |
| M6 | CUDA capture and device integration |
| M7 | Adaptive sampling, depth of field and motion |
| M8 | Production-ready deep alpha across supported surfaces and volumes |
| M9 | Future extensions beyond the M8 deep-alpha release |

## M8: production-ready deep alpha

M8 owns remaining production blockers from M5-M7 as well as volume work.
Existing M8a-M8d volume checkpoints and their validation reports remain useful
evidence; they do not replace the following release gates.

### Remaining work and completion criteria

1. **Define and qualify the supported scene matrix.** Cover opaque and scalar
   transparent surfaces, homogeneous volumes, heterogeneous VDB density, mixed
   surface/volume scenes, overlapping media, clipping and camera-inside cases.
   Specify supported material graphs, grid precision/interpolation, transforms,
   camera modes and sampling combinations. Qualify multiple camera samples and
   the existing adaptive/DOF/rigid-motion features wherever advertised, including
   volume combinations before claiming support. Unsupported combinations must
   fail explicitly before publishing output.
2. **Prove alpha correctness.** Use independent extinction/transmittance oracles,
   boundary and interior depth cuts, thin features, occlusion, partial coverage
   and CPU/CUDA comparisons. Validate total scalar extinction for supported
   materials, including any scattering contribution to opacity; deferring deep
   scattering colour is not permission to ignore extinction. Record numerical
   tolerances and native backend differences. Preserve beauty with deep enabled.
3. **Qualify production scale and performance.** Start with the supplied VDB at
   664x625 and multiple samples, then test larger representative workloads.
   Measure render/capture/export time, host/device peak memory, spill I/O, sample
   counts and EXR size. Improve CUDA batching and other measured bottlenecks.
   Establish explicit acceptable workload and resource targets from these
   measurements before sign-off; no target is considered met merely because a
   small render completes. Repeat timings for performance claims.
4. **Control storage and reduction.** Bound allocations and working sets on host
   and device, use host-preallocated GPU storage, and avoid allocation in GPU
   threads. Qualify volume as well as surface reduction against a documented
   whole-curve error budget. Measure size/accuracy tradeoffs and preserve useful
   depth cuts. Exhausted capacities must produce actionable errors, not dropped
   samples or silently degraded alpha.
5. **Harden lifecycle and publication.** Exercise cancellation, device errors,
   memory/capacity exhaustion, disk failures, output replacement, repeated renders
   and session reset. Verify cleanup and atomic publication: failed renders must
   not publish partial EXRs or destroy a previous completed output.
6. **Complete integration and review.** Keep the core host-independent, qualify
   standalone and custom Blender paths on CPU/CUDA, run applicable regressions,
   and document supported settings, limitations and reproducible commands. Show
   each checkpoint in Gaffer with depth slices and DeepToPointCloud. Commit the
   release evidence and obtain the user's review before declaring M8 complete.

Production readiness is claimed for the explicitly tested support matrix, not
every Cycles feature or backend. A failure of a promised M8 capability remains
an M8 blocker; it must not be moved to M9 merely to make M8 pass.

### Evidence already available

- Full Blender scene: 664x625, 128 samples, byte-identical optimized deep EXR,
  unchanged beauty and live Gaffer point-cloud review.
- Native VDB: CPU/CUDA 256x256, one-sample scalar-absorption renders; unchanged
  per-device beauty, independent grid checks, scale invariance and Gaffer cuts.
- Production-scale VDB: CPU/CUDA 1024x768, four samples; repeated runs pass
  accuracy, beauty, resource and output-size targets with byte-identical repeats
  within each device. This checkpoint completes the supplied-VDB scale benchmark;
  final scene-matrix coverage, full Blender asset regression and release audit
  also pass. The user approved the final Gaffer scene on 2026-09-29, completing M8.
- Bounded capture/spill, explicit failures and atomic publication tests.

See [implementation status](DEEP_IMPLEMENTATION_STATUS.md),
[performance and VDB evidence](DEEP_PERFORMANCE_AND_VDB.md), and
[native VDB qualification](src/deep/NATIVE_VDB_PLAN.md).

## M9: future development

M9 is a deferred backlog, not a prerequisite for the deep-alpha release. Items
will need separate designs and acceptance criteria when development is scheduled.

- Deep RGB, including emission and single/multiple-scattering colour
  reconstruction, associated AOVs and their storage/compute costs.
- Additional native device backends: OptiX, HIP, Metal and oneAPI qualification.
  M8 must still use shared Cycles device conventions so these remain feasible.
- Features outside the published M8 alpha support matrix: additional volume
  shader graphs/interpolation/precision modes, coloured extinction semantics,
  deforming or animated grids/geometry, motion scale, rolling shutter and animated
  camera intrinsics. Existing advertised alpha features remain M8 obligations.
- Additional host integrations and compositing applications beyond the current
  standalone/Blender rendering and Gaffer validation workflow.
- Further compression, acceleration and workflow enhancements beyond the
  performance, robustness and output-size targets accepted for M8.

No deep-RGB implementation work is scheduled while M8 deep-alpha production
blockers remain. Scope changes must be recorded here rather than silently
changing the definition of production-ready alpha.
