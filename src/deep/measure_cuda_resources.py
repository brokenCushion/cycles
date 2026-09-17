# SPDX-License-Identifier: Apache-2.0
"""Compare CUDA driver-reported kernel resources for deep-on/off cubins.

Windows Python: ON_CUBIN OFF_CUBIN OFF_PTXAS_LOG OUTPUT_JSON.
Occupancy is the driver's theoretical recommendation, not measured utilization.
"""
import ctypes as c
import json
from pathlib import Path
import re
import sys

on, off, log, output = map(Path, sys.argv[1:])
cuda = c.WinDLL('nvcuda.dll')
pointer = c.c_void_p

def call(name, *args):
    code = getattr(cuda, name)(*args)
    if code:
        raise RuntimeError(f'{name}: CUDA error {code}')

call('cuInit', 0)
device = c.c_int()
call('cuDeviceGet', c.byref(device), 0)
context = pointer()
call('cuCtxCreate_v2', c.byref(context), 0, device)
names = sorted(set(re.findall(r"Compiling entry function '([^']+)'", log.read_text())))
if not names:
    raise RuntimeError('No kernel names in ptxas log')

def inspect(path, names):
    module = pointer()
    call('cuModuleLoad', c.byref(module), str(path.resolve()).encode())
    result = {}
    for name in names:
        function = pointer()
        call('cuModuleGetFunction', c.byref(function), module, name.encode())
        attributes = {}
        for label, attribute in (('max_threads', 0), ('shared_bytes', 1),
                                  ('local_bytes', 3), ('registers', 4)):
            value = c.c_int()
            call('cuFuncGetAttribute', c.byref(value), attribute, function)
            attributes[label] = value.value
        call('cuFuncSetCacheConfig', function, 2)  # PREFER_L1, as Cycles does.
        blocks, threads = c.c_int(), c.c_int()
        call('cuOccupancyMaxPotentialBlockSize', c.byref(blocks), c.byref(threads),
             function, pointer(), c.c_size_t(0), 0)
        attributes.update(min_grid_blocks=blocks.value, recommended_block_threads=threads.value)
        result[name] = attributes
    call('cuModuleUnload', module)
    return result

try:
    disabled = inspect(off, names)
    enabled = inspect(on, names + ['kernel_gpu_deep_surface'])
    changed = [name for name in names if disabled[name] != enabled[name]]
    report = {'common_kernel_count': len(names), 'changed_common_kernels': changed,
              'deep_on_cubin_bytes': on.stat().st_size, 'deep_off_cubin_bytes': off.stat().st_size,
              'on': enabled, 'off': disabled}
    output.write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if k not in ('on', 'off')}, indent=2))
    print('Deep kernel:', enabled['kernel_gpu_deep_surface'])
finally:
    call('cuCtxDestroy_v2', context)
