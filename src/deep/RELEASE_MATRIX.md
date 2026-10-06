# M8 deep-alpha support matrix

This is the historical M8 support matrix, with technical qualification and user
Gaffer approval recorded on 2026-09-29. It does not qualify the current landscape
development executable or the expanded original-settings production test, which
remains open. See [current release status](../../DEEP_IMPLEMENTATION_STATUS.md)
and [landscape evidence](LANDSCAPE_COMPATIBILITY.md).

## Scene and device contract

| Feature | Surface-only capture | Capture with any volume |
| --- | --- | --- |
| Device | Single CPU or CUDA, background render | Single CPU or CUDA, background render |
| Shading | Native SVM; restricted CPU OSL fixtures | Native SVM only |
| Geometry | Polygon meshes, rigid instances | Static polygon surfaces; closed convex outward-wound homogeneous boundaries; native FLOAT VDB density |
| Camera | Mono perspective or orthographic | Mono static perspective, positive near clip |
| Samples | Fixed or native adaptive accepted camera samples | Fixed accepted camera samples |
| DOF | Accepted lens rays | Explicitly rejected |
| Motion | Rigid object/camera translation and rotation, uniform shutter | Explicitly rejected, including motion on surfaces in a volume scene |
| Filters | Box, Gaussian, Blackman-Harris; finite positive width | Same allowlist; release VDB workload uses box width 1 |
| Denoising | CPU at native resolution; CUDA rejected | Same host preflight; VDB qualification disables denoising |
| Transparency | Scalar native camera-alpha semantics | Same scalar surfaces, including surfaces inside media |
| Volume closures | Not enabled | One scalar absorption or Henyey-Greenstein scattering closure; no combined closure graphs |
| Native grid | Not enabled | FLOAT source, FULL precision, linear interpolation; `density` Fac times nonnegative finite constants |
| Static volume transforms | Not applicable | Finite nonsingular transforms with positive determinant; qualification includes rotation and nonuniform scale |
| Occlusion and overlap | Ordered camera surface events | Additive optical depth for overlapping media, surface opacity steps, near/far clipping and camera-inside cases |

The API accepts 1..4096 maximum samples. That is an input/capacity limit, not a
performance guarantee. Numerical fixtures exercise small images at multiple
sample counts. Production workload evidence is the supplied static VDB at
1024x768/four samples and the supplied surface scene at 664x625/128 samples.
Arbitrary feature combinations are not implied by individual tests.

The surface shader allowlist in `session/deep.cpp` includes diffuse, emission,
principled, glass, translucent, scalar transparent and mix closures, plus the
qualified texture/mapping/ramp/bump inputs. Unsupported nodes fail preflight.
Glass describes camera opacity, not refracted-path deep reconstruction. Background
shading is constant. Coloured transparency/extinction and holdout/shadow-catcher
semantics are not supported. Native grids require a pure volume material.
An identically zero density multiplier may be folded to a constant by native
shader optimization; it remains an empty medium and performs no density lookup.

Deforming geometry, animated grids, motion scale/reflection, animated FOV,
rolling shutter, stereo, camera borders, sample subsets, time limits, automatic
tiling, baking, guiding and unsupported procedural geometry fail explicitly.
The custom Blender host additionally requires one enabled view layer. OptiX,
HIP, Metal and oneAPI are outside the CPU/CUDA M8 qualification.

## Numerical and storage contract

Output contains FLOAT Z/ZBack/A; depths are positive camera-axis scene units.
Beauty and diagnostic CSV are separate outputs. Completed misses count in the
sample denominator. Reconstruction averages transmittance over accepted rays.
Scattering contributes its scalar extinction; scattered colour remains M9.

The normal whole-curve transmittance error budget is 1e-6 against each device's
accepted samples. Optional surface reduction uses a 0.001 budget; native volume
streaming reduction stays within its existing 5e-8 reconstruction allocation.
Export can additionally coalesce adjacent intervals using 2.5e-7 of its existing
FLOAT export allowance. It preserves surface steps and gaps, then checks the
complete original curve again at boundaries and interior extrema. The overall
1e-6 budget is unchanged. This lowers storage and FLOAT consumer accumulation
error without promising exact arithmetic in every compositing application.
Sampling variance and native CPU/CUDA intersection differences are separate from
export error. The known moving-cube CPU/CUDA coverage difference remains documented
in [motion evidence](MOTION_VALIDATION.md); strict backend equality is not promised.
Adaptive beauty convergence does not ensure depth convergence.

Deep working memory is bounded and spill is explicit. It is not a total process
memory cap. GPU buffers are host-preallocated; GPU threads allocate no memory.
Capacity, representability, I/O and cancellation failures must preserve an
existing completed EXR. Local-file atomic replacement is qualified; power-loss
durability, network shares and a beauty/deep/CSV frame transaction are not.

## Reproduction and evidence

Fresh standalone suite runs use the installed build and its CUDA wrapper:

```powershell
& tools/qualify_deep_release.ps1 -Group All -Output builds/validation/m8-release-new
```

The runner records executable identity, suite exit status, elapsed time and logs.
Individual validators write numerical reports and Gaffer graphs with actual deep
readers, cuts and DeepToPointCloud. Numerical tests and kernel/grid oracles remain
separate from reader interoperability checks.

Native Blender cases are created from the supplied VDB fixture without changing
the source. `tools/create_vdb_deep_cases.py` generates supported and rejected
settings; `tools/qualify_native_vdb.py` runs beauty pairs, CUDA beauty repeats,
accepted-camera curve checks and output-preservation checks. Separate named-grid
references test the overlap product. These share single-grid capture code and
do not replace the independent OpenVDB integration oracle.

Final native CPU/CUDA matrix results are recorded under
`builds/validation/m8-release-qualified/native-default-colour/`; both devices pass 11 accepted scenes
and nine expected safe rejections. See [final evidence](M8_RELEASE_VALIDATION.md).
The final standalone audit passes all nine CTests, ten renderer/Gaffer suite
groups and lifecycle tests. The final supplied host-asset regression passes with
byte-identical deep output and unchanged beauty. CPU/CUDA production-scale
scattering repeats pass accuracy, beauty, resource and size gates with
byte-identical repeats per device. All technical gates pass; the user approved
the final Gaffer scene on 2026-09-29, completing M8.
