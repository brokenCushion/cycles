# Synthetic Deep EXR validation (M2)

## Build and reproduce

From the existing Windows baseline build:

```powershell
cmake -S . -B build-baseline -DWITH_CYCLES_DEEP_TESTS=ON -DWITH_CYCLES_DEEP_EXR_TESTS=ON
cmake --build build-baseline --config Release --target cycles_deep_reference_test cycles_deep_exr_test
ctest --test-dir build-baseline -C Release --output-on-failure
```

CTest regenerates eight EXRs and `expected_pixels.csv` in
`build-baseline/src/deep/fixtures`. Both experimental build options default OFF.
To choose another output directory:

```powershell
.\build-baseline\bin\Release\cycles_deep_exr_test.exe .\build-baseline\src\deep\fixtures
```

The executable generates and round-trips the files and exits nonzero on failure.
The writer uses the checkout's existing OpenEXR target; the reconstruction
reference remains dependency-free. Windows test DLL staging requires CMake 3.21+.

## Serializer contract

- Single-part deep scanline file, FLOAT `Z`, `ZBack`, and `A` only.
- Surface samples have `ZBack=Z`; empty pixels have zero samples in all channels.
- Input samples must be finite and strictly depth-sorted, with positive depth
  and alpha in (0,1]. No sorting, reduction, or synthetic background is hidden
  in serialization.
- Explicit inclusive display/data windows, positive pixel aspect, frame and view.
- Pixel vector order is increasing file y then x, beginning at the data-window
  minimum. No implicit flip; real renderer/beauty alignment remains M3 work.
- Metadata: `cycles:depthConvention=positive_axial_camera_z`,
  `cycles:depthUnits=scene_units`, `cycles:frame=<integer>`, standard `view`.
  Basic Z/alpha reading needs no custom interpretation of these attributes.
- Screen-window center/width are fixed to (0,0)/1 for these synthetic images.
- Tested codecs: NONE and ZIPS. Other compression enum values fail explicitly.
- No sorted/tidy metadata promise: FLOAT can collapse nearby double depths.

