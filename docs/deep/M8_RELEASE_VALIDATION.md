# Historical M8 deep-alpha qualification

This records the 2026-09-29 release and its executable, not qualification of the
current production build. The expanded landscape qualification subsequently
passed; see the [accepted production evidence](evidence/README.md).

All technical release gates pass, including production-scale repeats, the full
Blender asset regression and the default-colour native matrix. The user approved
the final Gaffer scene on 2026-09-29: "the gaffer scene for m8 looks good."
**M8 is complete within the qualified CPU/CUDA support matrix.**
The [support matrix](SUPPORT.md) defines the scope; deep RGB and
additional device backends belong to M9.

## Final native VDB matrix

The installed custom Blender executable has SHA256
`b281e5a8a8da4a7a1e1b146eee0e6a521ed9511199259666602d748b693031af`.
This identifies the historical qualified executable. On 2026-10-03 the local
`builds/blender/install` directory was refreshed by the development build's
configured INSTALL target and no longer contains that executable. Current
landscape tests use `builds/blender/install-m9`; these historical reports remain
unchanged and do not qualify the replacement binary.
Both CPU and CUDA pass 11 accepted cases and nine expected rejection cases at
96x96/four camera samples. Inputs and executable identity are recorded in the
reports under
`builds/validation/m8-release-qualified/native-default-colour/native-{CPU,CUDA}/`.
The renderer uses Blender's bundled colour configuration; the previous matrix
with Gaffer's inherited OCIO override is retained separately as earlier evidence.

| Check | CPU | CUDA | Limit |
| --- | --- | --- | --- |
| Accepted-camera transmittance error | 2.52685e-7 | 2.52684e-7 | 1e-6 |
| Gaffer flattened depth-cut error | 6.75701e-7 | 4.65852e-7 | 1e-6 |
| Named single-grid overlap product error | 1.25181e-7 | 1.25493e-7 | 1e-6 |
| Equivalent absorption/scattering EXR | Byte-identical | Byte-identical | Exact |

Accepted cases cover mixed scalar-transparent surfaces and VDB, two overlapping
grids, each isolated grid, a camera inside the grid, far clipping inside the grid,
scattering extinction, rotation/nonuniform positive scale, Gaussian and
Blackman-Harris filters, and native constant-folded zero density. Zero density
requires zero deep samples at every pixel and a valid zero-point cloud.

Adaptive sampling, DOF, motion, orthographic volume cameras, cubic interpolation,
half precision, coloured extinction, nonlinear density products and reflection
reject explicitly. Every rejection preserves a sentinel previous deep frame and
leaves no publication staging file. Surface-only adaptive/DOF/rigid motion have
their own qualification and remain supported under the published matrix.

The overlap product uses independent single-grid renders with matching object
names. It shares the single-grid capture implementation. The separate physical
grid oracle compares against OpenVDB integration: 128 CPU/CUDA rays, 89,102 cells,
maximum transmittance error 2.67564e-9 on both devices, including overflow checks.
Evidence: `m8-release-final/grid-oracle-{cpu,cuda}.log` under `builds/validation/`.

## Issues found and corrected during release qualification

- Clipped voxel starts could round into the adjacent cell or leave its outgoing
  crossing equal to the start. Bounded plane-time corrections preserve short
  represented intervals and avoid zero-length traversal. CPU/CUDA regressions
  cover 24 starts immediately before/at/after crossings in both directions.
- Long curves could pass stored-alpha accuracy while Gaffer accumulated thousands
  of small alphas in FLOAT beyond the existing cut limit. Adjacent volume intervals
  now coalesce within 2.5e-7 of the existing export allowance; surface steps, gaps
  and endpoint transmittance remain. Complete original-curve checks still enforce
  the unchanged 1e-6 total budget. The CPU overlap output falls from 4,351,561 to
  1,594,908 stored intervals, with accepted-camera error 2.12970e-7 and Gaffer cut
  error 6.75701e-7. Pixel export scratch reserves 240 bytes per fitted interval.
- Native shader simplification folds density multiplied by zero to a constant.
  The supported empty medium now skips density lookup, even when optimization
  removes the density attribute. Positive constant density is still unsupported
  for a native grid material.
- The storage reader suite now uses the organized `builds/build-m3` primitive
  fixture path, exercising actual 640x480 geometry instead of skipping it.
- The Blender overlay generator skips tracked files deleted in the worktree.
  Its anchors validate on an isolated clean checkout at the pinned Blender
  revision; the active dirty overlay is preserved.
- Blender child processes launched by Gaffer's Python environment previously
  inherited Gaffer's ACES `OCIO` override. That changes native scene colours and
  can change adaptive camera populations. Qualification now uses Blender's
  bundled colour configuration for renderer children while retaining Gaffer's
  configuration for reader checks. The inherited-config full-scene run is kept
  as failed legacy/beauty evidence; its own accepted-camera deep curve still
  passes. A direct native-config ordinary beauty repeat matches historical
  beauty exactly. The corrected final run retains strict gates and passes:
  664x625/128 samples, byte-identical 244,457,944-byte deep EXR, exactly equal
  fresh deep-on/off beauty, 21,076 boundary checks at 81 diagnostic pixels,
  maximum curve error 3.10044e-9, zero Gaffer cut error and a valid million-point
  preview. Evidence: `m8-release-qualified/surface-asset-native/`.

