# Landscape and cloud compatibility work

The historical M8 release matrix remains documented in RELEASE_MATRIX.md.
The user requested closing this expanded landscape production qualification as
part of M8. It remains unqualified on `codex/landscape-cloud-compatibility` until
the full original-settings run and review pass; the install-m9 folder name is
an existing development path, not a reason to defer the requested work.

The user supplied `blender-3.3-splash.blend` and ten CloudPack VDBs, and explicitly
requested preserving the original appearance while extending renderer support.
Assets remain local. A separate copy repairs VDB paths without changing shaders
or rendering settings; its provenance is under
`builds/validation/landscape-cloud/scene-paths-fixed.json`.

Inspection found FLOAT density grids loaded at HALF precision, native adaptive
sampling, GPU OpenImageDenoise, textured environment lighting and cloud density/phase
expressions driven by Light Path Ray Depth. The original scene has 359 meshes,
10 volume objects, geometry-node/particle modifiers, a perspective camera and
2350x1000 resolution at 50%, with a maximum of 1024 samples. A small diagnostic
render fails the M8 adaptive-volume preflight as expected.

Compatibility extensions:

- Preserve native adaptive accepted-camera populations through volume export.
- Read the same NanoVDB Fp16 values as native beauty shading.
- Admit textured background radiance while rejecting world volumes.
- Prove camera-constant scalar expressions involving Ray Depth, multiplication,
  addition and power. Ray Depth is zero on an unscattered camera path and does
  not count transparent bounces. Keep the original beauty shader graph intact.
- Capture combined surface/volume water and prove equal-colour, equal-density
  absorption plus scattering as scalar extinction without FLOAT cancellation.
- Follow native world-Z-up initial-volume classification for open and concave
  boundaries. Preserve camera clipping and the original geometry.
- Admit native surface inputs, AO used only in opaque shading, and reflected
  surface meshes; continue rejecting AO-driven opacity and reflected volumes.
- Fit dense cubic cells using their already-absorbed transmission prefix.
  Preserve total optical depth and the existing 1e-6 published curve budget.
- Split continuous intervals before FLOAT alpha loses optical-depth precision.
- Offer 8192 native-grid events explicitly through Deep Events Per Sample,
  retaining 4096 by default. GPU capture first tries 4096 lanes with 64 event
  slots and retries unfinished groups at wider capacities, within the existing
  32 MiB host/device staging reservation. Final overflow remains an error.

Required evidence before claiming support: meaningful regression tests, native
CPU/CUDA beauty pairs, accepted-camera deep curves and Gaffer depth cuts, plus
the actual production scene. Its high maximum sample count and evaluated
geometry may expose additional resource or compatibility limitations. Small
diagnostic previews do not qualify the full original render settings.

Historical M8 qualification identifies its executable by SHA256 in
`M8_RELEASE_VALIDATION.md`. On October 3 the configured INSTALL target refreshed
`builds/blender/install` with the development binary; that directory no longer
contains the qualified M8 executable. The production compatibility test uses
`builds/blender/install-m9`. Historical qualification reports are unchanged.

## Development evidence and remaining work

The CPU/CUDA compatibility matrices pass 18 cases, including retained rejection
checks (`compat-final-CPU`, `compat-CUDA`). Each supported case passes its own
beauty pair and Gaffer curve checks; equivalent camera extinction remains
byte-identical within each device. The adaptive fixture accepts 16 of 64 maximum
samples, establishing early convergence. These matrices precede the expanded
capacity setting, which has separate qualification.

The capture unit test passes different accepted populations, including misses,
for both memory and spill storage. An independent NanoVDB SDK decoding and
Gaussian-quadrature test passes 64 quantized cells at 2.45919e-10 maximum
transmittance error. The existing OpenVDB oracle passes 128 rays/89,102 cells at
2.67564e-9. Evidence is under `builds/validation/landscape-cloud/`.

The water-coefficient test matches a separate scalar-absorption object exactly
over 26,832 CPU comparisons. CPU/CUDA boundary tests include open entry, open initial
occupancy, missing sides, sideways camera rays without a boundary crossing, and
a welded concave mesh. Both final suites pass 33 render cases and 17 expected
atomic rejections (`boundary-final-CPU`, `boundary-final-CUDA`).

The actual landscape passes a 117x50, two-sample CPU diagnostic render
(`deep-pilot-8`). Its 17,190,150-byte EXR contains 2,305,233 deep samples. Gaffer
checks 775,962 accepted-camera probes: maximum error 2.50148e-7, slice error
1.40722e-7, and deep-on/off beauty difference exactly zero. This preserves the
scene's shaders, environment, HALF grids and adaptive/denoising settings, with
lower resolution and maximum sampling explicitly used for diagnosis.

All nine CTests pass. The actual CUDA grid oracle passes 128 rays/89,102 cells at
2.67564e-9, including appended writes at the 8192-slot buffer end and overflow
rejection. The independent CPU grid oracle has the same maximum error; its
maximum fitted interval count falls from 11,169 to 9,113 after attenuation-aware
fitting. These checks do not substitute for a full Blender CUDA render.

A 587x250, four-sample landscape attempt exposes the 4096-event limit. The
8192-event retry completes (`review-profile`): 394,216,792 bytes, 52,179,407 deep
samples, 674.45 seconds including diagnostic export logging. The ordinary beauty
pair is identical. Gaffer passes 1,321,258 accepted-camera probes (2.24964e-7
maximum error) and depth cuts (8.42107e-7). The connected review is
`builds/validation/landscape-cloud/landscape-deep-alpha-preview.gfr`, with one million
displayed points and a pixel stride of four. The source .blend hash is unchanged.
The review camera follows Cycles' removal of Blender object scale; world-point
axial depths agree with the EXR within 0.000123 scene units in the checked points.
Temporary per-pixel diagnostic logging was removed from the installed build.