The channel and point-sample contract follows the
[OpenEXR deep-pixel specification](https://openexr.com/en/latest/InterpretingDeepPixels.html).

Before opening a destination, the writer validates the samples and header and
checks FLOAT conversion against a maximum absolute transmittance error of 1e-6.
It compares both step functions over the union of original and exported depths,
covering both sides of their boundaries. Depth rounding matters: moving an opaque
boundary from 2.00000001 to FLOAT 2 is rejected even though its positional error
is small. Exactly FLOAT-representable depths avoid that issue; weak shifted
events can pass if the entire curve remains in budget. Underflow to zero fails.

The path API checks flush and close and propagates exceptions. Invalid input
preserves an existing destination. An I/O failure may leave a partial file:
atomic publication, cancellation, and beauty/deep pairing are deferred to M5.
Whole-image input/conversion buffers are acceptable for this synthetic prototype;
no production memory bound is claimed.

## OpenEXR and OpenImageIO evidence

Environment: [baseline report](../../BASELINE_BUILD.md). OpenEXR 3.4.10,
OpenImageIO 3.1.13.1, MSVC 19.44, Windows x64 Release.
Configuration/build passed. CTest passed 3/3: baseline version, reconstruction,
and deep EXR tests. Maximum measured round-trip transmittance error:
`1.1920928910669204e-08`, below 1e-6.

The reader is independent of writer internals and allocates each channel
separately, including ZBack. It verifies completeness, deep type/version,
channels/types, counts, exact FLOAT values, windows, aspect, compression,
line order and frame/view/depth metadata. Readback curves are checked against
raw camera-sample ledgers at original and exported boundaries.

| Prefix | Data window (inclusive) | Display window (inclusive) | Aspect |
| --- | --- | --- | --- |
| `surfaces_0` | (0,0)-(3,2) | (0,0)-(3,2) | 1 |
| `surfaces_1` | (-2,-1)-(1,1) | (-4,-3)-(5,6) | 1.5 |
| `surfaces_2` | (5,7)-(8,9) | (0,0)-(11,13) | 0.75 |
| `empty` | (0,0)-(3,2) | (0,0)-(3,2) | 1 |

Every prefix has `_none.exr` and `_zips.exr`. Surface images have 270 total
samples: two empty pixels, five 1-sample pixels, three 2-sample pixels, one
3-sample pixel and one 256-sample pixel. Empty files have zero samples throughout.
OIIO independently reads `surfaces_0_none.exr` as deep FLOAT A/Z/ZBack with that
distribution and no NaN/Inf values. This shares OpenEXR infrastructure and is not
proof of compatibility with every compositor.

Failure tests cover invalid windows/counts/metadata/samples, excessive FLOAT
quantization, destination-open failure, injected header/sample-write failures,
and preservation of existing files on validation failure. Actual disk-full,
close-failure injection and interrupted publication are not tested.

## Gaffer test bed

The user selected Gaffer as the application-level validation target. Official
portable Windows release 1.7.2.0 was downloaded from
[GafferHQ releases](https://github.com/GafferHQ/gaffer/releases/tag/1.7.2.0).
Archive SHA-256 was checked against the published asset digest:
`bd6fa3406813bbf67e630b3bc96d51883c670f1c647ac00cf6167aaeaaef332f`.
The portable runtime is in the ignored `build-gaffer/` directory.

```powershell
.\build-gaffer\gaffer-1.7.2.0-windows\bin\gaffer.cmd python src/deep/validate_gaffer.py build-baseline/src/deep/fixtures
```

The script checks all eight files through ImageReader, per-pixel sample counts,
Z/ZBack/alpha, windows and aspect, then DeepToFlat alpha and four DeepSlice depth
cuts. File coordinates are converted using Gaffer's `Format.fromEXRSpace` API,
including negative and offset windows. It writes `gaffer_validation.json` and a
small `deep_validation.gfr` graph into the fixture directory on success.
Gaffer validation passed all eight files, exit 0. Maximum flattened alpha error:
`3.35239146442845e-08`; maximum partial-depth error:
`1.1920928910669204e-08` (both below 1e-6). The saved graph loaded successfully
in a separate headless check. Initial filename backslash substitution was fixed
by passing forward-slash paths to Gaffer plugs; final validation uses that form.

For visual inspection, launch the portable Gaffer with
`build-baseline/src/deep/fixtures/deep_validation.gfr`. Select `flatten` or
`nearOpacity` in its Viewer and display alpha: these fixtures contain no RGB.

The bundled Cycles renderer is a future integration test bed, not a substitute
for this fork. Loading our EXRs does not mean Gaffer's Cycles produces deep data.
Runtime introspection reports bundled Cycles **5.1.0**. Future renderer
integration must account for that revision/build/ABI; no renderer replacement
or Gaffer Cycles render was performed in M2.

Node references: [ImageReader](https://www.gafferhq.org/documentation/1.7.1.0/Reference/NodeReference/GafferImage/ImageReader.html)
and [DeepToFlat](https://www.gafferhq.org/documentation/1.7.1.0/Reference/NodeReference/GafferImage/DeepToFlat.html).
The test uses the installed 1.7.2.0 API and pinned upstream test examples.

## Optional Nuke procedure (not executed)

Nuke is unavailable to the user and is not the acceptance test bed. If it becomes
available later: connect DeepRead for `surfaces_0_none.exr` to DeepToImage and
inspect alpha. Select the `left` view as needed. Compare every pixel to
`expected_pixels.csv`, recording Nuke's mapping from file coordinates. Check the
disjoint pixel at file (3,0): depths 2 and 8, alpha 0.5 then 1, flattened alpha 1.
The stack at (0,1) must flatten to 0.625. Repeat empty, ZIPS and cropped variants
without reformatting, verifying windows/aspect. Record Nuke version and errors.
No RGB data is present, so a black RGB Viewer is not a failure by itself.

References: [DeepRead](https://learn.foundry.com/nuke/Content/reference_guide/deep_nodes/deepread.html)
and [DeepToImage](https://learn.foundry.com/nuke/content/reference_guide/deep_nodes/deeptoimage.html).
Nuke compatibility remains unclaimed.
