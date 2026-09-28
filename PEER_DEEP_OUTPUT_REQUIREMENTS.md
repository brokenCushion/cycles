# Peer requirements: native Cycles deep output

These are architectural constraints. Current delivery scope and remaining work
live in [M8/M9 milestones](DEEP_MILESTONES.md) and
[release status](DEEP_IMPLEMENTATION_STATUS.md).

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