The full Blender expanded-capacity CUDA test passes at
33x17, three samples (an incomplete final batch), with beauty difference zero
and accepted-camera error 2.45686e-7. Distinct error messages now identify event,
medium, boundary and grid-step capacity.

Higher-sample reconstruction now budgets actual retained camera data, compacts
each camera curve, and combines weighted curves in balanced pairs using the
existing fitter. Capture feeds the balanced tree as it reads each camera ray,
retaining only logarithmically many partial curves; reconstruction takes
ownership of input storage and indexes surface transmission instead of
rescanning every surface at every depth. The total error allowances remain unchanged. Real pixel and
row memory overruns still reject publication. Nine CTests pass, including
independent 1024-camera curves, actual budget guards and dense-data reconstruction
within 64 MiB. The 23x10, maximum-1024 stress render now gets past its old memory
failure but reaches the 65,536-interval capacity (`original-sampling-streamed-curves`).
This unusually broad pixel footprint remains an explicit rejection, not a
qualified original-resolution render.

CPU and CUDA scattering/denoising fixtures pass (`denoise-scatter-CPU-qualified`,
`denoise-scatter-CUDA-qualified`). CUDA compares native noisy passes against the
fixed raw gate and denoised output against an independent ordinary repeat;
native OIDN variability is measured separately. The new batching also passes
these CUDA fixtures and an incomplete expanded-capacity batch (`batching-CUDA`).
The latter EXR is byte-identical to the previous expanded-capacity result.
Final denoising, adaptive, expanded-capacity and opaque-foreground CUDA fixtures
pass with streamed reconstruction and surface indexing (`indexed-final-CUDA`).
The final blocked spill index also passes these cases (`batched-final-CUDA`).
Separate scattering fixtures explicitly enable GPU OpenImageDenoise
(`gpu-oidn-CUDA`): fixed and adaptive cases both pass native noisy-pass isolation,
denoised beauty pairs and deep curve checks. Their beauty differences are zero;
the maximum accepted-camera curve errors are 2.30684e-7 and 2.19273e-7.

Spill metadata groups sixteen camera samples per pixel before advancing to the
next pixel, instead of reserving all 1024 samples between adjacent writes.
The actual landscape's first-sample spill time falls from 41.2 to 1.53 seconds.
A unit check covers 1920 pixels with a maximum of 1024 samples and verifies
metadata read traffic below 2 MiB, including finalization; partial blocks are
also covered. No storage format or optical-depth error allowance changes.
The full-scene qualification scopes Windows TEMP/TMP to
`D:/CyclesDeepScratch/landscape-cloud-20261001` because its temporary per-camera
spills exceed the available space on C:. Durable evidence stays under `builds`;
temporary files retain their automatic close/delete lifecycle.

Blender measurements must unset `OCIO` in the child environment after any Gaffer
launcher. Clearing it in the parent shell is insufficient: Gaffer sets its ACES
configuration again. The `deep-retained-budget-final` attempt was cancelled when
its startup log showed ACES replacing AgX, and is excluded from qualification.
The corrected `deep-retained-budget-AgX` invocation clears `OCIO` inside Gaffer's
Python before running the existing measurement helper; Blender then uses its
bundled colour configuration, as do the paired ordinary baselines.

Capture now stops at exact opaque surfaces. A foreground surface with a
one-event buffer passes on CPU/CUDA despite a VDB behind it (`opaque-stop-CPU`,
`opaque-stop-CUDA`). Constant zero NanoVDB tiles can skip their interior while
retaining the interpolation halo. FLOAT/Fp16 tests cover negative and positive
directions and inactive nonzero tiles. The independent CPU and CUDA VDB checks
retain the same 2.67564e-9 maximum error (`empty-grid-oracle.log`,
`cuda-empty-grid-oracle.log`). The updated CPU compatibility matrix also passes
(`empty-tiles-CPU`).

The original 1175x500, maximum-1024 ordinary CUDA beauty and independent repeat
complete with GPU OIDN in about 389 seconds each (`original-resolution-CUDA`).
The initial deep attempts were stopped to improve capture throughput. A later
run completed all 1024 scheduled samples with GPU OIDN, but rejected export at
row zero under the 1 GiB deep-memory budget (`deep-disk-qualified`). Its 15,172
seconds and 6,418 MiB device-wide GPU peak are failed-run measurements, not
qualification evidence.

The writer had reserved FLOAT channel arrays at the unreduced DOUBLE interval
count and charged that count to the retained scanline budget. It now allocates
and budgets the final converted arrays, including their capacities. DOUBLE
conversion scratch remains separately reserved. A regression publishes 128
constant-density source intervals under a one-sample retained-row budget;
overfull rows still reject atomically.

The fitter also excludes exactly extinguished rays from its rate-range bound:
zero transmission contributes nothing to the mixture's curvature. A regression
falls from 1,670 to 74 intervals and passes 1,001 independent Beer-Lambert probes
under the unchanged reconstruction allowance. Neither fix changes error budgets
or uses an approximate opacity cutoff. All nine CTests pass after these fixes
(`ctest-row-final.log`).

Fresh installed-build checks pass 14 CPU compatibility cases, including five
expected rejections (`retained-final-CPU`), and four CUDA cases with GPU OIDN,
adaptive sampling, expanded capacity and exact opaque foreground termination
(`retained-final-CUDA`). Both reports identify executable SHA256
`f821957384ef6052682c9cbe82aa520477480eaa60e0416325f426ffc5ce5d70`.

None of these runs qualifies the original deep output. Final original-scene deep rendering,
resource measurements, curve checks and connected Gaffer review remain pending.
At that checkpoint the extension was recorded as M9 development. The user has
since requested closing its full production qualification as part of M8.

