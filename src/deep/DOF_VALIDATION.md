# M7b — Static perspective depth of field

The capture adapters already read Cycles' accepted camera ray after lens
sampling. The CPU traversal copies that state; CUDA reads its ray and restores
the path fields it temporarily modifies. The static perspective camera returns
unit throughput, so the existing unit-weight population normalization applies.
Depth remains the hit point's camera-axis Z, not distance from the lens origin.
No separate lens RNG, central-ray substitute or beauty stopping override is used.

The standalone allowlist now accepts finite nonnegative aperture size, positive
finite focal distance and aperture ratio, and finite blade rotation. Static
mono perspective, box filter width 1 and the surface/material restrictions remain.
This gate tested static cameras/objects. Rigid motion is subsequently qualified
by [M7c](MOTION_VALIDATION.md); time-varying perspective remains rejected.

Camera XML scalar floats are checked before property assignment and camera
update. This rejects NaN/Infinity even when fast-math property comparisons would
otherwise hide the invalid value. The later deep camera checks also use Cycles'
fast-math-safe finite predicate. This input check applies to standalone camera
XML with and without deep output; finite camera settings retain their behavior.

## Acceptance

Run `validate_dof_gaffer.py EXE OUTPUT_DIR` in Gaffer's Python with the local
MSVC/CUDA environment. It renders CPU/CUDA near/middle/far surfaces at three
focus distances, an in-focus and defocused silhouette against a pinhole
baseline, combined adaptive sampling/DOF, and a rotated six-blade anamorphic
aperture. CPU OSL uses the existing
restricted shader graph. Each exported curve is checked against the accepted
raw ledger at both sides of every event boundary. CPU/GPU sample identities,
chain lengths, local opacity and axial depth are compared independently.

The focused plane preserves every camera hit/miss decision relative to pinhole;
defocusing that plane changes 1,292 of 49,152 decisions. Invalid negative
aperture, zero focal distance and zero aperture ratio are rejected on both
CPU/CUDA while preserving an existing deep output. The final suite also rejects
NaN rotation/focus and infinite aperture/FOV, for fourteen CPU/CUDA rejection
cases total. All eight CPU/CUDA scene pairs and the CPU OSL case pass.

CPU deep-on/off beauty is checked exactly. For CUDA these 64-sample, unit-range
fixtures use an explicit FLOAT accumulation bound of `64 * 2^-23` (about
`7.63e-6`). Repeated ordinary CUDA renders provide a separate scheduling-noise
baseline. This bound does not imply arbitrary production beauty tolerance or
bitwise GPU determinism. The observed maximum deep-on/off difference and the
maximum ordinary-render repeat difference reached `5.7220458984375e-6` across
the qualification runs. CPU/CUDA chain populations agree in the tested cases.

The final build and all five CTest groups pass. Fixed pinhole regression scenes
also pass. Maximum raw-ledger reconstruction error remains below `3e-8`.

The acceptance graph `build-m6/m7-dof/m7_dof_review.gfr` contains all three focus
settings on both backends, with beauty readers, editable depth cuts and live
DeepToPointCloud nodes. Reports, input XML, raw CSV and EXRs are adjacent.

The larger `build-m6/m7-dof-primitives/m7_dof_primitives.gfr` review contains
the sphere/cube/cylinder scene at 640x480, 16 samples, aperture 0.35 and focus
distance 7.1 (middle cube). CPU/CUDA eight-depth-cut comparisons differ by at most
`5.960464477539063e-8` alpha, with no pixel exceeding `1e-6`. Both live point
clouds evaluate successfully at a one-million-point display cap. The graph was
saved, reloaded and opened in Gaffer. The 93.047/52.812-second CPU/CUDA timings
include process/export work and are not isolated performance benchmarks.

## Reviewing DOF as points

DeepToPointCloud projects pixel centres and axial Z through a central camera.
For DOF this is a display of the deep distribution, not an exact reconstruction
of the original lens-ray hit positions. Z/ZBack/A do not retain lens positions
or subpixel coordinates. Independent scalar deep layers also lose lens/subpixel
coverage correlations. No exact separate-layer bokeh recomposition is claimed.

```powershell
build-m6/run-cuda.cmd build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_dof_gaffer.py install-m6/cycles.exe build-m6/m7-dof
```

Rigid motion is covered by [M7c](MOTION_VALIDATION.md). M6's separate performance
qualification remains open; successful DOF tests do not close those measurements.
