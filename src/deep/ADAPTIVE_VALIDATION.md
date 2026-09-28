# Adaptive surface sampling

CPU native/restricted OSL and CUDA native capture follow beauty's accepted
camera samples. Completed misses count; skipped lanes do not. Independent film
population counts must match complete, contiguous capture identities before
publication. Missing/extra samples, decreasing counts and cancellation fail.
Four bytes per pixel for population counts are included in the memory budget;
adaptive sampling reduces captured work, not the reserved spill capacity.

## Evidence

`validate_adaptive_gaffer.py` covers transparent stacks, misses, opaque surfaces,
variable-population edges and three seeds of an equal-colour depth discontinuity.
CPU/CUDA accepted populations agree in these diagnostic fixtures. The edge uses
24,128 samples instead of 98,304; populations range from 16 to 128. Maximum
ledger-to-EXR curve error is 2.608e-8 (limit 1e-6). Beauty is unchanged in the
recorded runs. Overflow/rejection tests preserve the previous EXR.

**Beauty convergence does not guarantee deep convergence.** Equal-colour near/far
surfaces stop at 16 samples despite uncertain depth. Three seeds differ by up to
0.0625 transmittance from a finite 512-sample reference. This is sampling error,
not reconstruction error; the reference is not exact ground truth.

Reports and Gaffer reviews: `builds/build-m6/m7-adaptive` and
`builds/build-m6/m7-adaptive-cuda`. These are small surface fixtures, not volume
or production-scale qualification. See [DOF](DOF_VALIDATION.md),
[motion](MOTION_VALIDATION.md) and the [M8 gates](../../DEEP_MILESTONES.md).

```powershell
builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_adaptive_gaffer.py builds/install-m6/cycles.exe builds/build-m6/m7-adaptive
builds/build-m6/run-cuda.cmd builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd env python src/deep/validate_adaptive_gaffer.py builds/install-m6/cycles.exe builds/build-m6/m7-adaptive-cuda CUDA builds/build-m6/m7-adaptive
```
