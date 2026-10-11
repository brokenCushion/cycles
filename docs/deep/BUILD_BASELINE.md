# Development baseline

Source and dependency pins for reproducing this fork:

| Component | Revision |
| --- | --- |
| Original standalone Cycles | `a456b761034dda42c32eef9f4aae0fa5a5c9f604` |
| Windows libraries | `60d6e96b917568278d400a4024c98da0fb777338` |
| Blender host | See [Blender build guide](BLENDER.md). |

Initial Windows qualification used MSVC 19.44 / Visual Studio 2022, SDK
10.0.22621.0 and CMake 3.31.2. Libraries include OpenEXR 3.4.10,
OpenImageIO 3.1.13.1 and OSL 1.15.3.0. The original CPU SVM/OSL flat renders
passed. This records provenance, not current deep-output qualification.

[Build instructions](../../BUILDING.md) | [Production evidence](evidence/README.md)

Large build products and validation artifacts belong on D:, with small reports
under `builds/`. The original build
transcript and milestone-by-milestone setup notes remain in Git history.

## Superseded standalone installs

The two old Phase 8c standalone executables were superseded by an install mistake.
They are not recoverable, but are rebuildable from `0c6821277` with a different
hash. Their former SHA-256 values remain in the
[install failure record](../../builds/validation/landscape-cloud/optimization-phase9/production-timing-build-failure.json).
The user approved moving on without recovery. Reference outputs remain preserved;
the current accepted executable and toolchain are identified in production evidence.