The corrected full retry completed capture and native CUDA GPU OIDN in
13,807.483189 seconds. Its 2,580,988,605-byte accepted-camera diagnostic CSV is
byte-identical to the earlier `deep-disk-qualified` capture (SHA256
`dae62fbd9d22e7b76ffe48785a0d0c476d17142cfc2b2e4cd141d67ee55ec561`).
This establishes unchanged raw extinction on the diagnostic grid; final EXR
export and qualification are still pending. This run was subsequently cancelled
for export performance work after 21,342 seconds overall. The initial report
that its first scanline was incomplete was incorrect: Windows directory metadata
can retain a zero length for an open output file. Inspection of its 32,653,312-byte
unpublished staging file confirms two complete scanline chunks. It is not
qualification evidence; use a shared read handle to measure active staging size.

Pair reconstruction now tightens the log-mixture curvature bound using its
normalized endpoint weights. With two constant rates, the weight is monotonic
between boundaries; maximum weighted variance is at an endpoint or the
equal-weight crossing. Child intervals reuse weights from the existing curve
evaluation. No opacity cutoff or error allowance changes. A partial-coverage
regression passes 2,001 Beer-Lambert probes under 2.5e-9; all nine CTests pass
(`ctest-weighted-cached.log`). The targeted two-ray benchmark measures fitting
alone at 0.204872 seconds before and 0.0037495 seconds after, with 14,262 versus
12,564 intervals and 10,001 independent probes below 2.5e-9. This is a single
component measurement, not an end-to-end landscape speedup.

The previous weighted-fit installed Blender SHA256 was
`bbf6e685867373025dd76f2b9639187fa915a81b3aee4ffd27a83a7a3225397a`.
Its 14 CPU and four CUDA cases pass (`weighted-cached-final-CPU`,
`weighted-cached-final-CUDA`), including safe rejections and GPU OIDN. The
independent adaptive CUDA oracle passes 1,561,922 probes at 2.19273e-7 maximum
error. The original-resolution, maximum-1024 rerun is
`original-resolution-CUDA/CUDA/deep-weighted-variance-AgX`; publication,
full-scene validation and review remain pending.

The serial weighted-fit run completed capture and GPU OIDN in 13,123.276172 seconds; its
raw diagnostic CSV has the same hash above. At approximately 20:08 Sydney on
October 2, the unpublished staging file held four scanlines of 500 and
56,041,472 bytes. This is partial progress, not final size or qualification.
A 400-sample process-local CPU profile (`export-cpu-profile.json`) identifies
curve reconstruction and its exponential/logarithmic evaluations as the main
work; cubic-density integration accounts for only 11 samples. No full-scene
export speedup is established. The brief diagnostic profiling period is part
of the export wall time and must be disclosed with final performance figures.

## Bounded parallel export and production retry

The serial weighted-fit run was cancelled for the requested performance work
after 34,969.625 seconds overall, with seven of 500 complete scanlines and an
unpublished 91,582,464-byte staging file. It is an incomplete baseline, not a
qualified render. Its raw diagnostic capture remained byte-identical to the
earlier captures above.

Export now uses the existing TBB scheduler to fit independent pixels in parallel.
The memory preflight preserves the serial row allowance and bounds worker count
and per-worker scratch inside the same deep working budget. The original scene
selects ten workers from 24 requested threads at 1024 MiB. Shared capture reads
remain protected; OpenEXR writing and publication remain serial after each row
joins. Tests cover byte-identical serial/parallel export, concurrent spilled
reads, low-budget fallback, cancellation, worker failure and row overflow.

Curve evaluation reuses exact adjacent boundary values and advances ordered
boundary queries through source intervals. Recursive fitting retains random
access. Neither change alters the reconstruction method or error budgets.

Current installed Blender SHA256:
`7116491030db6484c12bee4899c250031447564cd449d87100d53ef9ec23dcc0`.
All nine CTests pass (`ctest-parallel-sweep.log`); fresh installed-build checks
pass 14 CPU cases and four CUDA cases (`parallel-sweep-final-CPU` and
`parallel-sweep-final-CUDA`), including expected rejections and GPU OIDN.

### Measured landscape benchmark

Actual landscape, 47x20, maximum 16 adaptive samples, CUDA with native GPU OIDN,
8192 capture events and 1024 MiB deep working budget. Each row is one measurement;
export includes the diagnostic CSV and deep EXR. GPU peak is device-wide.

| Export version | Actual workers | Export seconds | Process seconds | GPU peak MiB |
| --- | ---: | ---: | ---: | ---: |
| Serial weighted fitting | 1 | 42.8911 | 69.313 | 7424 |
| Parallel, serial row allowance preserved | 10 | 12.5805 | 29.141 | 6644 |
| Parallel plus exact boundary reuse and ordered sweep | 10 | 9.18676 | 25.406 | 6577 |

The final small export is 4.67 times faster than the serial measurement. Its
4,445,714-byte EXR is byte-identical to serial (SHA256
`31fda17a7c019ef90c6c6f02e8e19b2dcb85f7fca2e9ba3ed79c05df223ac1fa`).
Independent validation passes 5,176,086 accepted-camera probes with maximum
error 2.201314e-7. Denoised beauty matches exactly; raw beauty difference
2.384186e-7 remains inside the fixed 1.907349e-6 allowance. Final process peak
working set is 5,532,061,696 bytes, including the ordinary scene and beauty.
These timings do not establish a full-resolution export speedup.

The full original-settings retry is
`original-resolution-CUDA/CUDA/deep-parallel-AgX`: 1175x500, maximum 1024 adaptive
samples, CUDA/GPU OIDN, original shaders, HALF grids, HDRI and camera/filter.
Rendering, atomic publication, full validation and connected Gaffer review are
pending. The source asset is unchanged.

