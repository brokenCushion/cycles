# Peer Review: Deep Output Requirements

The peer review calls for the deep-output implementation to fit naturally into Cycles, remain independent from Blender, and account for both CPU and GPU constraints.

## Keep Cycles Standalone

Do not introduce dependencies from Cycles into Blender-specific code. Blender and other host applications may use the feature through a general Cycles interface, but Cycles must remain buildable and usable independently.

```text
Blender or another host -> Cycles interface
Cycles core             -X-> Blender-specific code
```

## Follow the Existing Cycles Architecture

Cycles is relatively monolithic, so the feature does not need to be divided into many independent modules. It should instead look and behave like native Cycles functionality:

- Follow existing naming and code organization.
- Use established render, device, kernel, and buffer patterns.
- Place functionality alongside similar Cycles code.
- Avoid abstractions introduced only for the sake of modularity.
- Keep separate libraries only where they provide a practical benefit.

## Design for Devices, Not Only CPU

The feature should be designed around Cycles' device backends rather than as a permanently CPU-specific implementation. Relevant backends include CPU, CUDA, OptiX, HIP, Metal, and oneAPI.

Implementing CPU support first is acceptable, provided the interfaces and data model allow GPU implementations to be added without redesigning the feature.

## Respect GPU Kernel Constraints

GPU kernel code should:

- Avoid dynamic memory allocation, including `new`, `malloc`, and dynamically growing containers.
- Avoid ordinary STL containers, exceptions, file operations, locks, and host-only pointers.
- Use fixed-size or bounded data structures.
- Detect and report buffer overflow explicitly.
- Keep per-thread storage small to avoid register spilling and reduced occupancy.
- Minimize divergent execution between GPU threads.
- Minimize CPU/GPU synchronization and memory transfers.

Deep events should be written into preallocated device memory using bounded offsets or counters. A large event array should not automatically be allocated for every GPU thread because it could consume excessive local memory.

## Preallocate Deep-Data Storage

The host should allocate required memory before launching rendering kernels.

```text
Host allocates bounded deep buffers
                |
                v
Buffers are passed to the rendering device
                |
                v
Kernels write events into assigned locations
                |
                v
Overflow is detected and reported
                |
                v
Completed data is copied back to the host
                |
                v
Host reconstructs and writes the Deep EXR
```

The storage design should account for:

- Maximum events per sample or pixel.
- Total buffer capacity.
- Event offsets and counters.
- Atomic operations where required.
- Explicit overflow handling.
- Overall memory consumption.
- Efficient and preferably coalesced GPU access patterns.

## Follow Existing Device and Kernel Patterns

Use the mechanisms Cycles already provides for:

- Device memory allocation.
- Render buffers.
- Kernel arguments.
- Device queues.
- CPU and GPU kernel variants.
- Feature flags.
- Host/device data transfers.
- Cancellation and error reporting.
- Backend-specific compilation.

The implementation should look like part of Cycles rather than a separate rendering framework placed beside it.

## Separate Host and Kernel Responsibilities

Device kernels should collect only the rendering data required for deep output. They should not write files, access the filesystem, publish outputs, or depend on Blender or Gaffer.

Host-side code should handle:

- Validation.
- Device-buffer allocation.
- Copying results from the device.
- Deep-sample reconstruction.
- OpenEXR serialization.
- Atomic file publication.
- User-facing errors.

## Follow Output-Driver Conventions

Deep data should be exposed through a clear output interface. The standalone Cycles executable should not be the only component capable of receiving or writing deep output.

```text
Rendering kernels
        |
        v
Bounded deep-data buffers
        |
        v
Host-side reconstruction
        |
        v
Deep-capable output interface
        |
        v
Standalone app / Blender / another host
```

This lets another program integrate deep output without copying or modifying internal rendering logic.

## Use MoonRay as a Reference

Study MoonRay for useful concepts such as:

- Deep-event representation.
- Bounded storage.
- Sample reconstruction.
- Surface and volume handling.
- Output integration.
- Separation of CPU and GPU responsibilities.

Useful ideas should be adapted to Cycles' architecture rather than copied blindly.

## Use Gaffer Only for Validation

Gaffer is useful for opening generated Deep EXR files, checking depth cuts, inspecting samples, comparing reconstructions, and performing visual validation.

Gaffer should not become a runtime dependency, determine the internal architecture, be required to render deep output, or introduce Gaffer-specific code into Cycles.

## Avoid Blender-Specific Coupling

Do not repeat the earlier approach of placing Blender-specific integration inside Cycles. Cycles should expose general-purpose deep-output functionality, while Blender-specific UI, options, data conversion, and lifecycle management remain in Blender's integration layer.

## Implications for the Current Implementation

The current implementation is moving in the intended direction because:

- Reconstruction is independent of Blender.
- Gaffer is used for validation rather than at runtime.
- Event storage is bounded.
- Deep EXR writing occurs on the host.
- The feature is optional through build flags.

The main remaining architectural work is:

- Move beyond a CPU-specific capture path.
- Define backend-neutral deep-capture data and interfaces.
- Use Cycles device buffers for GPU storage.
- Design bounded GPU event allocation carefully.
- Avoid excessive per-thread event arrays.
- Add explicit device-buffer overflow handling.
- Connect deep results to a reusable output interface.
- Keep standalone command-line handling separate from the core feature.
- Implement and validate additional device backends.

## Summary

The peer review calls for deep output to be implemented as native, GPU-conscious Cycles functionality. It should use bounded, preallocated device memory; follow existing Cycles device, kernel, and output conventions; and keep Blender and Gaffer outside the renderer's core dependencies.

Modularity itself is not the goal. Correct integration with Cycles and efficient behavior across rendering devices are the priorities.
