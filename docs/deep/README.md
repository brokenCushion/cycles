# Cycles Deep documentation

Start with the [project overview](../../README.md).
Both full-resolution landscape production settings are accepted; see
[render previews, measurements and build identity](evidence/README.md).

| Guide | What it covers |
| --- | --- |
| [Quick start](QUICKSTART.md) | Download custom Blender, configure deep output, render and inspect a small example. |
| [Build Cycles](../../BUILDING.md) | Standalone/Hydra builds and the qualified toolchain. |
| [Blender integration](BLENDER.md) | Custom Blender build, scene settings and rendering. |
| [Deep output guide](../../src/deep/README.md) | Channels, error/sample controls, native VDB, OSL and regression. |
| [Gaffer review](../../src/deep/gaffer/README.md) | Connected depth cuts and DeepToPointCloud. |
| [Supported features](SUPPORT.md) | Qualified backends, shaders, cameras and limitations. |
| [Validation policy](VALIDATION.md) | Final numerical, beauty, identity and cross-backend gates. |
| [Architecture](ARCHITECTURE.md) | Core ownership and implementation invariants. |
| [Developer verification](DEVELOPMENT.md) | Inspect published files, run core tests, and prepare full qualification. |
| [Production evidence](evidence/README.md) | Accepted all-samples/64-sample results and checksums. |

## Provenance and future work

- [Original build baseline](BUILD_BASELINE.md).
- [Approved historical M8 release record](M8_RELEASE_VALIDATION.md).
- [Archived development reports](ARCHIVED_REPORTS.md), recoverable from Git history.
- [Parked OSL AOV side project](../plans/OSL_AOV.md), separate from deep output.

Build products, local scene assets and large validation files are not part of
this documentation cleanup. Full render outputs remain outside Git.