## High-population memory correction

The `deep-parallel-AgX` full run completed CUDA capture and GPU OIDN in
12,775.012101 seconds, then failed closed during export: pixel 883,499 retained
20,636,672 bytes against a 20,585,866-byte per-worker allowance. Process time was
13,063.578 seconds; device-wide GPU peak was 6787 MiB. No final EXR was published
and full-scene validation did not run. The small-scene pass above did not expose
this high-population ledger requirement.

Retained curves now reserve only their actual surface and volume entry counts,
rather than reserving the complete curve size in both arrays. Worker preflight
accounts for one maximum-size curve per retained streaming-tree level. The
1175-wide, maximum-1024 configuration selects four workers within the unchanged
1024 MiB deep budget; the serial row allowance and accuracy limits are unchanged.
The width/sample regression checks this selection without initializing a full
production-sized capture file. All nine CTests pass in 4.18 seconds
(`ctest-ledger-budget-final.log`).

The exact failing pixel is outside the saved diagnostic grid. Replay of its
nearest saved neighbor (880,0 in EXR coordinates) uses 352 accepted camera rays
and 3,126,957 raw records. Retained memory falls from the previous reservation's
55,185,408 bytes to 27,592,704 bytes; final output has 30,935 intervals. Eight
independent depth probes have maximum error 6.92846e-9. This is diagnostic replay,
not full-scene qualification or a timed production benchmark.

New installed executable SHA256:
`5d0708f824fb45f379518935f78419c7b00e5b35fa18ce7796743e3dd7b7060a`.
Fresh CPU/CUDA matrices pass 14 and four cases respectively
(`ledger-budget-final-CPU`, `ledger-budget-final-CUDA`). The small landscape
benchmark passes independent alpha, beauty and Gaffer cut checks, with the same
5,176,086 probes and 2.201314e-7 maximum camera error as the previous build.
At 16 maximum samples the preflight selects eight workers: export 10.4079 seconds,
process 27.25 seconds, device-wide GPU peak 6190 MiB, process peak working set
5,539,999,744 bytes. The EXR remains byte-identical to serial. Export is 4.12 times
faster than the 42.8911-second serial measurement; this is one small-scene run,
not a full-scene speedup. The previous 9.19-second measurement used a worker
allocation that failed the full-scene memory gate.

The full retry `original-resolution-CUDA/CUDA/deep-ledger-bounded-AgX` finished
on October 5, but is **not qualified**. The renderer exited successfully and
published a 4,108,740,437-byte deep EXR. Four-worker export took 165,570 seconds
(about 46 hours); the measured whole process took 178,714.563 seconds. Spill
reads totalled 4,601,353,434,020 bytes and the spill files reached 254,184,856,420
bytes. Bounded parallelism alone has not solved full-scene performance.

The resource gate failed: device-wide GPU peak was 9,811 MiB against the unchanged
8,192 MiB limit. The existing monitor recorded only the aggregate peak, without
timestamps or application attribution, so its cause cannot be established from
that report. Process peak working set was 5,870,800,896 bytes.

The first beauty comparison also failed the fixed raw tolerance: deep/off maximum
error 0.1650557518 and ordinary/ordinary repeat error 0.1321629882, versus
0.0001220703125. Fresh ordinary renders with the same current executable
(`beauty-ledger` and `beauty-ledger-repeat`) also fail: raw deep/off error
0.1650557518, raw ordinary/repeat 0.1271969676. These ordinary renders took
394.641 and 389.984 seconds and peaked at 4,749 and 4,795 MiB device-wide GPU
memory. The worst deep/off raw pixel is (1120, 370), red channel; the two ordinary
values at that pixel agree closely. Repeatability and that localized deep/on-off
difference both require investigation.

Independent accepted-camera alpha validation passed all 81 diagnostic pixels
and 123,216,166 depth probes: maximum error 2.294721579e-7. Gaffer depth cuts
passed at 9.760500105e-7 against the unchanged 1e-6 threshold, and the point-cloud
preview contains 1,000,000 valid points. The EXR stores 718,786,068 samples, with
at most 9,277 per pixel. Alpha correctness does not waive beauty or resources;
the aggregate Gaffer validation remains failing. This full production
configuration remains open.
The pipeline stops on any failed render, resource or validation gate, and opens
the connected Gaffer review only after all full-scene checks pass.
The separately prepared diagnostic review is
`builds/validation/landscape-cloud/landscape-original-deep-alpha-diagnostic.gfr`.
It requires passing independent alpha and depth-cut checks, reloads with valid
points and a maximum world-depth error of 6.103515625e-5, and explicitly states
that production qualification failed. It was launched in Gaffer; human review
is pending. The full run's `production_qualification.json` records `passed:false`
and input/output fingerprints so passing numerical evidence can be retained
while investigating the failed gates.

The final standalone source was rebuilt after reverting the exploratory fitting
changes. All nine CTests pass in 4.13 seconds (`ctest-post-revert-final.log`), and
the tools were installed explicitly into `builds/install-m9`. This does not
replace the production Blender executable. Short diagnostic builds and CPU replays
ran concurrently with the full export, so its elapsed time is indicative rather
than a controlled performance comparison. No full-scene speedup is established.

## Rejected validation threading experiment

On the small 47x20 fixture, one/two/four query workers gave identical reports
(81 pixels, 5,176,086 probes), but four workers did not improve speed: 11.547 /
10.672 / 11.750 seconds. Increasing batches from 4,096 to 65,536 queries also
failed to improve this workload; parallel large batches were slower. The
experimental options were reverted, leaving the original checker unchanged
(SHA256 `e8e9225b156ea3d4274aad2943a7d1f21c7b16e66bd9fbfb1a6a151c7c2129e4`).
These are single small-fixture measurements while the full serial oracle ran,
not a full-scene speedup. Logs remain in `oracle-worker-benchmark.json` and
`oracle-batch-benchmark.json` under the landscape validation directory.

