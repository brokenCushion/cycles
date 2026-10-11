# Deep output architecture

Cycles remains usable without Blender or Gaffer. Kernels capture bounded
visibility records; the host reconstructs and publishes deep EXRs. Native beauty
stays separate. The [support matrix](SUPPORT.md) and [validation policy](VALIDATION.md)
define the accepted behavior.

## Code map

| Layer | Ownership |
| --- | --- |
| `src/session/deep.cpp` | Settings validation, material/scene support and host output capability. |
| `src/integrator/path_trace*` | Capture lifetime, bounded buffers, scheduling, readback and delivery. |
| `src/kernel/deep/` | Shared typed records and CPU/CUDA visibility collection. No allocation, files or host dependencies. |
| `src/deep/capture.*` | Completed camera-sample ledgers, spill storage and bounded reconstruction. |
| `src/deep/reconstruction.cpp`, `volume.cpp` | Surface and extinction-to-alpha reconstruction. |
| `src/session/output_driver.h` | Deep-capable host output interface with bounded per-pixel reads. |
| `src/deep/exr_writer.cpp`, `publication.cpp` | FLOAT conversion, error checks and atomic file replacement. |
| `src/app/deep_output.cpp` | Standalone writer adapter. Blender has its own host adapter. |

## Invariants

- Cycles builds and runs without Blender or Gaffer. Hosts use the output-driver
  interface; Gaffer only reviews generated data.
- Kernels collect typed surface alpha or volume optical depth. The host reconstructs
  Z/ZBack/A; those quantities are not interchangeable.
- Host code allocates device pools before dispatch using Cycles device buffers.
  GPU threads write bounded event planes and explicit completion/failure status.
- A completed miss contributes to sample normalization. Incomplete, failed or
  skipped work must not be silently interpreted as a miss.
- Record definitions and layout assertions live in `kernel/deep/types.h`.
  Inactive value fields must be zero. Spill files are process-local scratch,
  not a portable persistence or resume format. Capture tests cover these rules.
- Beauty ray, sample, lens and time semantics are preserved without advancing
  beauty RNG state for deep capture.
- Queue-ordered copies precede host consumption. Output starts only after sample
  completion; cancellation/failure prevents publication.
- Host output callbacks and pixel buffers have scoped lifetimes. Consumers that
  retain data must copy it. Adding virtual output methods requires a host rebuild.

## Fit into Cycles

- Keep Cycles buildable and usable without Blender or Gaffer. Host-specific UI,
  scene conversion and lifecycle code belong in the host integration layer.
- Follow existing naming, code placement, rendering, buffer, device and kernel
  conventions. Separate libraries need a practical benefit; modularity alone
  is not a goal.
- Use Cycles device allocation, queues, kernel arguments, feature flags,
  transfers, cancellation, errors and backend compilation mechanisms.
- Expose deep results through a reusable output-driver interface. Another host
  must not need to copy renderer internals or use the standalone command line.

## Respect device limits

- Design shared data and interfaces for CPU, CUDA, OptiX, HIP, Metal and oneAPI.
  Backends can arrive incrementally; CPU-first must not require a redesign for GPU.
- Allocate bounded device buffers on the host before kernel launch. Define
  per-sample/event limits, total capacity, offsets, counters and atomics where
  needed. Overflow must produce an explicit error, never dropped samples.
- No GPU-thread heap allocation, growing containers, ordinary STL, exceptions,
  filesystem access, locks or host-only pointers.
- Keep per-thread scratch small. Measure register spilling, local memory and
  occupancy; a large private event array is not a substitute for device storage.
- Prefer coalesced writes; minimize divergence, synchronization and transfers.
  Account for total memory, including compiler-generated local storage.

## Keep responsibilities clear

Kernels capture rendering data into preallocated buffers. Host code validates,
allocates, reads back, reconstructs, serializes OpenEXR, publishes atomically and
reports errors. Kernels must not write or publish files.

Gaffer is a validation tool for samples, reconstruction, depth cuts and visual
review. It must not become a render dependency or dictate Cycles internals.

Use MoonRay as a reference for event representation, bounded storage,
reconstruction, surface/volume handling and output integration. Adapt useful
ideas to Cycles rather than copying its architecture.

## Qualification

