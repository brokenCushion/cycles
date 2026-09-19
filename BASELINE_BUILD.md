# Cycles Deep EXR: Windows baseline

Date: 2026-09-16. This report covers the unchanged standalone Cycles build only.
Deep EXR implementation and Blender integration have not started.

## Source

- Fork: https://github.com/brokenCushion/cycles
- Upstream mirror: https://github.com/blender/cycles
- Branch: `codex/deep-exr`
- Baseline: `a456b761034dda42c32eef9f4aae0fa5a5c9f604` (upstream main, 2026-09-08).
- Windows libraries: `60d6e96b917568278d400a4024c98da0fb777338`.
- No applicable AGENTS.md found in the checkout or searched parent directories.
- Build guidance: `BUILDING.md`, `make.bat`, and inspected CMake configuration.

This is a pinned development baseline, not a qualified stable release. Standalone
Cycles supports the initial experiments; later Blender settings and output
integration require a full Blender checkout and revision mapping.

## Environment

- Windows 11 Home, x64, version 10.0.26200.
- AMD Ryzen 9 5900X; approximately 80 GiB physical memory.
- Visual Studio Community 2022 17.14.28.
- MSVC 19.44.35224.0 (toolset directory 14.44.35207).
- Windows SDK 10.0.22621.0.
- CMake 3.31.2; Git LFS 3.7.1.
- Bootstrap Python 3.12.14 from the Codex runtime (Python is absent from PATH).
- Pinned precompiled libraries include OpenEXR 3.4.10, OpenImageIO 3.1.13.1,
  OpenColorIO 2.5.0, and OSL 1.15.3.0.

## Reproduction

Run from the repository root in PowerShell. The explicit Python path is local
to this machine; another Python 3 installation may be substituted.

```powershell
& 'C:\Users\jun\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' src/cmake/make_update.py --no-cycles

cmake -S . -B builds/build-baseline -G 'Visual Studio 17 2022' -A x64 `
  -DWITH_CYCLES_DEVICE_CUDA=OFF -DWITH_CYCLES_DEVICE_OPTIX=OFF `
  -DWITH_CYCLES_DEVICE_HIP=OFF -DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF `
  -DWITH_CYCLES_USD=OFF -DWITH_CYCLES_OSL=ON -DWITH_STRICT_BUILD_OPTIONS=ON

cmake --build builds/build-baseline --target install --config Release --parallel 4
ctest --test-dir builds/build-baseline -C Release --output-on-failure

.\builds\install\cycles.exe --list-devices
.\builds\install\cycles.exe --device CPU --background --samples 8 --threads 8 --width 160 --height 100 --output builds/build-baseline-logs/native.exr examples/scene_cube_surface.xml
.\builds\install\cycles.exe --device CPU --background --shadingsys osl --samples 8 --threads 8 --width 160 --height 100 --output builds/build-baseline-logs/osl.exr examples/scene_osl_stripes.xml

$env:PATH = "$((Resolve-Path builds/install).Path);$env:PATH"
.\lib\windows_x64\openimageio\bin\oiiotool.exe --info -v --stats build-baseline-logs/native.exr
.\lib\windows_x64\openimageio\bin\oiiotool.exe --info -v --stats build-baseline-logs/osl.exr
```

CPU Release configuration, OSL enabled, GUI/GPU/Hydra/USD disabled. Other library
options retain upstream defaults. No deep feature is present in this baseline.
The update command preserves the source revision using `--no-cycles`.

## Results

- Dependency update: exit 0; about 6.7 GB of LFS objects downloaded.
- CMake configuration: exit 0.
- Release compilation and installation: exit 0. Executable: `install/cycles.exe`.
- CTest: exit 0; `cycles_version` passed (1/1 tests, 3.69 seconds total).
- Device listing: exit 0; AMD Ryzen 9 5900X CPU detected.
- Native shader render: exit 0; `examples/scene_cube_surface.xml`, 8 samples.
- OSL render: exit 0; `examples/scene_osl_stripes.xml`, 8 samples.
- OpenImageIO readback/statistics: exit 0 for both EXRs. Each is 160 x 100,
  FLOAT RGBA, nonconstant, with 16,000 finite values per channel and zero
  NaN/infinite values. These are ordinary flat EXRs, not deep files.
- Both render logs warn about an empty color-space name and fall back to scene
  linear. Output is readable and finite; this baseline does not qualify a
  production color-management workflow.
- CMake reports missing optional LevelZero; CPU configuration and build pass.

Local logs, scripts, and rendered outputs are in the ignored
`build-baseline-logs/` directory. Build products are in `build-baseline/` and
`install/`. The standalone CTest suite includes `cycles_version`; broader unit
tests in `src/test` depend on Blender's test infrastructure and are not enabled
in this configuration.

## Limits and next step

The unchanged standalone CPU baseline is verified. This completes build setup,
not the proposal's full M0 source investigation. This build does not qualify
deep output, native/OSL deep-opacity parity, GPU
rendering, Nuke, or full Blender integration. After baseline verification, map
camera samples, shader opacity, transparent continuation, and output ownership
in the pinned source before implementing the independent reconstruction model.
