# Cycles deep output: current state

[Optimization plan](DEEP_OPTIMIZATION_PLAN.md) is the plan of record;
[peer requirements](PEER_DEEP_OUTPUT_REQUIREMENTS.md) remain mandatory.
Branch: `codex/landscape-cloud-compatibility`. Phases 0-7 accepted.

**Phase 8a stopped at the one-time toolchain rounding audit (2/111 cases).**
Pairing: clang-cl 20.1.8 / NVCC 12.8.61 (CUDA 12.8.0), OptiX 8.0.0,
native precompiled modules; [toolchain](BUILDING.md).
CPU adaptive_volume strict passes: max old/new transmittance difference
5.10897e-7. Its 1e-4 case fails the required 1e-6 compiler ceiling:
1.96236e-5 at (13,22). Both pass independent oracle/depth cuts within their
headers and exact same-build CPU beauty/raw. Nine CTests, unchanged beauty
sources and all 75 accepted CUDA resource records pass.
[Audit evidence](builds/validation/landscape-cloud/optimization-phase8a/toolchain-audit.json).

Raw CUDA spill differs before fitting: 338/2,244 rays, same 70,201 events,
event FLOAT max 1 ULP, identical cubic coefficients. This rules out fitting
as the sole cause; NVCC versus host-generated GPU inputs remains unisolated.
[Raw capture evidence](builds/validation/landscape-cloud/optimization-phase8a/raw-capture-toolchain.json).
No baseline was promoted. Remaining audit/identity/regression, OptiX AO/Bevel,
GPU beauty and speed measurements are pending. No Phase 8b/8c or Phase 9 run.

The approved [M8 release record](src/deep/M8_RELEASE_VALIDATION.md) is retained;
[13 archived reports](src/deep/ARCHIVED_REPORTS.md) remain indexed.
[Core map and regression command](src/deep/README.md).
Latest qualified Gaffer review: `D:/CyclesDeepScratch/regression-tools/phase7_review.gfr`.
Large evidence/TEMP stay on D:; legacy references remain unchanged.
