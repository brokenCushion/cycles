# M8d1: known linear-density integration reference

This checkpoint qualifies a host reference for supplied piecewise-linear scalar
extinction profiles. It does **not** enable heterogeneous shaders or VDB capture
in Cycles. The existing CPU/CUDA homogeneous-volume allowlists are unchanged.

## Contract and limits

`integrate_linear_density()` in `volume.cpp` converts known linear profiles into
piecewise-homogeneous optical-depth intervals. Depth is axial; the supplied
physical-length-per-depth factor accounts for off-axis rays. Overlaps add
extinction. Zero-density input emits no records; constant density needs one.

For a segment of physical length L and endpoint extinction difference d, N equal
pieces bound optical-depth interpolation error by `abs(d)*L/(8*N*N)`.
Since `exp(-tau)` is 1-Lipschitz for nonnegative tau, this also bounds absolute
transmittance error. Each segment gets an equal share of the requested budget,
including overlapping segments. Completed pieces preserve trapezoidal integrals
in exact arithmetic. Double arithmetic is checked on bounded fixtures; this is
not interval-arithmetic certification for arbitrary dynamic ranges.

Record capacity is checked before allocating the output vector. Invalid input,
overflow, an unrepresentable budget, exhausted depth precision and insufficient
capacity throw. This host reference allocates a vector; it is not a GPU-thread
implementation or a new renderer storage path.

Error allocation is 1e-7 for integration, 1e-7 for reconstruction when needed,
and 8e-7 for FLOAT export, totaling 1e-6. The single-ray sphere fixture directly
converts nonoverlapping intervals and needs no reconstruction approximation.

## Evidence

- `cycles_deep_density_test`: rising/falling ramps, constant/empty profiles,
  off-axis scaling, overlap, a narrow explicitly delimited feature, a clipped
  ramp, 20 seeded random ramps, invalid input and capacity failures.
- Probes include piece boundaries, piece midpoints and a dense depth grid;
  endpoint total-transmittance agreement is checked to 1e-12.
- A half-covered overlapping profile verifies averaging transmittance after
  integration and reconstruction within 2e-7.
- Integration maximum checked error: 9.9999985070375885e-8.
- All eight CTest groups pass, including the existing volume, EXR, storage and
  output-driver tests. This reference change does not claim new device tests.
- Gaffer 1.7.2.0 checks all 768 pixels at 13 depth cuts against the analytic
  integral. Maximum alpha error: 1.6407879654956048e-7, below 1e-6.
- The 32x24 sphere EXR contains 31,332 intervals. At the saved depth cut 5.7,
  each live cloud contains 24,492 points. Orthographic projection, primitive
  variables, and save/reload evaluation are checked.

Artifacts: `builds/validation/heterogeneous-reference/`. Open
`m8d1_density_reference.gfr`, view `DensityIntervalPoints`, and adjust
`DensityDepthCut.farClip.value`. Blue/orange points represent EXR interval
fronts/backs. This is a low-resolution analytic fixture, not a rendered mesh,
VDB, or scattering-particle cloud.

Reproduce after building:

```powershell
& builds/build-m6/bin/Release/cycles_deep_density_test.exe builds/validation/heterogeneous-reference
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_density_gaffer.py builds/validation/heterogeneous-reference
ctest --test-dir builds/build-m6 -C Release --output-on-failure
```

## Next gate

Establish cell-aware integration and error bounds for the actual volume lookup.
Trilinear VDB interpolation along a general ray is generally cubic within each
cell, so endpoints alone do not define a sufficient linear profile. Randomized
beauty-ray density samples likewise cannot certify deep transmittance at every
depth. Thin features must be bounded or explicitly traversed, not inferred
absent from sparse samples. Then integrate with bounded device capture/storage,
qualify CPU/CUDA and fail-closed publication, and review the user's VDB. M8
remains open; RGB and scattering require separate contracts.
