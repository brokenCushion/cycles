# Deep alpha: measured results

**M8 remains open.** Each result qualifies only its stated scope.
Paths below are relative to `builds/validation/`.

| Check | Result | Evidence |
| --- | --- | --- |
| Blender, 664x625, 128 spp | 247.633 s vs 1862.293 s; 7.52x single-run speedup. Entire deep EXR identical; beauty RGBA exact. | `blender-deep/compact-performance-comparison.json` |
| Optimized scene in Gaffer | 81 pixels, 21,076 boundaries; max curve error 3.1005e-9; slice error zero; 1M points. Live review passed. | `blender-deep/scene-compact-deep/gaffer_validation.json` |
| VDB CPU, 256x256, 1 spp | 10.0517 s, 112,780,431 bytes; deep-on/off beauty exact. | `native-vdb/final-cpu/render.json` |
| VDB CUDA, 256x256, 1 spp | 61.1818 s, 112,787,785 bytes; deep-on/off beauty exact. | `native-vdb/final-cuda/render.json` |
| VDB Gaffer cuts | 81 pixels, five cuts; errors CPU 2.4614e-7 / CUDA 2.7566e-7; 1M points each. CPU live review passed. | `native-vdb/final-{cpu,cuda}/gaffer_validation.json` |
| CPU/CUDA curves | 45,004 boundary/midpoint probes; max difference 5.8071e-7, limit 1e-6. | `native-vdb/backend-validation.json` |
| Scale invariance | 16,308,564 samples: identical counts/alpha, depths doubled for doubled world scale and halved extinction. | `native-vdb/scale-validation.json` |
| Independent OpenVDB oracle | CPU/CUDA: 128 rays, 89,102 cells; max transmittance error 2.6757e-9. | `native-vdb/grid-qualification/`, `native-vdb/native-capture-precision-cuda-test.log` |
| CPU checkpoint, 664x625, 4 spp | Sequential measurements: 471.531 / 515.390 s; byte-identical 1,028,105,267-byte EXRs. Observed peak working set 719,749,120 / 715,935,744 bytes. First run: beauty exact; 241,222 curve probes, max error 3.418e-7; Gaffer cuts 5.228e-7. | `m8-production/final-scale/cpu-repeat.json` |
| CUDA checkpoint, 664x625, 4 spp | Sequential measurements: 642.938 / 685.203 s; byte-identical 1,028,120,960-byte EXRs. Peak working set 892,624,896 / 885,448,704 bytes; device-wide memory 5523 / 5705 MiB. Both pass 241,222 curve probes (max 3.476e-7), Gaffer cuts (3.094e-7) and beauty repeat checks. | `m8-production/final-scale/cuda-repeat.json` |
| Qualified CPU, 1024x768, 4 spp | 1140.969 / 1144.062 s; byte-identical 2,448,866,824-byte EXRs; peak working set 817,561,600 / 822,566,912 bytes. Both pass 380,694 curve probes (max 2.629e-8), Gaffer cuts (3.144e-7), exact beauty and 1M-point previews. | `m8-production/final-scale/cpu-1024-projected-{1,2}/` |
| Qualified CUDA, 1024x768, 4 spp | 1466.219 / 1422.766 s; byte-identical 2,448,846,469-byte EXRs; peak working set 1,026,363,392 / 1,023,262,720 bytes; device-wide peaks 4038 / 4203 MiB. Both pass 380,698 curve probes (max 2.637e-8), Gaffer cuts (2.789e-7), beauty tolerance and 1M-point previews. | `m8-production/final-scale/cuda-1024-projected-{2,3}/` |

Timings are qualification runs, not statistical benchmarks. Gaffer validates
stored EXR curves; OpenVDB tests provide the independent grid reference.
Neither substitutes for full scene coverage.

## Production-scale qualification

