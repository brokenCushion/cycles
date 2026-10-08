# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a remains unqualified; pristine toolchain step 1 passed.** The pinned
revision renders the AO landscape on OptiX with clang-cl 20.1.8, CUDA 12.8.0
and OptiX 8.0.0 using native precompiled modules (235x100, four samples).
Earlier MSVC/runtime-PTX failures used CUDA 12.8.1. Compiler and module path
both changed; clang-cl alone is not isolated. Beauty sources/flags untouched.
Stopped before switching deep builds or resuming qualification. The AO/Bevel
restriction and Phase 9 CUDA fallback are conditional on that qualification.

The raw-pass harness is fixed: all required count/denoiser channels are visible;
raw data and deep payload/deterministic headers are unchanged. Deep build
toolchain switch, full matrices/boundaries, fresh 81/81 + 30/30 identity, GPU beauty and
after measurements remain pending. Nine CTests and 75 resource records passed
earlier; the OptiX volume probe matches CUDA and passes its alpha oracle.
[Evidence and pending checks](builds/validation/landscape-cloud/optimization-phase8a/phase-results.json).
No Phase 8b/8c or full-resolution Phase 9 run started.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is restored
verbatim; [13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:.