Final Blender CUDA resources match the previous qualified deep-enabled kernel:
168 registers, 16,784 local bytes, zero shared bytes. All 75 common beauty kernels
retain their resource attributes. This comparison uses two deep-enabled cubins;
it is not a fresh disabled-feature overhead claim. The driver's suggested block
size is not a measured occupancy result.

## Final release-scale scattering

The final supplied-grid scattering fixture is 1024x768/four fixed samples.
Its source SHA256 is
`efebbc590e803b7fd7ac04b6a03c4f5b4f92e6b9080abbf67260fe4a4b45fd33`.
CPU and CUDA repeats pass all resource, whole-curve, Gaffer and beauty gates:

| Run | Wall seconds | EXR bytes | Host peak bytes | Curve error | Gaffer cut error |
| --- | --- | --- | --- | --- | --- |
| CPU 1 | 1189.313 | 1,020,940,868 | 864,317,440 | 2.49442e-7 | 2.04364e-7 |
| CPU 2 | 1211.579 | 1,020,940,868 | 862,797,824 | 2.49442e-7 | 2.04364e-7 |
| CUDA 1 | 1549.500 | 1,020,932,399 | 1,053,687,808 | 2.49700e-7 | 1.90898e-7 |
| CUDA 2 | 1579.156 | 1,020,932,399 | 1,061,670,912 | 2.49700e-7 | 1.90898e-7 |

CPU deep files are byte-identical (SHA256
`889b7cf0952b9771eb1bfcaa94e489d8165577e04ebff53516b888f5e8c0a1c2`).
CPU retains exact deep-on/off beauty. CUDA beauty maximum difference is
1.19209e-7, equal to its ordinary repeat and below the unchanged 4.76837e-7
regression threshold. All four outputs have valid million-point previews.
CUDA repeats are byte-identical (SHA256
`561bfb8b0f2ebe31f406c5cc6b8585aece95c33b75cbf2112394aa1ee1520906`).
Device-wide CUDA peaks are 5,260 / 4,675 MiB, including other applications.
All runs meet the 2,400 s, 4,000,000,000-byte EXR, 4 GiB host working-set and
8 GiB CUDA device-wide memory targets. The sampled CPU/CUDA curve comparison
passes 40,034 boundary/midpoint comparisons at 81 pixels, maximum error
5.76589e-7 against the unchanged 1e-6 gate. CUDA capture retains 3,145,728
camera records with zero skips. Its 670.103 GB of readback per render remains
a substantial cost; meeting these fixture targets does not imply general speed
parity with ordinary beauty rendering. Evidence:
`builds/validation/m8-release-scale/report.json`.

## Reproduction

The final standalone installed executable has SHA256
`8ea0c9c2d45c083df4a6e4db6309837c3a1b5bc5241765c7886575a677520bb4`.
All nine CTests and all ten fresh renderer/Gaffer suite groups pass.
CPU/CUDA homogeneous volume suites each cover 28 accepted scenes and 19
rejections. Surface transparency, adaptive sampling, DOF and rigid motion pass
their separate suites. The storage suite renders the actual 640x480 primitives.
Its dense surface fixture reduces 2,214,155 samples to 350,403 within a
0.000998797 whole-curve error and 8.316e-10 endpoint error.

Publication/lifecycle checks and the CUDA host-driver suite pass cancellation,
callback failure, injected device failure, reset, disable/re-enable and capacity
changes. The core tests additionally exercise allocation/source-I/O failure,
scanline exhaustion, failing output streams and a real Windows locked destination.
Previous completed EXRs survive and temporary publication output is cleaned.
Evidence: `m8-release-qualified/{ctest.log,All.json,storage/}`.

A shared-runtime source audit compares 822 standalone/Blender files. It excludes
standalone app entry points and unlinked tests; one explicitly checked BVH
comment-only difference has no executable change. There are no runtime code
differences. The clean-checkout overlay anchor check also passes.

Generate fresh VDB cases with `tools/create_vdb_deep_cases.py`, then run
`tools/qualify_native_vdb.py` in Gaffer's Python environment for CPU and CUDA.
CUDA uses the configured toolchain wrapper. Keep fixture source assets unchanged.

```powershell
& tools/qualify_deep_release.ps1 -Group All -Output builds/validation/m8-release-new
& tools/qualify_deep_scale.ps1 -Scene builds/validation/m8-release-scale-fixtures/scattering/scene.blend -Output builds/validation/m8-release-scale-new
& builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python tools/qualify_blender_surface.py builds/blender/install/blender.exe test-assets/blender/blender-3.5-splash.blend builds/validation/blender-deep/scene-compact-deep builds/validation/m8-surface-new
```

`tools/create_m8_release_review.py` bundles only passing render graphs into a
Gaffer script. Each box exposes an actual DeepToPointCloud scene output and its
linked depth-cut controls. It reloads the saved graph and checks valid point
counts. The cloud displays stored depth samples; no mesh conversion is involved.

The final saved and reload-checked graph is
`builds/validation/m8-release-qualified/m8_final_release_review.gfr`.
All 15 boxes validate, with 12,775,584 displayed points in total (including two
valid empty-medium boxes). It includes the CPU/CUDA production-scale renders,
the final supported native matrix cases and the full Blender asset. A fresh
Gaffer session was opened for user review. The automated reload check verifies
graph evaluation; final visual approval was supplied separately by the user on
2026-09-29. Implementation and qualification were committed in `8e03847da`.

[Release status](evidence/README.md) |
[Measurements](evidence/README.md) |
[Milestones](ARCHIVED_REPORTS.md)