Current export reconstructs one pixel at a time, stages one FLOAT scanline and
uses synchronous OpenEXR compression. Preflight reserves 80 bytes per row sample
plus reconstruction scratch; the supplied four-sample fixture accepts 1 GiB and
rejects 512 MiB. Streaming merges share the 5e-8 reconstruction allowance with
mixture fitting. Final-build measurements are under `m8-production/final-scale`.

Storage details: [production contract](src/deep/PRODUCTION_VALIDATION.md).

## Scene coverage (96x96, four samples)

CPU beauty-on/off pairs are exact. Gaffer compares
exported curves with accepted-camera diagnostics, not an independent grid oracle.

| Case | Depth probes | Maximum curve error | Result |
| --- | ---: | ---: | --- |
| Scalar transparent surface inside VDB | 236,012 | 3.125e-7 | Pass |
| Camera inside VDB | 923,090 | 2.524e-8 | Pass |
| Far clip inside VDB | 185,824 | 3.125e-7 | Pass |
| Two overlapping VDB objects, CPU | 786,100 | 4.732e-7 | EXR and Gaffer pass |
| Two overlapping VDB objects, CUDA | 786,108 | 4.757e-7 | EXR and Gaffer pass |

The first three cases pass depth slices and evaluate one million preview points.
Bounded exact merge checks reduce overlap output from 41.24 MB to 34.62 MB
(about 16%) without changing the error budget. CPU/CUDA Gaffer slice errors
are 5.523e-7 / 5.192e-7; both evaluate one million preview points. CUDA beauty
on/off and ordinary-repeat differences are both 5.961e-8.

Separate-object references pass on CPU/CUDA at 1.252e-7 / 1.255e-7 over
340,392 / 340,390 boundary/midpoint probes. This isolates combined capture but
still shares single-grid code; it does not replace the OpenVDB oracle.
Evidence: `m8-production/overlap-oracle-named` and each
`coverage/overlapping_grids/*-reduced/overlap_validation.json`.

Keep volume names stable between layers: native `Object::adjust_volume_tfm()`
adds a name-hashed offset up to 0.001 local units. Separate-layer oracles must
preserve those names. Saved Gaffer reviews are beside the EXRs.

## Release-scale targets

Final qualification targets for the supplied static VDB, four samples, on this
RTX 3080 / Ryzen 5900X workstation: 664x625 within 20 minutes and 2 GB per EXR;
1024x768 within 40 minutes and 4 GB. Both must retain the existing 1e-6 curve
gate, a 1 GiB configured deep budget, host peak working set below 4 GiB and
device-wide observed memory below 8 GiB. These are fixture release targets,
not guarantees for arbitrary assets. Run sequential warm-cache repeats and
report individual measurements before claiming performance qualification.
`tools/measure_deep_render.py` records process memory and I/O plus device-wide
GPU memory; process I/O is not an isolated spill counter. GPU compiler local
storage and renderer allocations still require the separate budget audit.
The current build reports logical spill bytes and export time with
`--log cycles --log-level info`. Nine CTests pass. Double-precision boundary
pairing passes two captured-ray regressions and the CPU/CUDA volume suites
(`m8-production/precise-boundary/`). **The supplied VDB production-scale
qualification now passes**, recorded in
`m8-production/final-scale/projected-1024-report.json`. The cross-device check
passes 101,222 boundary/midpoint comparisons at 81 pixels, maximum 4.6094e-7
against the unchanged 1e-6 gate. This is sampled cross-device coverage; the
writer also checks each pixel's whole serialization curve. Both devices use
the same scene and executable; repeats are byte-identical within each device.

CPU capture/render takes 20.918 / 22.415 s and export 1118.24 / 1119.82 s.
CUDA takes 354.665 / 347.381 s and export 1109.87 / 1073.81 s.
Logical spill reads are CPU 15.954 / 15.454 GB and CUDA 4.314 GB per run;
writes are 3.926 / 3.999 GB respectively, with 3.703 GB file extents.
Each CUDA run captures all 3,145,728 camera records with zero skips, 49,152
batches and 670.103 GB of readback. Transfer cost remains substantial even
though these fixture targets pass. CPU-run GPU peaks include other applications.
CUDA run 1 reached 9463 MiB with another Blender session open; it is retained
in the report and excluded from qualification. Runs 2 and 3 were measured after
the user closed that session; neither exceeds 8 GiB.