## Targeted export and validation improvements (October 5)

The fallback merge-error calculation now stops once its monotonically increasing
maximum exceeds the existing reduction allowance. The roundoff allowance and
acceptance test are unchanged. Three paired replays of the saved dense landscape
pixel (352 accepted rays, 3,126,957 raw intervals) measured median fitting time
6.05073 seconds before and 5.33606 seconds after: 11.81% faster. All six output
curves have SHA256
`146435d574574d4cd48bf84fea6f5fb3219177d1cb675d75d591aca17f511265`.
This is a local CPU fitting benchmark, not a full-scene speedup or qualification.
Evidence: `export-merge-short-circuit/repeated-benchmark.json` and adjacent logs.

All nine standalone CTests passed in 4.48 seconds after this change. The separate
Blender installation `builds/blender/install-landscape-merge` has executable SHA256
`124e452833490029fb742b4b8d3c06e9e104f4ead0df03c56b6ce4788cf3bfe6`;
all 14 native CPU and four native CUDA cases passed with that executable
(`merge-short-circuit-final-CPU/report.json`, `merge-short-circuit-final-CUDA/report.json`).
The completed full production EXR belongs to the previous executable and is not
evidence of full-scene qualification for this new build.

A second experiment reused volume-only boundary queries. It produced identical
replay curves, but the three timings were inconsistent (median 5.74182 versus
5.93782 seconds for the merge-only build, with one slower result). It was reverted;
the additional branch is not included in the final source. Its nine CTests also
passed, but that does not establish a worthwhile performance improvement.

The resource monitor now writes live snapshots every 30 seconds, records the
device-wide peak's timestamp and render-log context, and additionally samples
Windows' per-process dedicated GPU-memory counter. Missing counters remain
unavailable rather than zero. The unchanged 8,192 MiB device-wide gate remains
authoritative. A real CUDA/GPU OIDN fixture measured 6,115 MiB device-wide and
3,551,928,320 bytes for the renderer process, with two valid process samples and
no monitoring errors (`process-gpu-monitor-smoke/measurement.json`). This verifies
the monitor; it does not explain or waive the previous full run's 9,811 MiB peak.

Two additional original-settings ordinary landscape renders with sample-count
passes found 588 pixels with different accepted populations and 1,033 pixels
outside the fixed raw beauty tolerance; 587 of those failures had different
populations. Maximum error was 0.1134551764 even among equal-population pixels.
All 81 diagnostic deep populations match both ordinary renders. Thus population
differences alone do not explain the raw failure (`sample-count-diagnostic.json`).

Ordinary border diagnostics covering the failed region finished in 12.781 and
13.078 seconds. Their raw maximum difference was 2.384185791e-7, below the fixed
tolerance, but both reached 1,024 samples instead of the full-frame pixel's 192.
They are only failure-isolation evidence. Deep rejects camera borders as designed;
the attempted deep border diagnostic failed safely and is not qualifying evidence.
Full-scene beauty, GPU resources, and export performance remain open.

The full-camera one-sample diagnostic completed with the merge-only Blender
build in 126.219 seconds, including 79.6364 seconds of export with 24 workers
under the same 1,024 MiB deep CPU budget. This lower population permits more
workers; it does not change the four-worker limit of the 1,024-sample run.
It used original geometry, VDBs, camera, shaders, CUDA and GPU OIDN but reduced
sampling explicitly, so it is not original-settings qualification. Device-wide
peak was 7,903 MiB and renderer dedicated GPU peak 5,493,903,360 bytes.
Raw deep/off and ordinary/repeat maximum differences both measured
1.907348633e-6; denoised beauty was identical. Evidence:
`full-camera-one-sample/beauty-diagnostic.json` and the three measurement reports.

CUDA kernel metadata for this executable reports 22,656 local bytes per thread
for `deep_surface`, compared with 6,864 for native surface shading and 7,824
for native volume shading (`cuda-kernel-resources.json`). This is static kernel
metadata, not a measured allocation breakdown. Compact shader storage is being
tested to reduce this overhead; no measured improvement or qualification is
claimed yet.

A direct recursive lambda avoids allocating `std::function` for each fitting
span. Three paired dense-pixel replays measured medians 6.44017 seconds with
the merge-only build and 5.56501 seconds with direct recursion (13.59% faster).
All six curves match the same SHA256 above. This measurement was made separately
from the early-merge benchmark; do not add the two percentages or extrapolate
them to full-scene performance. Evidence: `export-merge-short-circuit/direct-fit-benchmark.json`.

### Compact shader storage measured

Deep traversal now uses native `ShaderDataTinyStorage` and opacity-only surface
evaluation with closure storage disabled. Camera visibility and path flags are
preserved; they are not replaced with shadow or emission flags. Normal surface
evaluation keeps its existing default behavior. No per-thread GPU allocation is
introduced. CPU storage uses Cycles' existing full-sized alias.

The compact build (executable SHA256
`912ae6da9a1ed1e74b059059060f29dae69715e354cf489167004f0f70fdb742`)
passed all 14 CPU cases, all four CUDA cases, and nine standalone tests in
4.10 seconds. Static deep-kernel local storage fell from 22,656 to 7,312 bytes
per thread (67.7% less); native surface and volume kernel resource counts
are unchanged. The paired one-sample full-camera diagnostic measured renderer
GPU dedicated peak 5,493,903,360 -> 3,960,868,864 bytes (27.90% less), and
device-wide peak 7,903 -> 6,539 MiB. Both the 1,458,895,482-byte deep EXR and
accepted-camera CSV are byte-identical between these builds.

