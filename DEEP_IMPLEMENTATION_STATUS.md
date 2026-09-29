# Deep alpha: release status

**M8 is complete. Deep alpha is production-ready within the qualified CPU/CUDA support matrix.**
The user approved the final Gaffer scene on 2026-09-29.
Output is Z/ZBack/A; beauty is separate. Deep RGB belongs to M9.
See [release gates and roadmap](DEEP_MILESTONES.md).
The explicit [support matrix](src/deep/RELEASE_MATRIX.md) separates surface
features from static fixed-sample volume capture.

## Works today

- CPU/CUDA surface capture, scalar transparency, homogeneous absorption and
  scalar Henyey-Greenstein scattering extinction.
- Native FLOAT VDB density with linear interpolation and scalar absorption or
  Henyey-Greenstein scattering extinction.
- Bounded capture/spill, explicit overflow and atomic EXR publication.
- Full Blender scene: 664x625, 128 samples; optimized output is byte-identical.
- VDB: 256x256, one sample on CPU/CUDA; beauty unchanged on each device.
  CPU 664x625/four-sample export completes with exact beauty and passing Gaffer
  reader/cut/point-cloud checks. The repeat is byte-identical; 241,222 accepted-camera
  depth probes pass with maximum error 3.42e-7 on both runs.
- Both outputs inspected live in Gaffer through DeepToPointCloud.

These are tested configurations, not arbitrary Cycles compatibility.
Surface adaptive sampling, DOF and rigid motion have separate validation;
those results do not establish support for volume combinations.

## M8 release gates

| Gate | Status / remaining work |
| --- | --- |
| Production scale / performance | Pass. Final 1024x768/four-sample scattering repeats: CPU 1189.313 / 1211.579 s, CUDA 1549.500 / 1579.156 s; about 1.02 GB per EXR, byte-identical repeats within each device. Host peaks at most 1,061,670,912 bytes; CUDA device-wide peak at most 5260 MiB. All time, memory and size gates pass. |
| Multiple samples | Final-build CPU/CUDA four-sample VDB matrix passes. Fresh installed-build surface transparency, adaptive, DOF and motion suites pass. |
| Scene coverage | Pass. Corrected default-colour native CPU/CUDA matrix passes 11 accepted scenes and nine safe rejections per device, including mixed, overlapping, inside/clip, scattering, transforms, filters and folded zero density. Clipped voxel-start arithmetic passes CPU/CUDA unit/grid oracles. Homogeneous suites cover 28 accepted scenes and 19 rejections per device. |
| Reduction | Pass. Bounded reconstruction merges and export coalescing keep the unchanged 1e-6 whole-curve budget. CPU overlap falls from 4,351,561 to 1,594,908 stored intervals; accepted-camera error 2.130e-7 and Gaffer cut error 6.757e-7. Final matrix and production repeats pass. |
| Reliability | Final nine CTests and all ten renderer/Gaffer suite groups pass. CPU/CUDA callback/cancellation recovery, injected device-error suppression, reset/re-enable and capacity changes pass. Allocation/source-I/O/row-capacity failures preserve prior EXRs. Stream-write failures propagate; a real Windows locked destination preserves the completed EXR and cleans staging. |
| Scattering | Pass. Final native CPU/CUDA VDB matches equivalent absorption byte-for-byte. Release-scale accepted-camera error is at most 2.49700e-7 and Gaffer cut error at most 2.04364e-7. Sampled CPU/CUDA curve comparison passes at 5.76589e-7. CPU beauty is exact; CUDA matches its ordinary repeat within the unchanged threshold. |
| Release review | Pass. Full supplied Blender asset passes at 664x625/128 samples: legacy deep EXR byte-identical, fresh beauty exact, curve error 3.10044e-9, Gaffer cuts exact and valid 1M-point preview. All technical qualification is complete. The user approved the final Gaffer scene on 2026-09-29. |

Do not move failed promised capabilities into M9 to close M8.
Boundary regression evidence: `builds/validation/m8-production/precise-boundary/`.
The revised CPU/CUDA kernels pass 28 scenes and 19 rejection cases; nine CTests pass.
Production qualification: `builds/validation/m8-production/final-scale/projected-1024-report.json`.
The Gaffer review is `m8_production_1024_review.gfr` in that directory.
The user reviewed that production-scale checkpoint and approved the final M8
Gaffer scene on 2026-09-29. Final matrix artifacts:
`builds/validation/m8-release-qualified/native-default-colour/`.
Final scale report: `builds/validation/m8-release-scale/report.json`.
Final review: `builds/validation/m8-release-qualified/m8_final_release_review.gfr`;
all 15 saved boxes reload with valid deep point clouds. Select a box and press F
in Gaffer's Viewer; its depthCut controls slice the stored deep samples.
See [final qualification evidence](src/deep/M8_RELEASE_VALIDATION.md).

## Evidence and code

- [Measured results](DEEP_PERFORMANCE_AND_VDB.md)
- [Native VDB implementation](src/deep/NATIVE_VDB_PLAN.md)
- [Blender integration](BLENDER_DEEP_INTEGRATION.md)
- [Build setup](BUILDING.md)
- Core: `src/kernel/deep`, `src/deep`, `src/session/deep.cpp`, `src/integrator`.
- Generated files: `builds/`; supplied assets: `test-assets/` (not committed).
- Checkpoints: `1ac6ce87c` (compact capture), `8fb124b8d` (native VDB),
  `d502875fd` (live review).

Historical implementation details remain in Git history and existing feature
validation reports. They do not supersede this release status.
