# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a is stopped, unqualified.** Pristine Blender at the pinned revision
fails native shader-raytrace linking with both OptiX 9.0 and the official
buildbot's 8.0.0 SDK headers, on CUDA 12.8. Beauty sources/flags were not changed.
OptiX fallback scope: scenes without shader-raytrace (AO/Bevel); Phase 9 uses CUDA.

The raw-pass harness is fixed: all required count/denoiser channels are visible;
raw data and deep payload/deterministic headers are unchanged. Restricted-scene
preflight, full matrices/boundaries, fresh 81/81 + 30/30 identity, GPU beauty and
after measurements remain pending. Nine CTests and 75 resource records passed
earlier; the OptiX volume probe matches CUDA and passes its alpha oracle.
[Evidence and pending checks](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).
No Phase 8b/8c or full-resolution Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is restored
verbatim; [13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:.