This diagnostic did not demonstrate a speedup: wall time increased from
126.219 to 133.703 seconds; export increased from 79.6364 to 83.0736 seconds.
It is one run per build with desktop applications open, not a controlled
original-settings performance result. Evidence: `compact-shader/benchmark.json`,
`compact-shader/cuda-kernel-resources.json`, `compact-shader-final-CPU/report.json`,
and `compact-shader-final-CUDA/report.json`.

An additional bounded cache retains the 64 merge-source extinction rates instead
of recomputing logarithms for every candidate merge. Three paired dense-pixel
replays measured medians 6.73908 -> 4.67538 seconds (30.62% less), with identical
output hashes; the third pair improved only about 1%, so these timings are
indicative. CUDA compilation ran during this small benchmark. All nine standalone
tests pass in 4.00 seconds. The final combined Blender installation is
`builds/blender/install-landscape-efficient`, executable SHA256
`b83a0b3146a33b024665e32043a85b24a95ffadeb87ad048a7aed349595d0036`.
All 14 CPU and four CUDA native cases also passed with this final combined
executable (`efficient-final-CPU/report.json`, `efficient-final-CUDA/report.json`).
The compact-build diagnostic
above does not include this rate cache. No full original-settings qualification
or aggregate speedup is inferred from these incremental measurements.

The final combined build's one-sample full-camera diagnostic completed in
123.266 seconds, including 76.732 seconds of export. Relative to the earlier
merge-only diagnostic, this is 2.34% less wall time and 3.65% less export time,
within a single-run comparison; no full original-settings speedup is established.
Renderer GPU peak remains 3,960,868,864 bytes; device-wide peak was 6,725 MiB.
The EXR and camera CSV are byte-identical across all three builds. Independent
validation passed 81 pixels / 444,794 depth probes with maximum error
2.607264841e-7, and Gaffer depth cuts passed at 5.983607942e-7. One million
preview points are valid, and denoised beauty matches exactly. The ordinary
beauty reference is the older merge-only build, so this is cross-build diagnostic
evidence, not original-settings same-executable qualification. Evidence:
`efficient-full-camera-one-sample/deep/gaffer_validation.json`,
`efficient-full-camera-one-sample/deep/accepted_camera_oracle.json`, and
the updated `compact-shader/benchmark.json`.

### Capture-first diagnostics

`tools/render_blender_deep_scene.py --capture-only` requires deep volume capture
and saved native passes. It finalizes capture identities, runs native rendering
and denoising, and saves beauty plus all-pixel Debug Sample Count. It skips curve
fitting and deep EXR publication. The report explicitly records
`deep_published: false`; the production validator rejects these directories.
The mode is a diagnostic prerequisite, not deep-alpha qualification.

The diagnostic executable SHA256 is
`6227c5e8fccd54dc80f0723cb41a031c750eec4b87b26701b13487468e52b829`.
CPU and CUDA 33x17 / four-sample smoke pairs match ordinary raw and denoised
beauty exactly, finalize capture, and publish no deep EXR. All nine standalone
tests passed in 3.86 seconds. The negative production-validator check failed
as expected with `Capture-only diagnostics cannot qualify deep output`.
Evidence: `compact-shader/capture-only-smoke.json` and
`compact-shader/capture-only-rejection.log`.

All 291 installed GPU source files match the efficient renderer. The existing
`CYCLES_KERNEL_PATH` override reuses that verified source tree and native cache
without another cold GPU compilation. The original-settings CUDA diagnostic
under `original-settings-capture-check/CUDA` completed its ordinary controls;
capture was deliberately stopped after their raw comparison failed.
The unchanged 1 GiB deep CPU budget,
8192 MiB device-wide GPU gate, raw beauty limit, and denoised repeat envelope
still apply. This diagnostic does not provide full production qualification.

Source investigation identified a possible native repeatability mechanism:
`volume_majorant_optical_depth()` reads accumulated optical depth and count
from the live render buffer (`kernel/integrator/shade_volume.h`). Other
terminating paths update those same passes via `write_optical_depth()`
(`kernel/integrator/state_flow.h`). Camera rays use those estimates to guide
volume scattering, and GPU tile scheduling can have several samples of one
pixel in flight. This is a candidate execution-order dependency, not an
experimentally established explanation for the measured beauty failure.
The diagnostic compares all-pixel accepted counts and reports raw error at
equal populations; no sampling behavior or acceptance gate has been changed
to make the test pass.

The fresh same-executable ordinary controls completed in 401.000 and 401.282
seconds. Their raw comparison failed: maximum error 0.1558516026 versus the
unchanged 0.0001220703125 limit; 986 pixels exceed the limit and 612 pixels have
different accepted counts. Maximum error at equal counts is 0.05039703846.
The worst pixel is (983,428), `ViewLayer.Noisy Image.R`, with 368 versus 336
samples. Denoised native-repeat maximum is 2.608245850 in a bright HDR region.
Both process/resource checks passed; this does not establish beauty consistency.
Evidence: `original-settings-capture-check/CUDA/ordinary-repeat-diagnostic.json`.
The capture-only run was stopped early as described below.

A separate host-only scheduling experiment was built at
`builds/blender/install-landscape-single-sample-diagnostic`, executable SHA256
`40cbee96442f0cacd87207f70fe43b7814569f37f08998947d5c9dc15f30af55`.
Its sole temporary host change limits updates to one camera sample per pixel,
with GPU parallelism across pixels retained. Scene settings remain original,
but renderer scheduling is experimental: these runs are not production
qualification. The Blender source overlay was restored byte-for-byte, and all
291 GPU source files match the efficient renderer. No experiment source change
was added to the tracked production renderer. Build evidence is
`single-sample-diagnostic/build.json`; exact experimental source is preserved
alongside it. Two full-settings ordinary controls completed sequentially
under `single-sample-diagnostic/CUDA`. Their comparison verifies actual batch
cadence in render logs before interpreting the experiment.

