# CUDA surface validation

Historical M6 correctness evidence on RTX 3080 / CUDA 12.8.93 (`sm_86`).
Current scope: [release status](../../DEEP_IMPLEMENTATION_STATUS.md).
Current memory/resource audit: [CUDA storage](CUDA_STORAGE_VALIDATION.md).

## Contract

A separate deep kernel traces camera visibility using private path state and
unique, bounded lane slots. Host code drains records into bounded capture/spill
storage. No GPU-thread heap allocation or append truncation. Overflow, cache
failure, unsupported primitives and cancellation invalidate output.

Native SVM is qualified; GPU OSL is not. Later adaptive, DOF, motion and volume
work has separate validation and is not established by these surface fixtures.

## Evidence

`validate_cuda_gaffer.py` compares ten scenes with CPU: stacks, diffuse, opaque
backdrop, misses, clear surfaces, shader cutoff, beauty bounce limits, opaque
capture, checker cutouts and image textures. Each has 3,072 camera identities.

- Maximum CPU/CUDA depth difference: 1.908e-6; local alpha: 5.961e-8.
- CUDA export versus raw capture: 2.608e-8; beauty on/off: 3.726e-9.
- Overflow, coloured transparency and GPU OSL reject and preserve prior output.
  Exactly full capture succeeds, including clear-surface capacity accounting.
- The 64-layer stress fixture passes at 16x12 / 16 samples.
- The 640x480 / 16-sample primitive review captures 4,915,200 identities and
  passes eight Gaffer cuts within 5.961e-8, with one million preview points.

Historical artifacts: `builds/build-m6/acceptance`, `acceptance-layers` and
`primitives-review`. Timings overlapped other work; they are not speedup claims.

## Resource caveat

The original enabled/disabled cubin comparison retained identical registers,
shared memory and driver occupancy recommendations for 75 common kernels.
One surface kernel used 64 extra local bytes; zero disabled-feature cost was
not proven. Cubin size is not resident GPU memory, and staging capacity is not
total working memory. Later resource measurements supersede the original deep
kernel's 6,720 local bytes. Production throughput/VRAM qualification remains M8.

Run the validator with the current installed executable in the CUDA environment
and a fresh output directory. Build commands: [BUILDING.md](../../BUILDING.md).
