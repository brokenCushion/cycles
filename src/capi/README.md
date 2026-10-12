# Cycles C API â€” Phase 1

Experimental shared-library boundary for host applications such as Gaffer.
The contract is [cycles_c.h](cycles_c.h): it contains no Cycles C++ headers or
deep types. It initializes resource paths, enumerates native devices, reports
compiled capabilities, routes logs and introspects built-in node types, sockets,
defaults, flags and enums. It does **not** render yet.

## Build and test

Use the repository's [build prerequisites](../../BUILDING.md), including its
supported compiler and dependency bundle. Configure a **fresh build directory**
and set the install prefix at configure time:

```text
cmake -S . -B <build> -DWITH_CYCLES_C_API=ON -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=<new-install>
cmake --build <build> --target cycles_c cycles_c_host cycles_c_contract cycles_c_native_dump
ctest --test-dir <build> -R "^cycles_c_" --output-on-failure
```

On Windows, run these in the configured clang-cl/Visual Studio environment.
Runtime dependencies must be discoverable by the OS loader (the dependency
bundle's DLL directories on PATH for build-tree tests). The existing dependency
installation helper also supplies them for installed builds. Inspect generated
install destinations before installing; never reuse a qualified install prefix.
Keep build directories and test output outside the repository, on D: locally.
Before a full `cmake --install <build>`, build the default target too; the native
install rules also need resources such as compiled OSL shaders, not just the DLL.
For a build without the unrelated USD/Hydra integration, set both
`WITH_CYCLES_USD=OFF` and `WITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF`.

`cycles_c_host <absolute-library-path> <absolute-resource-path> <absolute-user-path>`
is compiled as C11, uses LoadLibrary/GetProcAddress or dlopen/dlsym, and has no
Cycles import-library dependency. `cycles_c_contract` additionally checks native
enumeration, exception containment, struct sizes, thread-local errors and logging.
`cycles_c_exports` requires the exact twelve public functions, rejecting extra
symbols even if they start with `ccl_`. No tests depend on deep being enabled.

## Reflection and registration completeness

API 1.1 adds five queries: node count, node description, socket description,
one default element, and one enum entry. Inputs and outputs are separate;
inherited sockets are included. Type and enum lists are sorted by name, while
sockets retain declaration order. Defaults use C scalars, UTF-8 strings and
explicit float components, never a native C++ memory layout. Empty array defaults
are distinct from missing defaults; closures/outputs have no default. Native
empty strings are represented by non-null empty C strings. Instance-specific
OSL shader sockets remain part of Phase 4.

Two independent checks protect this boundary:

- `cycles_c_source_inventory` scans source `NODE_DEFINE` declarations and the
  conversion-node generator, then queries the **actual DLL**. It caught missing
  `buffer_params` and `buffer_pass` before the link fix. The wrapper now retains
  `cycles_scene` and `cycles_session` with `/WHOLEARCHIVE`, `--whole-archive` or
  `-force_load`; no native registration code changed. The source scanner fails
  on unrecognized registration shapes rather than silently skipping them.
- `cycles_c_reflection` compares the full plain-C dynamic-host dump with a
  separate whole-archive native C++ dump: node/base/kind, socket names/UI labels,
  directions/types/flags/constraints, default values and enum names/numbers.
  Exact float hex output preserves the native values. The native dump does not
  call the wrapper. The contract test also exercises nonempty defaults of all
  eleven array types, including types whose built-in defaults are empty.

Measured inventory: 118 `NODE_DEFINE` types + 64 generated conversion types +
2 registered abstract bases = **184 types**; **1,277 sockets, 963 default elements,
816 enum entries** match. The unused `procedural_base` is explicitly accounted
for as a lazy abstract definition, not a missing `NODE_DEFINE` registration.
The original pre-fix inventory is retained in the D: validation directory.

## Phase 1 outline decisions

- Version, feature and device-count queries return `ccl_status` and write into
  host-owned outputs. An error cannot masquerade as zero devices or an unsupported
  feature. Version output also has a size field instead of returning an extensible
  struct by value. All public integers have explicit widths.
- Init exposes the **two existing native resource roots**, not independent kernel,
  OSL and temporary paths that native `path_init` does not provide. Repeated init
  with identical strings is allowed; changing roots is rejected.
- Device metadata is an immutable discovery snapshot. Its strings live for the
  library's process lifetime. Compiled backend support is distinct from available
  hardware. OSL denotes compiled support; individual devices report their support.
- The deep capability stays false until the C API extension is implemented, even
  if the underlying Cycles build includes deep code.
- Log callbacks are serialized, may run on worker threads, and cannot reenter the
  API. Removing/replacing a callback waits for the old call to finish. Callback
  strings are borrowed only during that call. Native severity filtering is kept.
- Phase 1 supports one loaded Cycles library retained until process exit. Safe
  unload, multiple simultaneous versions, resource relocation and backend runtime
  compatibility need explicit lifecycle design before the Phase 7 swap promise.

Phase 2 applies the status/out convention to reflection. Before later phases,
use it for handle creation too; specify scene/graph ownership and deletion
invalidation; do not assume pausing rendering alone makes scene edits safe;
define callback lifetime, cancellation and buffer stride/capacity rules before
session/output functions. These are proposals, not implemented future phases.

## Runtime dependencies and host coexistence

`dumpbin /dependents` on the Windows Phase 1 and Phase 2 DLLs reports the same
direct runtime imports (2026-10-12):

- `OpenImageIO.dll`, `OpenImageIO_Util.dll`
- `oslexec.dll`, `oslcomp.dll`, `oslquery.dll`
- `openvdb.dll`, `embree4.dll`, `OpenColorIO_2_5.dll`
- `OpenImageDenoise.dll`, `tbb12.dll`, `sycl8.dll`
- `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`
- `KERNEL32.dll`, `ADVAPI32.dll`, `CFGMGR32.dll`, `VERSION.dll`
- Windows UCRT API sets `api-ms-win-crt-{stdio,heap,runtime,filesystem,time,math,
  convert,environment,utility,string,locale}-l1-1-0.dll`.

This is the direct import table, not a complete transitive or driver-load list.
CUDA/OptiX/HIP drivers can be loaded dynamically. This build disables USD/Hydra,
so it has no direct USD import. Dependencies may load further DLLs themselves.
The C ABI **does not yet isolate these dependencies** from a host's loaded DLLs.

For the GafferCycles author to decide before Phase 7:

| Option | Tradeoff |
| --- | --- |
| Static dependencies with hidden symbols | Stronger isolation where supported; larger binaries, additional build/licensing work, and transitive global-state/symbol interposition checks. |
| Renamed private DLLs | Keeps dynamic dependencies; requires rebuilding/renaming all affected transitive imports and checking plugins and driver interactions. |
| Version-matched VFX Reference Platform stack | Shares a coordinated dependency set; simpler integration but restricts compatible renderer swaps and does not support arbitrary mixed versions. |

Phase 7 must preload a different OIIO version before `cycles_c`, exercise both,
and verify loaded-module identities. It must also establish the supported policy
for OSL, OpenVDB, Embree, OCIO and USD coexistence. None of these isolation options
is implemented in Phase 2.

## Internal changes

No Cycles rendering, device, scene, scheduling or kernel source was changed.
Build integration adds an opt-in `WITH_CYCLES_C_API` and shared wrapper target,
and enables position-independent static objects when that option is on. Phase 2
adds whole-archive retention for `cycles_scene` and `cycles_session` in the wrapper
and its independent reference test only. The
wrapper calls existing `path_init`, `log_init` and `Device::available_devices`.
Static implementation symbols are private to the shared library: explicit
exports on Windows, linker export lists on ELF/Mach-O. Dependencies may still
be separate runtime libraries; a C ABI does not make them self-contained or
guarantee compatibility between arbitrary dependency versions.

The API and tests are intentionally limited to Phases 1–2. ABI evolution and render
parity remain acceptance gates in the [plan](../../docs/plans/CYCLES_C_API.md).
