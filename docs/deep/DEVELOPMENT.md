# Developer verification

Cycles Deep captures camera visibility into bounded records, reconstructs
transmittance on the host, and publishes deep alpha separately from native beauty.
Start with the [project overview](../../README.md), [architecture](ARCHITECTURE.md)
and [support matrix](SUPPORT.md). SVM means Cycles' built-in shader virtual machine;
OSL means Open Shading Language.

## 1. Inspect the accepted output

Download the [production EXRs and checksums](https://github.com/brokenCushion/cycles/releases/tag/deep-output-evidence-2026-10-10).
Start with the smaller `landscape-deep-64.exr`. It has the same channels and
metadata contract as the all-samples file. Both files are unchanged production
outputs, not synthetic examples.

Verify each downloaded file against `SHA256SUMS.txt`. For example, in PowerShell:

```powershell
Get-FileHash ./landscape-deep-64.exr -Algorithm SHA256
```

With Python's OpenEXR binding (the snippet was checked with 3.5.2), inspect the
header without loading millions of samples into memory:

```python
import OpenEXR

image = OpenEXR.File("landscape-deep-64.exr", header_only=True)
header = image.header()
assert header["type"] == OpenEXR.deepscanline
assert {channel.name for channel in header["channels"]} == {"A", "Z", "ZBack", "id"}
assert header["cycles:depthConvention"] == "positive_axial_camera_z"
assert header["cycles:deepSamples"] == 64
print(header["dataWindow"])
print(header["cycles:maxTransmittanceError"])
print(header["cycles:deepIDManifest"])
```

Use a deep-capable viewer for samples, flattened alpha and depth cuts.
[Gaffer integration](../../src/deep/gaffer/README.md) adds the point-cloud node;
install its Python package and startup registration in your own Gaffer setup.
Keep the original EXRs intact. Gaffer FLOAT conversion can lose UINT ID precision;
select IDs as UINT before conversion, as described in the deep output guide.

These checks establish file identity and readable structure. Reproducing the
independent physical oracle also requires the matching scene, grids and captured
camera samples; an EXR alone cannot reproduce that proof. Accepted numerical
results, build identity and original scene attribution are in the
[evidence page](evidence/README.md).

## 2. Run core tests from a clone

The completed feature lives on `deep-output`; GitHub's default branch is still
`main`. Clone the feature explicitly:

```text
git clone --branch deep-output https://github.com/brokenCushion/cycles.git cycles-deep
cd cycles-deep
```

From the repository root, with CMake and a C++17 compiler:

```text
cmake -S src/deep -B build/deep-reference -DBUILD_TESTING=ON
cmake --build build/deep-reference --config Release
ctest --test-dir build/deep-reference -C Release --output-on-failure
```

This standalone CMake project defines three tests: reconstruction, capture and
analytic density cells. It needs neither Blender nor CUDA/OptiX. The optional
EXR tests additionally require OpenEXR and TBB. A full renderer build enables
the remaining integration tests; see [building](../../BUILDING.md).

This route was smoke-tested on Windows with CMake 3.31.2 and MSVC 19.44:
3/3 passed, with 14.15 seconds of test time. It checks the source-only reference
project, not the production renderer or a new reference baseline.

## 3. Reproduce full qualification

The [regression guide](../../src/deep/README.md#regression-command) explains the
single command, dependencies and configuration. It is currently a Windows
qualification harness with D:-drive storage and machine-local paths, not a
zero-setup test for a fresh clone. Supply your own qualified-build configuration;
do not assume the historical installs in the example configuration still exist.

Full replay needs the matching Blender/Cycles builds, scene and VDB assets,
fixture definitions, accepted golden EXRs, resource baseline and beauty-build
registry. Large local assets and golden archives are not included in Git.
The [asset inventory](../../test-assets/ASSET_INVENTORY.md) records the supplied
scenes; [build provenance](BUILD_BASELINE.md) records source pins. The accepted
production executable/toolchain and output checksums are in the evidence page.

Use the [final validation policy](VALIDATION.md) unchanged: independent oracles,
same-build/backend identity, exact CPU beauty, calibrated GPU beauty and
source/resource proofs. Cross-backend curve identity is not promised.

For a contribution, report the build/toolchain, affected backends, fixture,
command, result and retained evidence. Small core-test success does not establish
GPU beauty isolation or full production qualification.
