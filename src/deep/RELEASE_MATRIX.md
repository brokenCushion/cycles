# Deep alpha qualification matrix

Current scope follows the [optimization plan](../../DEEP_OPTIMIZATION_PLAN.md)
and [status](../../DEEP_IMPLEMENTATION_STATUS.md). Phases 0-6b are accepted.
Qualification covers the named fixtures; arbitrary feature combinations and the
full-resolution landscape are not implied. Phase 9 remains unlaunched.

| Feature | Qualified scope |
| --- | --- |
| Devices | Single CPU or CUDA, background render; OptiX/OSL are Phase 8. |
| Shading | Native SVM scalar camera opacity/extinction, within `session/deep.cpp` preflight. |
| Geometry | Polygon surfaces/rigid instances; static homogeneous boundaries and native scalar NanoVDB density with linear interpolation. |
| Camera | Surface-only perspective/orthographic, DOF and rigid motion; volumes require static mono perspective without DOF/motion. |
| Samples | Fixed/native adaptive accepted populations; `--deep-samples 0` uses all, positive N retains the first N without changing beauty. |
| Denoising | Native OIDN, including GPU OIDN in supplied landscape fixtures; deep remains separate from beauty. |
| IDs | Optional UINT `id`, raw name hashes and manifest; numeric error required. Strict + IDs fails preflight. |
| Holdout | Object flags and native Holdout closure preserve camera opacity; checked holdout manifest subset. Shadow catchers/caustics remain rejected. |
| Surface depth merging | Same object only, checked relative depth span; numeric default `--deep-z-tolerance 1e-4`, zero disables it, strict forces zero. |
| Output | FLOAT Z/ZBack/A, positive axial depth in scene units. Deep RGB, subpixel masks and other backends are later work. |

## Numerical and storage contract

Strict reproduces legacy payload and deterministic headers under Section 2 of
the plan. Numeric error splits the user setting across device/host/publication
allowances; their sum bounds absolute transmittance error. Validators read the
effective error and sample setting from EXR headers. ID publication sums measured
per-object errors; independent exact-cubic/camera/depth-cut checks remain mandatory.
Positive z tolerance approximates the interiors of merged surface depth bands;
combined alpha and curves outside those bands retain the error checks. Use zero
for the whole-curve depth contract. Strict always uses zero.
Completed misses count in normalization; incomplete/overflowing work fails explicitly.

Device buffers are host-allocated and bounded. Spill and parallel host fitting
are separate from beauty. No GPU allocation/files/STL/exceptions. Cancellation,
capacity and I/O failures preserve the previous EXR through atomic local publication;
network/power-loss/frame-transaction durability is not qualified. Scalar alpha
does not encode arbitrary subpixel correlations or recover colour from beauty.

CPU beauty remains exact. CUDA beauty policy and its GPU-only phase scope are in
Section 2; source hashes and 75 kernel resource records provide the host-only proof.

## Reproduction

Use [the single regression command](README.md#regression-command): nine CTests,
CPU/CUDA matrices, boundaries, oracles, CPU beauty, resource/source proof and
81/81 strict + 30/30 numeric identity. Gaffer is optional for interactive review.
Large validation output/TEMP lives on D:, with only small reports under `builds/`.
Verified historical sample ZIPs stream without disk expansion. Historical M8
reports/support details remain in Git history at `9cad1e861`; current results
are in Section 6 of the plan.
