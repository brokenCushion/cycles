# Native VDB deep alpha

Current scope and qualification follow the [support matrix](RELEASE_MATRIX.md)
and [optimization plan](../../DEEP_OPTIMIZATION_PLAN.md). Historical experiments
remain in Git history at `9cad1e861`.

Native scalar NanoVDB density uses the existing scene/grid transforms and linear
interpolation. Along a ray, trilinear cells integrate as exact cubics. Numeric
device compression has a stated transmittance allowance and never increases the
per-ray record count; cells that cannot merge retain their exact cubic record.
The host fits and publishes Z/ZBack/A with the remaining budget. Early termination
charges the object's allowance; overlapping media are bounded explicitly.
Scattering contributes extinction; deep colour remains deferred.

Preflight: `src/session/deep.cpp`; traversal/compression:
`src/kernel/deep/volume_grid.h`; captured records/spill: `capture.*`;
host fitting: `volume.cpp`; FLOAT/error/publication: `exr_writer.cpp`.
No host library, heap allocation or file access enters GPU kernels. Exhausted
capacity and unsupported shaders/cameras/grids fail explicitly.

The shared regression checks accepted-camera curves, independent geometry/depth
cuts, IDs, CPU beauty equality, strict/numeric identity and kernel/source records.
Independent cubic/grid tests remain separate from Gaffer reader review.
