# Deep output architecture

The implementation follows the [peer requirements](../../PEER_DEEP_OUTPUT_REQUIREMENTS.md).
This is the current code map; the superseded patch plan remains in Git history.

| Layer | Ownership |
| --- | --- |
| `src/session/deep.cpp` | Settings validation, material/scene support and host output capability. |
| `src/integrator/path_trace*` | Capture lifetime, bounded buffers, scheduling, readback and delivery. |
| `src/kernel/deep/` | Shared typed records and CPU/CUDA visibility collection. No allocation, files or host dependencies. |
| `src/deep/capture.*` | Completed camera-sample ledgers, spill storage and bounded reconstruction. |
| `src/deep/reconstruction.cpp`, `volume.cpp` | Surface and extinction-to-alpha reconstruction. |
| `src/session/output_driver.h` | Deep-capable host output interface with bounded per-pixel reads. |
| `src/deep/exr_writer.cpp`, `publication.cpp` | FLOAT conversion, error checks and atomic file replacement. |
| `src/app/deep_output.cpp` | Standalone writer adapter. Blender has its own host adapter. |

## Invariants

- Cycles builds and runs without Blender or Gaffer. Hosts use the output-driver
  interface; Gaffer only reviews generated data.
- Kernels collect typed surface alpha or volume optical depth. The host reconstructs
  Z/ZBack/A; those quantities are not interchangeable.
- Host code allocates device pools before dispatch using Cycles device buffers.
  GPU threads write bounded event planes and explicit completion/failure status.
- A completed miss contributes to sample normalization. Incomplete, failed or
  skipped work must not be silently interpreted as a miss.
- Record definitions and layout assertions live in `kernel/deep/types.h`.
  Inactive value fields must be zero. Spill files are process-local scratch,
  not a portable persistence or resume format. Capture tests cover these rules.
- Beauty ray, sample, lens and time semantics are preserved without advancing
  beauty RNG state for deep capture.
- Queue-ordered copies precede host consumption. Output starts only after sample
  completion; cancellation/failure prevents publication.
- Host output callbacks and pixel buffers have scoped lifetimes. Consumers that
  retain data must copy it. Adding virtual output methods requires a host rebuild.

## Release qualification

Native VDB CUDA uses 64 lanes with an explicit staging reservation. Final
CPU/CUDA scene and failure suites pass; current kernel resource attributes match
the previous qualified build. The updated exporter's release-scale scattering
repeats and default-colour native matrix also pass. The user approved the final
Gaffer scene on 2026-09-29, completing M8 within the qualified support matrix.
See [final evidence](M8_RELEASE_VALIDATION.md). Other backends belong to M9.

[Release gates](../../DEEP_MILESTONES.md) | [Status](../../DEEP_IMPLEMENTATION_STATUS.md)