The earlier cross-device failure (1.2303e-6) remains in
`m8-production/final-scale/precise-1024-backend-detail.json`. Exact replay traced
it to opposite serialization drift, despite much closer raw device curves.
Default cumulative-depth projection fixes it without relaxing the allowance;
`exr_writer_test.cpp` guards the below-threshold drift case. Replay evidence is
under `m8-production/backend-diagnostic/`. The earlier unresolved-boundary run
under `m8-production/final-scale/cpu-1024-1/` published no deep EXR.

## Scattering

Scattering checkpoint: `m8-production/scattering/{CPU-input,CUDA-input}` passes
28 scenes and 19 rejection cases, including five new scalar-scattering cases
and XML NaN/Infinity rejection.
Their maximum analytic error is 1.135e-7; CPU beauty is exact and CUDA differences
remain within its existing repeat threshold. Nine current CTests also pass.

`m8-production/scattering-vdb/scattering` uses half-grey scattering at twice the
density of black absorption. CPU/CUDA deep files match their respective
absorption references byte-for-byte. CPU/CUDA Gaffer passes. CUDA beauty peak
is 1.881; on/off and ordinary-repeat differences are both 1.193e-7 against the
unchanged 4.769e-7 absolute threshold. The validator now accepts HDR inputs but
does not scale the threshold with brightness or repeat error. This is a strict
fixture regression criterion, not a universal HDR rounding bound.

## Final M8 scattering repeats

The final exporter passes the supplied-grid 1024x768/four-sample scattering
workload using Blender's bundled colour configuration. Each device's two deep
files are byte-identical:

| Device | Wall seconds, runs 1 / 2 | EXR bytes | Host working-set peak bytes, runs 1 / 2 | Device-wide peak MiB, runs 1 / 2 |
| --- | --- | --- | --- | --- |
| CPU | 1189.313 / 1211.579 | 1,020,940,868 | 864,317,440 / 862,797,824 | 2409 / 2402 (other applications) |
| CUDA | 1549.500 / 1579.156 | 1,020,932,399 | 1,053,687,808 / 1,061,670,912 | 5260 / 4675 |

Maximum accepted-camera curve error is 2.49700e-7; maximum Gaffer cut error is
2.04364e-7. CPU beauty is exact; CUDA's 1.19209e-7 difference matches its ordinary
repeat and passes the existing 4.76837e-7 threshold. CPU/CUDA deep curves pass
40,034 sampled boundary/midpoint comparisons, maximum error 5.76589e-7.
All four million-point previews validate. Gates remain 40 minutes, 4 GB EXR,
4 GiB host working set, 8 GiB CUDA device-wide memory and 1e-6 curve/cut error.
CUDA still reads back 670.103 GB per render. These are fixture qualifications;
the historical absorption measurements above are not a controlled speedup
comparison. Evidence: `builds/validation/m8-release-scale/report.json` and
[final release audit](src/deep/M8_RELEASE_VALIDATION.md).

## Reproduce and review

```powershell
ctest --test-dir builds/build-m6 -C Release --output-on-failure
```

- Fixture: `tools/create_vdb_deep_scene.py`, including `--width`/`--height`.
- Render pairs: `tools/render_blender_deep_scene.py`; same sample/device settings,
  deep enabled/disabled. CUDA cache: workspace-local `BLENDER_USER_RESOURCES`.
- Gaffer validators: `src/deep/validate_*_gaffer.py`.
- Reviews: `blender-deep/scene-compact-deep/blender_deep_validated_review.gfr`
  and `m8-production/final-scale/m8_production_1024_review.gfr`.
- Source assets unchanged. Recovered user graph edits were kept separately.

[Release status](DEEP_IMPLEMENTATION_STATUS.md) | [M8/M9 scope](DEEP_MILESTONES.md)
