# Volume reconstruction and homogeneous capture

Current release gates: [M8](../../DEEP_MILESTONES.md).
Native heterogeneous grids: [VDB contract](NATIVE_VDB_PLAN.md).
CPU/CUDA homogeneous qualification: [device evidence](CUDA_VOLUME_VALIDATION.md).

## Contract

Each completed camera sample carries surface-opacity steps and optical-depth
intervals. Depth is axial; optical depth uses physical ray length. Overlapping
media add extinction; misses contribute to the sample denominator. Reconstruction
averages transmittance before fitting ordered exponential intervals. It never
averages local alpha. See `volume.h` for current numerical budgets.

Fitting splits at medium/surface boundaries and bounds the mixture's curvature.
FLOAT export checks both sides of boundaries and interior extrema. Thin intervals
may collapse only if the whole-curve error remains bounded. Invalid data,
exhausted precision/capacity and unrepresentable opacity fail explicitly.

Renderer output reconstructs one pixel at a time and writes bounded scanlines.
The separate whole-image reference API has no renderer memory-budget guarantee.
See [storage/publication](PRODUCTION_VALIDATION.md) for atomic replacement.

Homogeneous capture supports constant scalar absorption or Henyey-Greenstein
scattering extinction on closed, convex,
outward-wound meshes with one pure volume material; ordinary supported surfaces
may cross them. It uses native intersections and extinction shading, without
changing beauty state or RNG. Static perspective/pinhole, positive near clip,
fixed samples and native SVM are required. CPU/CUDA share the capture helper.

## Reference evidence

`volume_test.cpp` covers Beer-Lambert attenuation, off-axis/inside rays,
overlapping media, partial coverage, surfaces, random ledgers, invalid inputs,
capacity limits and failed-publication preservation. Current tests also cover
indexed lookup and bounded streaming reduction.

Historical Gaffer analytic fixtures pass at 4.741e-7 maximum alpha error;
the 160x120 two-sphere oracle passes at 1.522e-7 (limit 1e-6). These are analytic
EXRs, not native renderer/VDB evidence. Artifacts and review:
`builds/build-m6/m8-volume-reference/m8_volume_reference.gfr`.

```powershell
builds/build-m6/bin/Release/cycles_deep_volume_test.exe builds/build-m6/m8-volume-reference
builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_volume_gaffer.py builds/build-m6/m8-volume-reference
```

DeepToPointCloud shows EXR interval fronts/backs; points are not scattering
particles. Independent-layer coverage correlations are not retained by scalar
alpha. Small homogeneous/analytic tests do not establish general production
volume readiness; remaining qualification is tracked in release status.
