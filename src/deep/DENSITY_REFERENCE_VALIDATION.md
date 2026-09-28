# Linear-density reference

`integrate_linear_density()` converts supplied piecewise-linear scalar extinction
to homogeneous optical-depth intervals. This is a host math reference; native
trilinear VDB rays need cubic cell integration, described in [NATIVE_VDB_PLAN.md](NATIVE_VDB_PLAN.md).

For physical length L, endpoint extinction difference d and N equal pieces,
`abs(d)*L/(8*N*N)` bounds optical-depth interpolation error and therefore
absolute transmittance error for nonnegative extinction. Segments share the
budget, including overlap. Physical-length-per-depth handles off-axis rays.
Zero density emits nothing. Capacity is counted before allocation; invalid data,
overflow and exhausted precision fail. Double arithmetic is tested on bounded
fixtures, not certified for arbitrary dynamic ranges.

## Evidence

`density_test.cpp` checks rising/falling/constant/empty profiles, off-axis scaling,
overlap, a delimited thin feature, clipping, random ramps and failures. Probes
include boundaries, midpoints and dense depths; total transmittance agrees at
endpoints to 1e-12. Maximum integration error is 1e-7. Partial-coverage mixture
checks pass within 2e-7.

Gaffer checks 768 pixels at 13 cuts against analytic integrals: maximum alpha
error 1.641e-7 (limit 1e-6). The 32x24 sphere contains 31,332 intervals. This is
an analytic EXR, not a native VDB render. Artifacts:
`builds/validation/heterogeneous-reference/`; review `m8d1_density_reference.gfr`.
`DensityDepthCut` controls the slice; `DensityIntervalPoints` shows EXR boundaries.

```powershell
builds/build-m6/bin/Release/cycles_deep_density_test.exe builds/validation/heterogeneous-reference
builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_density_gaffer.py builds/validation/heterogeneous-reference
```