The regression checks independent oracles, output identity, CPU beauty,
beauty sources and GPU resource records. Gaffer is optional for review.
See the [regression guide](../../src/deep/README.md#regression-command),
[production evidence](evidence/README.md) and [archive index](ARCHIVED_REPORTS.md).

## Core footprint for rebases

The table records the qualified capture integrations. Shared deep headers remain
in `src/kernel/deep/`; paths are relative to the repository root.

| Files | Reason |
| --- | --- |
| `CMakeLists.txt`<br>`src/CMakeLists.txt`<br>`src/app/CMakeLists.txt`<br>`src/integrator/CMakeLists.txt`<br>`src/kernel/CMakeLists.txt`<br>`src/kernel/device/cuda/CMakeLists.txt`<br>`src/session/CMakeLists.txt` | Guarded build flags, source lists, CUDA deep entry point and nine CTests. |
| `src/app/cycles_standalone.cpp`<br>`src/app/cycles_xml.cpp` | CLI/preflight configuration and standalone fixture scene inputs. |
| `src/app/deep_output.cpp`<br>`src/app/deep_output.h`<br>`src/app/oiio_output_driver.cpp`<br>`src/app/oiio_output_driver.h` | Output-driver deep callback, diagnostic ledger and atomic EXR publication. |
| `src/app/deep_output_driver_test.cpp`<br>`src/app/deep_output_driver_test.xml`<br>`src/app/deep_output_driver_transparent_test.xml`<br>`src/app/deep_output_driver_volume_test.xml` | Host API/lifecycle/overflow fixtures, independent of Blender. |
| `src/device/cpu/kernel.cpp`<br>`src/device/cpu/kernel.h`<br>`src/device/cuda/device_impl.cpp`<br>`src/device/cuda/kernel.cpp`<br>`src/device/cuda/queue.cpp`<br>`src/device/cuda/queue.h`<br>`src/device/kernel.cpp`<br>`src/device/queue.cpp`<br>`src/device/queue.h` | CPU/CUDA entry dispatch, queue arguments, async transfers and kernel naming. |
| `src/device/optix/device_impl.cpp`<br>`src/device/optix/device_impl.h`<br>`src/device/optix/queue.cpp`<br>`src/device/optix/queue.h`<br>`src/kernel/device/optix/CMakeLists.txt`<br>`src/kernel/device/optix/kernel_deep.cu` | Isolated native SVM deep module/pipeline, bounded all-hit traversal and queue-owned launch snapshot. |
| `src/integrator/path_trace.cpp`<br>`src/integrator/path_trace.h`<br>`src/integrator/path_trace_deep_tile.h`<br>`src/integrator/path_trace_work.h`<br>`src/integrator/path_trace_work_cpu.cpp`<br>`src/integrator/path_trace_work_cpu.h`<br>`src/integrator/path_trace_work_gpu.cpp`<br>`src/integrator/path_trace_work_gpu.h` | Separate capture scheduling, bounded GPU batches/host spill and output-driver tile adapter. |
| `src/kernel/bvh/bvh.h`<br>`src/kernel/bvh/intersect_filter.h`<br>`src/kernel/bvh/volume_all.h`<br>`src/kernel/device/cpu/bvh.h` | Bounded deep intersection batches using existing BVH; default beauty traversal unchanged. |
| `src/kernel/device/cpu/kernel.h`<br>`src/kernel/device/cpu/kernel_arch.h`<br>`src/kernel/device/cpu/kernel_arch_impl.h`<br>`src/kernel/device/gpu/kernel.h` | Guarded CPU architecture dispatch and GPU deep kernel entry. |
| `src/kernel/integrator/surface_shader.h` | Template switch disables closure storage for deep opacity evaluation; beauty uses the original default. |
| `src/kernel/types.h` | KernelShader padding carries extinction constants without increasing its size; deep DeviceKernel enum. |
| `src/kernel/util/nanovdb.h` | Optional uniform-tile dimension accessor for deep grid traversal; ordinary reads preserve defaults. |
| `src/kernel/device/gpu/image.h`<br>`src/kernel/util/image_2d.h`<br>`src/kernel/svm/svm.h` | Guarded deterministic deep texture/attribute evaluation; native beauty specializations remain unchanged. |
| `src/kernel/device/optix/kernel_deep_osl.cu`<br>`src/kernel/device/optix/kernel_deep_osl_services.cu`<br>`src/kernel/osl/closures.cpp`<br>`src/kernel/osl/services.cpp` | Separate deep OSL callables/services, initialized volume evaluation and scalar extinction extraction. |
| `src/scene/osl.cpp`<br>`src/scene/osl.h` | Guarded shader-group feature queries for explicit deep preflight. |
| `src/scene/object.cpp`<br>`src/scene/shader.cpp`<br>`src/scene/shader.h` | Object primitive ranges and preflight extinction constants uploaded for separate capture. |
| `src/session/deep.cpp`<br>`src/session/deep.h`<br>`src/session/output_driver.h`<br>`src/session/session.cpp`<br>`src/session/session.h` | Public settings/preflight, camera/data contract and deep output-driver callback lifecycle. |

Blender-specific properties, synchronization, sample-count pass and output-driver
plumbing live in `tools/prepare_blender_deep.py`, outside the standalone renderer.

The host timing fix also adds guarded clock accounting in
`src/integrator/render_scheduler.h`, alongside the path-trace integration.