The host-only build overlapped the early capture diagnostic, using two build
workers. No additional GPU render or GPU kernel compilation ran concurrently.
Capture timing from this run must not be presented as an isolated performance
benchmark.

The experimental host scheduler passed a 33x17 / four-sample CPU smoke with
GPU denoising disabled. Actual log cadence is exactly `[1,1,1,1]`; ordinary
beauty was saved and the executable matches the experiment manifest. Evidence:
`single-sample-diagnostic/cpu-cadence-smoke.json`. This proves the host update
limit is active, not that the full CUDA beauty repeatability issue is solved.

The default-scheduler capture-only diagnostic was deliberately stopped at
1332.875 seconds after the ordinary-repeat raw failure made its combined beauty
gate impossible to pass. Its measurement records exit code 4294967295, and it
produced neither a completed render report nor a deep EXR. The partial GPU
memory peak excludes final denoising and cannot qualify the resource gate.
The preserved complete deep EXR from the earlier original-settings run is
unchanged. Evidence: `original-settings-capture-check/CUDA/capture-stop-reason.json`.

The queued scheduling experiment correctly refused to proceed from that failed
capture measurement. After verifying teardown, its two ordinary controls were
started directly under `single-sample-diagnostic/CUDA`. Full production capture,
export, independent alpha/resource validation and connected Gaffer review remain
required after the beauty issue is resolved; this early stop does not reduce
release scope.

The first full-settings experimental ordinary render completed in 762.157
seconds, with all 1024 logged updates containing one sample. Its execution and
resource checks passed (device-wide peak 5339 MiB, no monitor errors).
Compared with the 401.000-second default-scheduler control, this single-run
comparison is about 1.90 times slower. The independent experimental repeat
completed in 756.813 seconds. One-sample updates are a
diagnostic, not an accepted production performance solution. Device-wide
memory values also include other applications and do not establish a memory
improvement from this scheduling change.

Both experimental controls passed the unchanged raw gate: maximum difference
7.62939453125e-6, zero failing pixels, and zero accepted-population differences.
Default scheduling produced 0.1558516026 maximum raw error, 986 failing pixels,
and 612 population differences. Both experimental logs contain exactly 1024
one-sample updates. Denoised native-repeat maximum is 0.007093787193. Two-run
median wall time increased from 401.141 to 759.485 seconds (1.8933 times).
Evidence: `single-sample-diagnostic/CUDA/ordinary-repeat-diagnostic.json` and
`single-sample-diagnostic/comparison.json`. These ordinary-only results do not
qualify deep alpha, publication, or the eventual production fix.

A proposed production fix retains normal batching and freezes the optical-depth
sum/count in an internal two-component pass at the existing power-of-two
volume-guiding updates. Sampling reads that snapshot instead of live
accumulators. It uses shared CPU/GPU film and kernel conventions, adds no
per-thread allocation, and does not change scene settings or error gates.
The implementation is under build/validation; full-scene repeatability and
performance are not yet established for this fix.

The frozen-majorant renderer is installed separately at
`builds/blender/install-landscape-frozen-majorant`, executable SHA256
`0de35a05eff64b51e72b86b929a4547a50e0d38985a1cdc5226b55e427edc0aa`.
All 14 CPU cases, four CUDA cases, and nine standalone tests passed (CTest
4.26 seconds). The new kernel was compiled normally from the changed GPU
sources; the older source override/cache was not used. Evidence:
`frozen-majorant-final-CPU/report.json`, `frozen-majorant-final-CUDA/report.json`,
and `compact-shader/frozen-majorant-ctest.log`.
Fresh original-settings ordinary controls with normal batching are running
under `frozen-majorant-original-controls/CUDA`. These full-scene results remain
unproven. The internal snapshot uses two floats per pixel (4,700,000 bytes per
render buffer at 1175x500), without changing the 1 GiB deep CPU budget or
allocating memory from GPU threads.

The full original-settings frozen-majorant ordinary controls completed and
passed the unchanged raw and resource gates. Maximum raw repeat difference is
3.0517578125e-5 against the 1.220703125e-4 limit, with zero failing pixels and
zero accepted-population differences. Wall times are 397.312 and 401.270
seconds (median 399.291 seconds), close to the earlier normal-batching median
401.141 seconds; this pair does not establish a general performance improvement.
Device-wide peaks are 5823 and 5299 MiB, both below 8192 MiB, with no monitor
errors. GPU OIDN native-repeat maximum is 2.609588623; it remains the measured
envelope for the paired capture comparison, not evidence of deterministic
denoising. Evidence: `frozen-majorant-original-controls/CUDA/ordinary-repeat-diagnostic.json`
and both `measurement.json` files. The full-settings capture-only comparison
is now running with the same executable and 1 GiB deep CPU budget. No deep
alpha/publication qualification follows from these ordinary-only results.

The capture comparison uses `run-frozen-majorant-capture-check.ps1`. The queued
`run-frozen-majorant-production.ps1` waits for that capture process and requires
the completed `capture-beauty-diagnostic.json` to pass with the same executable
fingerprint before starting a fresh original-settings render and bounded parallel
export in `frozen-majorant-production/CUDA/deep`. It retains 1024 maximum samples,
50 percent of 2350x1000, native CUDA/GPU OIDN, 24 configured threads, 8192 events
and the 1024 MiB deep CPU budget. It then requires the resource gate and the
existing accepted-camera/beauty/Gaffer validator to pass before preparing
`landscape-frozen-majorant-deep-alpha.gfr`. The final report records the CUDA
kernel identified in the actual production log, separate total-process memory
and deep-budget scope, and pending human review. No completed production result
or approval is claimed for this queued run.

