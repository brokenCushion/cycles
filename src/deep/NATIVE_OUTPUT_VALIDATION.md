# Deep output host API

Hosts opt into `OutputDriver::supports_deep_output()` and implement
`write_deep_render_tile(const DeepTile &)`. This changes the vtable and requires
rebuilding the host against matching headers; it is not a drop-in DLL replacement.

## Lifecycle

1. Set `SessionParams::deep` mode, event capacity and working-memory allowance.
2. Install a deep-capable output driver before session reset/start.
3. Supply full-frame BufferParams matching the camera; start and wait normally.
4. Consume the final callback, checking `tile.cancelled()` during long work and
   immediately before publishing the host result.
5. Inspect Session progress for errors and cancellation after waiting.

PathTrace owns capture. A reset invalidates old storage before validating or
allocating replacements. Disabling deep releases capture. All workers finish and
sample completeness passes before delivery. Errors/cancellation suppress delivery;
callback exceptions become session errors. Resume, crops, tiling and multiple
render devices remain rejected.

## Data and lifetime

- Matching data/display windows at (0,0); Y-up render-buffer coordinates.
  File adapters perform their required row flip.
- Positive axial camera depth; equal front/back is a surface, positive extent
  is an exponential extinction interval. Values are visibility alpha, not RGB.
- `get_pixel()` returns an owned vector. Hosts may retain it at their own memory
  cost; the tile reference expires when the callback returns.
- Serialize reads on the callback thread. Do not reset Session inside the callback.
- Streaming volume writers must enforce `volume_row_sample_limit()` before
  converting another pixel. It accounts for FLOAT rows and supported EXR codec
  buffers; a custom consumer owns any additional retained memory.
- `population()` includes completed misses. `get_camera_sample()` exposes accepted
  surface alpha and integrated volume optical depth before pixel reconstruction.
- Delivery is final, not progressive. Spill data is not a persistence/resume API.

## Tests

`src/app/deep_output_driver_test.cpp` uses an in-memory consumer. It covers sample
values/populations, coordinates, returned-data lifetime, changed dimensions and
sample counts, disabling deep, unsupported drivers, crop rejection, cancellation,
callback exceptions and reuse after failures. CPU/CUDA qualification is recorded
separately; an API test does not qualify every device/material combination.

[Architecture](ARCHITECTURE_REVIEW.md) | [Release status](../../DEEP_IMPLEMENTATION_STATUS.md)
