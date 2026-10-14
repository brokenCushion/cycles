# Local Gaffer proof of concept

`Renderer.cpp` registers **CyclesC** alongside native **Cycles**. It includes
only `cycles_c.h` from Cycles and loads the front DLL dynamically. This is a
qualification prototype, not a complete replacement for GafferCycles.

Qualified on Windows against Gaffer commit
`afb285e7f5b7245944f3111321068bcee0ae3888` and GafferHQ dependencies
11.0.0a8 platform25, a **pre-release set for Gaffer 1.8 development**. Cortex
10.7.3.1 was built locally to supply the PointInstancer interface required by
that Gaffer revision. No original dependency package was modified.

## Results

- Three 64×48, 16-sample fixtures: emissive mesh, two transformed meshes and a
  diffuse mesh with a point light. CPU is bit-identical in RGBA, normal, depth
  and sample count (nine channels).
- CUDA and OptiX each pass the unchanged beauty/count/bias policy using five
  native ordinary controls and four native seed references per fixture. Lit RGB
  maximum difference is 2.2351741790771484e-8; alpha, normal, depth and counts
  are exact. Emissive fixtures are exact in all nine channels.
- Interactive CPU rendering is exact before and after a transform/shader edit.
- The same Gaffer/plugin DLLs load core-only and deep-enabled libraries without
  rebuilding. Four fixtures, including the interactive edit, are exact between
  builds. Capability queries confirm deep off/on. Both pass dependency checks
  inside Gaffer, 98-export checks and the OS-only front import check.
- Core build: 14/14 CTests. Deep build: 23/23, including nine native deep tests.
  Timing observations are provisional and do not determine acceptance.

[Machine-readable evidence](../../../builds/validation/c-api/phase9-final.json)
contains hashes, exact differences, library-swap checks and patch hunk ranges.

## Why the native adapter needed changes

The comparison baseline uses **the identical Cycles 5.3 native static libraries
and build definitions** as the API backend. The
[native patch](native-cycles53.patch) adapts Gaffer's C++ integration from 5.1.
All paths below are relative to `src/GafferCycles/IECoreCyclesPreview/`; ranges
refer to the patched local checkout and include context.

| File and new line ranges | Cycles API change requiring adaptation |
| --- | --- |
| Attributes.h 139–146; Attributes.cpp 559–570, 616–622, 809–837 | Separate visibility flags; abstract Light replaced by concrete light types/factory. |
| Renderer.cpp 729–736, 762–768, 1252–1264, 1400–1406, 1789–1795 | Concrete light creation/type-change recreation; visibility flag type/names; LIGHT_SUN. |
| ShaderNetworkAlgo.cpp 529–535, 541–560, 573–609, 962–975 | Concrete area/spot/sun setters, radius/smooth socket names, visibility constants. |
| IECoreCycles.cpp 340–354, 363–369 | Reflect sockets from concrete light types while preserving Gaffer-facing names. |
| MeshAlgo.cpp 121–130, 137–155, 192–200, 250–257 | Position attribute storage; resize/fill topology arrays and subdivision face offsets. |
| CurvesAlgo.cpp 81–91, 99–109, 121–131 | Hair position/radius attributes and resized topology arrays. |
| PointsAlgo.cpp 74–85, 89–95, 99–105, 121–127; SphereAlgo.cpp 81–90 | Point-cloud position/radius attributes and resized arrays. |
| GeometryAlgo.cpp 143–149, 162–168, 183–189, 192–198, 395–419, 519–525 | Typed writable packed attributes, motion attributes and attribute-size signature. |
| SocketAlgo.cpp 187–193, 537–543 | Packed float3 socket-array representation. |

The native source adaptation totals **146 added / 95 removed lines in 11 files**.
SConstruct separately adds 25 lines for matched definitions/libraries, whole-archive
registration, native-only C++20 and registration of the new C-only plugin. The
last item is prototype setup, not a Cycles-version adaptation. Exact hunks and
counts remain in the patch/evidence.

After the C adapter was implemented, swapping the two qualified API builds
required **zero host source changes and zero host rebuilds**. This does not mean
that writing the initial adapter cost nothing or that untested future major
versions are interchangeable.

## Reproduction and scope

Guarded drivers are in `../test/gaffer_*.py`. Build/install/output destinations
must resolve under `D:/temp/cycles_c_api`; repo writes are limited to durable
code/docs/small reports. `gaffer_native_baseline.py`, `gaffer_adapt_native.py`
and `gaffer_stage_adapter.py` reproduce the local host changes. The public header
is copied from this repository. Nothing has been pushed or submitted.

The subset supports static linear triangle meshes with unindexed vertex normals,
basic built-in shader networks, constant point lights and one camera. It does not
qualify Gaffer-side OSL, volumes, motion, subdivision, object deletion or the full
interactive UI. API-level OSL/deep tests from earlier phases are separate evidence.
Do not deploy this prototype as a production Gaffer renderer.

Repairs found by testing: transfer mesh normals/smoothing; explicitly resume a
paused session after resetting it. Harness repairs use native ieDisplay output,
remove/re-add native outputs before edits, preserve FLOAT precision, recognize
OptiX device IDs and avoid Python message-handler lock contention. CPU exactness
and the existing GPU policy were not relaxed. No Cycles internals were changed.