## Full-settings frozen-majorant capture comparison passed

The capture-only comparison completed with exit code zero on 2026-10-05 using
executable SHA256 `0de35a05eff64b51e72b86b929a4547a50e0d38985a1cdc5226b55e427edc0aa`.
At the original 1175x500 output and maximum 1024 adaptive samples, raw capture
on/off error was 3.0517578125e-5 against the unchanged 1.220703125e-4 limit.
Accepted sample counts matched at every pixel; no raw pixels failed. Denoised
on/off maximum was 1.7820587158203125, within the ordinary-repeat maximum
2.609588623046875 plus the unchanged allowance. Wall time was 13098.375 seconds;
device-wide GPU peak was 5381 MiB with no monitor errors. This proves the capture
beauty/resource diagnostic, not deep-alpha accuracy or publication.

Evidence: `frozen-majorant-original-controls/CUDA/capture-beauty-diagnostic.json`
and `capture/measurement.json`. The guarded queue started the fresh full
production render in `frozen-majorant-production/CUDA/deep`, retaining the same
scene, executable, sampling, CUDA/GPU OIDN settings and 1024 MiB deep CPU budget.
Its parallel export, alpha/beauty/resource gates and connected Gaffer review
remain pending. No full-scene speedup or human approval is claimed.

## Hardware-budget native-repeat investigation

The 7ab hardware-budget executable passed all 14 CPU and four CUDA fixture
cases. Its full original-settings ordinary controls completed, but one pixel
(1152, 422) exceeded the unchanged raw tolerance: 0.0035251379 versus
0.0001220703125. A separate sample-count diagnostic reproduced that same
failure, with 448 versus 464 samples at that pixel. Exactly one pixel changed
accepted population; all matching-population pixels remained within tolerance
(maximum error 3.0517578125e-5). Deep was disabled in both pairs.

Evidence: `hardware-budget-original-controls/CUDA/ordinary-repeat-diagnostic.json`
and `hardware-budget-sample-count-diagnostic/CUDA/ordinary-repeat-diagnostic.json`
under `builds/validation/landscape-cloud`. No tolerance or scene setting changed.
The checker now permits an explicitly absent optional sample-count pass while
retaining all raw/resource gates. The production queue remains stopped before
its deep render; `--resume` reuses verified completed controls and matrices,
and still requires the ordinary gate to pass.

An 11x11 border diagnostic around the failed location passed with matching
populations and raw error 8.9406967e-8. Cropping changes neighborhood/guiding
behavior; this does not qualify or explain the full-frame result. Evidence:
`hardware-border-diagnostic/CUDA/comparison.json`. To inspect actual full-frame
convergence decisions, `diagnose_adaptive_convergence.py` runs two ordinary
renders with a separate, hashed CUDA source override that prints convergence
values at the failed pixel and its filter neighbors. Its source and manifest
are in `adaptive-convergence-kernel-diagnostic`. This instrumented override is
strictly diagnostic and must never be used as production qualification.

## Count-matched beauty qualification method

The instrumented ordinary pair reproduces the adaptive population difference.
At the failed pixel, convergence error at 448 samples is approximately 0.114975
against 0.15 in both traces: its own decision is converged in both. Native
neighborhood filtering can extend sampling; the exact numerical source of the
cross-run stopping difference is not established by this local trace. The CUDA
instrumentation and its timings are diagnostic evidence, not release evidence.

For the production comparison, every deep noisy pixel is compared against all
ordinary reference pixels with the same accepted sample count. The absolute
raw tolerance remains maximum_samples * 2^-23 (0.0001220703125 at 1024 samples).
Any count absent from both references fails. If both references have the same
count, both comparisons must pass, and their ordinary repeat difference must
also pass. No pixel/channel is excluded and no larger raw-error envelope is used.
Global unmatched-population counts and count-matched maximum errors are reported,
alongside the original unconditioned errors. The old ordinary repeat failure is
retained as failed evidence; the new population-reference-pair result has its
own field rather than rewriting that failure as a pass.

This changes the validation comparison method, not renderer behavior, alpha
accuracy thresholds, scene appearance/settings or the denoised-repeat envelope.
The helper's runnable negative checks reject unknown counts, altered values and
an inconsistent pair at equal counts. Before the full run, the queue requires a
47x20/max16 native CUDA deep/ordinary pair to exercise the actual EXR count reads,
accepted-camera oracle, beauty comparator and Gaffer depth cuts. This small
check cannot qualify production scale. The full run uses original settings,
24 export workers, 8192 MiB deep memory and no diagnostic CUDA source override.

Timing note: `capture_readback_seconds` starts before the deep GPU kernel is
enqueued and ends after synchronization and populated-plane copies. It includes
GPU capture execution and queue waits, including prior queued work, not just
PCIe transfer time. `spill_seconds` measures host record packing and spill.
Neither field alone identifies a transfer bottleneck; final render and export
wall times are reported separately.

### Interrupted original-settings hardware-budget run (2026-10-06)

Capture completed in 13278.764 seconds and export selected 24 workers. The
machine rebooted before atomic EXR publication; the renderer and queue are gone.
System events record an unexpected reboot at 14:42 Sydney time and an
update-related restart at 14:51. The cause of the unexpected reboot is unproven.
The partial EXR, diagnostic CSV, logs and `interruption-evidence.json` remain in
`builds/validation/landscape-cloud/hardware-budget-production/CUDA/deep`.
No final output or qualification report exists. The current renderer cannot
resume from these remaining artifacts; they must not be presented as a completed
production result. No rerender or restart was performed.
