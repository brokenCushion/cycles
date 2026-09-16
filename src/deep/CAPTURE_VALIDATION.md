# M3 CPU opaque capture

This opt-in standalone prototype captures every accepted camera sample before
surface shading. It executes the scheduled CPU intersection once and resumes
normal rendering from the next kernel. No additional beauty random values are
drawn. A miss uses the explicit `PRIM_NONE` marker; the other intersection fields
are not defined on misses.

Hit position is `ray.P + ray.D * isect.t`. Exported depth is that position's
positive camera-space Z, including the shifted near-clip ray origin. Captured
depths are already FLOAT. Small floating-point differences on coplanar geometry
are preserved rather than merged with an arbitrary tolerance.

Each pixel has exactly N uniquely identified, unit-weight sample records. Zero
depth completes a miss. The M1 reconstruction reference combines opaque hits and
misses into the pixel transmittance curve, including conditional alpha at later
depths: half coverage at Z=2 and half at Z=8 emits alpha 0.5 then 1, not 0.5 twice.
Deep output is flipped to the same file orientation as beauty output.

## Supported scene contract

- CPU background render, 1..4096 fixed samples, full frame/resolution, box filter
  width 1, static mono perspective pinhole camera.
- Static polygon meshes with constant diffuse or emission shaders, and constant
  background lighting. Transparent background is allowed and geometry misses
  always produce empty deep samples.
- Pre-render rejection for other material nodes (including transparency/OSL),
  volume/displacement outputs, subdivision, non-mesh geometry/lights, camera
  borders, adaptive sampling, sample subsets, tiling, motion blur, DOF, guiding,
  denoising, custom pixel jitter, holdout, shadow catcher, caustics, and baking.
- Runtime checks reject unexpected camera state/primitive, duplicate samples,
  invalid depth, incomplete capture, cancellation and renderer errors.

Storage is a full-frame float array. `--deep-memory-mb` defaults to 64 MiB and
limits **raw capture only**; it accepts 1..1024 MiB. Checked dimensions prevent
overflow before allocation. Reconstruction/output/OpenEXR staging consumes
additional memory. A mutex protects sample writes; performance optimization and
a production total-memory bound are deferred.

## Reproduce

Configure the existing CPU build with `-DWITH_CYCLES_DEEP_OPAQUE=ON` and
`-DWITH_CYCLES_DEEP_TESTS=ON`. Build `cycles`, `cycles_deep_capture_test`,
`cycles_deep_reference_test` and `cycles_deep_exr_test`, then run CTest.
The switch defaults OFF; all renderer hooks and the extra CPU kernel dispatch
entry are compiled out when disabled.

The local binary is staged beside the existing runtime DLLs as
`install/cycles-m3.exe`; `install/cycles.exe` remains the original baseline.

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd python src/deep/validate_capture_gaffer.py install/cycles-m3.exe build-m3/acceptance
```

The script generates small XML scenes, renders deep off/on with the same seed,
compares all RGBA pixels, and checks every deep transmittance discontinuity
against independent raw-hit counts. Misses remain in the denominator. It checks
plane depths to 2e-5 scene units and transmittance/flattened alpha to 1e-6.
Gaffer DeepSlice is tested at four depth cuts. It saves `report.json`, render
logs, raw CSVs, EXRs and `capture_validation.gfr` under the output directory.

Fixtures cover a full off-axis plane, diagonal edge, near/far coverage, small
object, all misses, translated camera with nearclip=1, and diffuse shading.
A one-worker repeat must match four-worker raw records exactly. Invalid-scene
tests require nonzero exit, a diagnostic, and preservation of a pre-existing
deep output. Unit tests cover lifecycle failures, concurrent writes, bounds,
invalid depth and budget overflow.

## Verified results (2026-09-16)

Both feature-enabled and feature-disabled Release builds succeeded. The disabled
CLI has no deep options. The seven scenes above pass in Gaffer 1.7.2.0 at 32x24,
32 samples, seed 123. All RGBA pixels are exactly equal between runtime deep
off/on. The maximum axial-depth error is `9.5367431640625e-7` scene units,
complete transmittance-curve error `2.7939677238464355e-8`, and flattened-alpha
error `5.960464477539063e-8`. Four Gaffer depth cuts pass the 1e-6 gate.

The near/far fixture has 27 pixels with mixed depths; the tiny object has six
partially covered pixels. The empty scene emits zero samples. Raw records match
exactly between one and four workers. Fourteen invalid-setting/path/write cases
pass, including preserving pre-existing sidecars on pre-render rejection.
The four CTests cover capture, reconstruction, writer and executable version.

Final local artifacts are in `build-m3/acceptance`. This is evidence for the
restricted M3 contract, not for arbitrary materials or scenes.

Example after running the suite:

```powershell
.\install\cycles-m3.exe --background --device CPU --samples 32 --width 32 --height 24 --deep-output build-m3/example.deep.exr build-m3/acceptance/near_far.xml
```

The initial M3 smoke test was not a correctness gate: it used ray distance,
an undefined miss field and unconditional depth-bin alpha. Those errors were
corrected before the acceptance tests described here. Existing smoke-test files
outside `acceptance` should not be used as validation evidence.

## Limits

No transparent chains, volumes, deep RGB, GPU, crop/view/frame controls, production
sample reduction or atomic beauty/deep publication. Frame/view metadata uses the
writer defaults (frame 1/main); Gaffer exposes the single view as `default`.
An I/O failure can leave a partial sidecar, and failure writing optional raw CSV
after successful EXR output does not roll back the EXR. Raw CSV is a debugging
oracle, not a stable interchange format. Reported tiny-scene wall times include
startup and are not an overhead benchmark. The fork has not been integrated into
Gaffer's bundled Cycles; Gaffer is the independent EXR reader/compositor here.
