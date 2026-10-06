# Deep alpha: release status

**The historical M8 matrix is qualified; the requested full landscape production completion is open.**
The user approved the earlier final Gaffer scene on 2026-09-29.
The M8 release remains at `c98cf9093` on `codex/deep-exr`. This development
branch adds [landscape-scene compatibility](src/deep/LANDSCAPE_COMPATIBILITY.md)
as part of the user's expanded M8 production test; that configuration is not
yet a qualified production release.
Output is Z/ZBack/A; beauty is separate. Deep RGB belongs to M9.
The latest compact-shader diagnostic reduced renderer GPU memory by 27.9% and
preserved byte-identical full-camera one-sample deep output. Local CPU fitting
optimizations are also tested; these results do not qualify the original
1,024-sample configuration or establish a full-scene speedup.
The frozen-volume-estimate candidate now passes the full original-settings
ordinary beauty repeat gate with normal batching: zero failing pixels, matching
accepted sample counts and maximum raw difference 3.0517578125e-5 against the
unchanged 1.220703125e-4 limit. Its two controls took 397 and 401 seconds.
The full-settings deep-capture comparison also passed: identical accepted sample
counts, maximum raw difference 3.0517578125e-5, denoised difference within the
measured ordinary-repeat envelope, and a 5381 MiB device-wide GPU peak. The fresh
production attempt under `frozen-majorant-production/CUDA/deep` completed
rendering but was found stopped during export on 2026-10-05, without a final
EXR or completion result. Its process/queue handles and temporary capture files
are gone; cause is unknown. Logs and `interruption-observation.json` are preserved.
Final deep export, alpha/resource qualification and connected review remain open.
On 2026-10-05 the user replaced the 1 GiB target with maximum performance on
the available hardware. Source now accepts larger configurable budgets; the
capture test confirms 8192 MiB permits 24 export workers for the landscape.
The higher-budget Blender is now built and installed separately at
`builds/blender/install-landscape-hardware-budget` (SHA-256
`7ab7684eee1f40f8bb6050de08fca6584212b9ed678e80a42f276a62f3a3e90d`).
The capture budget test and six small-landscape export/alpha/beauty checks pass.
Measured export medians on that 47x20/max16-sample case are 12.071 seconds
(4 workers), 8.087 seconds (12), and 7.977 seconds (24), at an 8192 MiB budget.
This small benchmark selects 24 workers; it does not establish full-scene speedup.
The new build's 14-case CPU and 4-case CUDA matrices pass in the replacement
qualification queue. New full-settings ordinary controls finished in 419.890
and 404.625 seconds. Their comparison failed the unchanged raw gate at one
pixel (maximum difference 0.00352514, limit 0.0001220703125), before deep
capture started. The optional sample-count diagnostic was absent; the checker
now handles that explicitly while preserving the raw acceptance gate. The separate
ordinary repeat diagnostic completed and reproduced the same failure: exactly one
pixel received 448 versus 464 adaptive samples. Matching-population pixels differ
by at most 3.0517578125e-5, within the fixed gate. This establishes a population
difference with deep disabled, not its numerical root cause. These diagnostics
cannot qualify production. Instrumented repeats reproduce the stopping-count
difference; the failing pixel's own convergence error is below the threshold
in both runs, so a direct borderline decision at that pixel is not established.
The raw beauty comparison now uses native references with matching accepted
sample counts: every pixel must match at least one native count and stay within
the original fixed tolerance against every reference with that count. Native
references with equal counts must also meet that tolerance. Unknown counts fail;
neither pixels nor channels are omitted. This changes the comparison method,
not the error threshold or renderer. The earlier ordinary-repeat failure remains
recorded. Deep-alpha and denoised-repeat-envelope gates remain unchanged.
The end-to-end smoke passed: 47x20/max16 native CUDA deep/ordinary renders,
accepted-camera oracle and Gaffer cuts; matched raw error 2.3841858e-7 and zero
unmatched populations. On 2026-10-06 the detached production queue started the
full render/export at `hardware-budget-production/CUDA/deep`, with original
1175x500/max1024 adaptive settings, GPU OIDN, 24 threads and 8192 MiB deep memory.
The log confirms the normal 6FAF CUDA kernel and no source override. Capture
completed 1024 maximum samples in 13278.764 seconds; export selected 24 workers.
The job was interrupted by a machine reboot on 2026-10-06 before publication.
Windows recorded an unexpected reboot at 14:42 Sydney time, followed by an
update-related restart at 14:51. Both renderer and queue processes are gone.
The last sampled elapsed time was 52816.250 seconds, with 6363594752 bytes peak
process working set and 6290 MiB device-wide GPU memory. These are incomplete-run
measurements, not completed export timings or qualification evidence.
No final deep EXR, beauty output, render completion report or validation result
was published. The partial EXR and diagnostic CSV are preserved, together with
`hardware-budget-production/CUDA/deep/interruption-evidence.json`. The current
renderer cannot resume this capture from the remaining files. No rerender was
started; the monitor for the dead processes is stopped.
Full production export and qualification remain open.
The queue performs unchanged alpha/beauty/GPU gates and prepares the connected
`landscape-hardware-budget-deep-alpha.gfr` review only after those gates pass.
Production qualification remains unproven.
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

### Current landscape production completion

The user requested completing the original-settings landscape production test
as part of M8. The historical September 29 qualification below remains evidence
for its tested support matrix; it does not close this expanded production task.
The full 1175x500, maximum-1024 adaptive CUDA/GPU OIDN retry published its EXR under
`builds/validation/landscape-cloud/original-resolution-CUDA/CUDA/deep-ledger-bounded-AgX`.
Its first ten-worker attempt failed the pixel-memory gate without publishing
an EXR. Corrected curve reservations and worker preflight pass nine CTests,
14 CPU cases and four CUDA cases. The small landscape passes alpha, beauty and
Gaffer checks with byte-identical deep output. The full four-worker export took
165,570 seconds; it has not solved the performance requirement. The observed
device-wide GPU peak of 9,811 MiB failed the 8,192 MiB gate. The fixed raw-beauty
gate also failed, including an ordinary/ordinary repeat with the same executable.
Independent deep-alpha validation passes 123,216,166 probes across 81 pixels
(maximum error 2.29472e-7); Gaffer depth cuts pass at 9.76050e-7. A connected
one-million-point diagnostic review is available, labelled as failed production
qualification. Resource, beauty and performance remain unresolved. See
[landscape production evidence](src/deep/LANDSCAPE_COMPATIBILITY.md).

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
