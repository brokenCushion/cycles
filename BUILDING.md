Building Cycles
===============

## Prerequisites

Ensure the following software is installed and available in the PATH:
- Git
- Git LFS
- Python 3
- CMake

### Standalone Build

Get the source code:

    git clone https://projects.blender.org/blender/cycles.git
    cd cycles

Download precompiled libraries and build:

    make update
    make

The resulting binary will be at:

    ./install/cycles

## Hydra Render Delegate with USD Repository

This will make the render delegate work with usdview and other applications built using the USD repository. USD version 25.11 or newer is required.


USD includes a script to build itself and all required dependencies and then install the result a specified directory.

    git clone https://github.com/PixarAnimationStudios/USD.git
    cd USD
    python3 build_scripts/build_usd.py "<path to USD install>"

Get the Cycles source code:

    git clone https://projects.blender.org/blender/cycles.git
    cd cycles

At the moment, when using USD from the USD repository, Cycles will build without OSL and NanoVDB support.
To build Cycles, download the Cycles dependency libraries and point CMake to the USD directory like this.

    make update
    cmake -B ./build -DPXR_ROOT="<path to USD install>"
    make

Test in usdview.

    PYTHONPATH=<path to USD install>/lib/python PXR_PLUGINPATH_NAME=<path to cycles>/install/hydra <path to USD install>/bin/usdview

## Hydra Render Delegate for Houdini

For use in Houdini, Cycles must be built using Houdini's USD libraries. Houdini version 21+ is required. 

Get the source code:

    git clone https://projects.blender.org/blender/cycles.git
    cd cycles

Download precompiled libraries and build.

Linux:
```
    make update
    cmake -B ./build -DHOUDINI_ROOT="<path to Houdini>"
    cmake --build build --config Release
    cmake --install build
```

Windows:
```
    .\make.bat update
    cmake -B .\build -DHOUDINI_ROOT="<path to Houdini>"
    cmake --build build --config Release
    cmake --install build
```

The path to Houdini depends on the operating system, typically:
- Linux: `/opt/hfsX.Y`
- macOS: `/Applications/Houdini/HoudiniX.Y.ZZZ`
- Windows: `C:/Program Files/Side Effects Software/Houdini X.Y.ZZZ`

Test in Houdini using an environment variable.

    PXR_PLUGINPATH_NAME=<path to cycles>/install/houdini/dso/usd_plugins houdini

Or copy `install/houdini/packages/cycles.json` to the Houdini packages directory to make it always available.
The packages directory can be found (or needs to be created) under:
- Linux: `/home/[username]/houdiniX.Y`
- macOS: `/Users/[username]/Library/Preferences/houdini/X.Y`
- Windows: `C:/Users/[username]/Documents/houdiniX.Y`
**Note:** If you move the Cycles installation folder to another place you need to adjust the path in `cycles.json`.

## Build System

### Windows deep-output qualification toolchain

Phase 8a uses clang-cl 20.1.8, Ninja 1.13.2, CMake 3.31.2, CUDA Toolkit
12.8.0 (NVCC 12.8.61) and OptiX SDK 8.0.0, with Blender revision
`749518deb2f0735a22361a07488b35f7ea5c2fdf`. Use an x64 Visual Studio 2022
developer environment. Main C/C++ compilation uses clang-cl; NVCC's host
compiler remains MSVC 19.44 (14.44.35207), explicitly selected with
`CUDA_HOST_COMPILER` for the standalone build. Enable native precompiled CUDA
and OptiX modules. Keep builds, validation images and TEMP/TMP on D:.

OSL surface capture uses the bundled OSL 1.15.3.0 on CPU/OptiX. Its separate
`kernel_optix_deep_osl` module uses native compiled OSL callables; optimized
group `ShadingSystem::getattribute` queries and cached loaded OSO metadata
provide preflight. Phase 8c qualifies adaptive OSL volume evaluation on CPU/OptiX;
CUDA OSL remains unsupported. Native analytic VDB capture stays the default.

See [Blender deep build commands](docs/deep/BLENDER.md) and the
[validation policy](docs/deep/VALIDATION.md). A recorded compiler change
uses its own CPU/CUDA references after independent qualification;
retain the cl.exe/NVCC references for history. Within the same build and backend,
payload and deterministic-header byte identity remains mandatory. Across builds,
toolchains or backends, use the policy's unified cross-comparison rule.

Cycles uses the CMake build system. As an alternative to the `make` wrapper, CMake can be manually configured.

See the CMake configuration to enable and disable various features.

The precompiled libraries are shared with Blender, and will be automatically downloaded from the Blender repository with `make update`. This will populate a submodule in the `lib/` folder, matching the platform.

## Dependencies

Core Cycles has the following required and optional library dependencies. These are all included in precompiled libraries.

Required:
- OpenImageIO
- TBB

Optional:
- Alembic
- Embree
- OpenColorIO
- OpenVDB / NanoVDB
- OpenShadingLanguage
- OpenImageDenoise
- USD

For GUI support, the following libraries are required. The SDL library must be manually provided, it's not part of the precompiled libraries.
- OpenGL
- GLEW
- SDL

For GPU rendering support on NVIDIA cards, these need to be downloaded and installed from the NVIDIA website.
- CUDA Toolkit 11 or newer
- OptiX 7.3 SDK or newer
