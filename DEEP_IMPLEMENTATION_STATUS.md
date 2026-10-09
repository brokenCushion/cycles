# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) still apply.
Branch: `codex/landscape-cloud-compatibility`. Phases 0–8 accepted.

**Phase 9: module difference resolved; scheduler timing diagnostic resuming.**
Both production deep outputs pass the deep gates; the adaptive beauty count-rate
failure remains unresolved. The isolated snapshot experiment reproduces the same
8 count-mismatch pixels on OptiX. CUDA off/on has zero image-wide count mismatches.

The approved fixed 336/adaptive-OFF OptiX pair uses the existing snapshot executable.
Both renders finished. The parser repair and regression check passed; only analysis
of preserved outputs was rerun. All 587,500 pixels have 336 samples and noisy alpha
is exact, but image-wide raw differences exceed a few ULP: noisy R/G/B 118/349/118,
albedo R/G/B 35/58/65, depth 30. Normal near-zero cancellation inflates reference ULP;
unit-scale maxima are 4.5/15.25/15.5 ULP(1.0).

Batch sizes differ in fixed and adaptive pairs. Adaptive filter checkpoints match
(16,32,…1024, reconstructed from earlier INFO logs); fixed runs have no filters.
At (986,61) the adaptive count is 336 off versus 464 on, with threshold 0.15. Its
checkpoint combined/class-A accumulations and neighbour masks were not saved,
so the convergence metric and exact first divergence cannot be recovered.
The user accepted the differences as consistent with summation order and waived
checkpoint instrumentation. Fix `47a8dffb0` excludes GPU capture kernels/readback/
spill from scheduler timing, under deep guards. Native-source hash equality and
the batch-comparator self-check pass. The separate snapshot+fix build installed.
The OSL MNEE PTX difference is only a temporary register rename at three lines;
instructions and constants match. Two same-source module rebuilds are byte-identical
and match the old PTX. The user-approved module comparison now strips comments/debug
paths and renames virtual registers bijectively; code/data/order remain checked.
No renderer code or numerical gate changed. Source and qualified installs were
verified preserved. The queue resumes using the existing diagnostic executable:
small117x50 test, then full OptiX off/on/on and CUDA off/on. Stop after step 3.

[Module audit and two rebuilds](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-module-check.json).

[Build failure and preservation proof](builds/validation/landscape-cloud/optimization-phase9/scheduler-timing-fix-build-failure.json).

[Fixed comparison, every raw pass and schedules](builds/validation/landscape-cloud/optimization-phase9/optix-fixed-count-isolation.json).
[CUDA isolation](builds/validation/landscape-cloud/optimization-phase9/cuda-count-isolation.json).
[Snapshot count experiment](builds/validation/landscape-cloud/optimization-phase9/majorant-count-diagnostic.json).
[Production results](builds/validation/landscape-cloud/optimization-phase9/production-results.json).
[Previous parser failure](builds/validation/landscape-cloud/optimization-phase9/optix-fixed-count-harness-failure.json).
Large fixed-pair outputs: `D:/CyclesDeepScratch/optimization-phase9/optix-fixed-count-diagnostic-20261010/`.
Current diagnostic: `D:/CyclesDeepScratch/optimization-phase9/scheduler-timing-fix-20261010/`.
Qualification and connected Gaffer review remain pending.

[Toolchain](BUILDING.md): clang-cl 20.1.8 / NVCC 12.8.61, CUDA 12.8.0,
OptiX 8.0.0, OSL 1.15.3.0. Snapshot patch is diagnostic only; production unchanged.
[Support matrix](src/deep/RELEASE_MATRIX.md),
[M8 release](src/deep/M8_RELEASE_VALIDATION.md),
[archived reports](src/deep/ARCHIVED_REPORTS.md), [regression](src/deep/README.md).
