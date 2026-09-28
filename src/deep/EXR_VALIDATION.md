# Deep EXR format and validation

## Format

- Single-part deep scanline EXR; FLOAT Z/ZBack/A, positive axial camera depth in
  scene units. Surfaces have ZBack=Z; volume intervals use exponential extinction.
- Empty pixels have zero samples. No synthetic background or deep RGB.
- Explicit inclusive display/data windows, pixel aspect, frame and view.
  Core writer coordinates follow file order; host adapters handle image orientation.
- NONE and ZIPS compression. Unsupported codecs and invalid/nonfinite data fail.
- FLOAT rounding must pass a whole-curve transmittance check, including displaced
  boundaries and interior volume extrema. Tiny positional error alone is insufficient.
- Continuous volume runs preserve cumulative optical depth at FLOAT boundaries
  by default. Independent rounding is a checked fallback; each choice must pass
  the same error allowance. Surfaces and gaps remain separate.
- General surface export allowance is 1e-6. Volume export reserves part of that
  allowance for density, coefficient rounding, reconstruction and reduction;
  constants live in `volume.h` and the writer.
- No sorted/tidy metadata promise: FLOAT can collapse nearby depths.

Depth metadata uses `cycles:depthConvention=positive_axial_camera_z` and
`cycles:depthUnits=scene_units`. Basic Z/alpha reading needs no custom metadata.
See the [OpenEXR deep-pixel specification](https://openexr.com/en/latest/InterpretingDeepPixels.html).

## Publication

Path writers stage a sibling temporary file, flush/close it, then atomically
replace the destination. Exceptions preserve the prior file and remove temporary
output. A caller-owned stream is the caller's publication responsibility.
See [storage and lifecycle](PRODUCTION_VALIDATION.md).

## Verification

```powershell
ctest --test-dir builds/build-m6 -C Release -R 'cycles_deep_(exr|volume|production)$' --output-on-failure
```

`exr_writer_test.cpp` independently reads channel counts/values, metadata,
compression, empty pixels, and positive/negative windows. It checks exported
curves against raw ledgers, checks volume rounding drift below the rejection
threshold, and exercises invalid data and stream-write failures.
`volume_test.cpp` covers exponential intervals, interior extrema and FLOAT
conversion. `production_test.cpp` covers streaming and atomic replacement.
Fixtures are regenerated under the build tree; Gaffer readers and depth cuts
provide separate application interoperability checks.

[Release status](../../DEEP_IMPLEMENTATION_STATUS.md) | [Evidence](../../DEEP_PERFORMANCE_AND_VDB.md)
