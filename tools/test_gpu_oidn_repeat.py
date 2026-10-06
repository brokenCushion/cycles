"""Gaffer Python: denoise identical saved passes twice with installed CUDA OIDN.

Usage: gaffer env python SCRIPT INPUT_EXR OIDN_DLL OUTPUT_DIRECTORY
No rendering, CPU fallback or changes to the input file.
"""
import array
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import sys

import GafferImage
import imath

source, dll, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
sha = hashlib.sha256(source.read_bytes()).hexdigest()
reader = GafferImage.ImageReader()
reader['fileName'].setValue(source.resolve().as_posix())
image = reader['out']
fmt = image['format'].getValue()
width, height = fmt.width(), fmt.height()
names = list(image.channelNames())
inputs = {}
tile_size = GafferImage.ImagePlug.tileSize()
for label, pass_name in [('color', 'Noisy Image'), ('albedo', 'Denoising Albedo'),
                         ('normal', 'Denoising Normal')]:
    channels = [next(c for c in names if f'.{pass_name}.' in c and c.endswith('.'+axis))
                for axis in ('XYZ' if label == 'normal' else 'RGB')]
    data = array.array('f')
    for y in range(height):
        for x in range(width):
            origin = imath.V2i(x//tile_size*tile_size, y//tile_size*tile_size)
            index = (y-origin.y)*tile_size + x-origin.x
            data.extend(float(image.channelData(c, origin)[index]) for c in channels)
    inputs[label] = data

with os.add_dll_directory(str(dll.resolve().parent)):
    oidn = C.CDLL(str(dll.resolve()))
    def api(name, result, *args):
        f = getattr(oidn, name)
        f.restype, f.argtypes = result, args
        return f
    pointer, size, string = C.c_void_p, C.c_size_t, C.c_char_p
    number = api('oidnGetNumPhysicalDevices', C.c_int)
    kind = api('oidnGetPhysicalDeviceInt', C.c_int, C.c_int, string)
    name = api('oidnGetPhysicalDeviceString', string, C.c_int, string)
    ids = [i for i in range(number()) if kind(i, b'type') == 3]  # CUDA only.
    assert ids, 'No CUDA OIDN device; CPU fallback prohibited'
    new_device = api('oidnNewDeviceByID', pointer, C.c_int)
    commit_device = api('oidnCommitDevice', None, pointer)
    release_device = api('oidnReleaseDevice', None, pointer)
    get_error = api('oidnGetDeviceError', C.c_int, pointer, C.POINTER(string))
    new_buffer = api('oidnNewBuffer', pointer, pointer, size)
    write_buffer = api('oidnWriteBuffer', None, pointer, size, size, pointer)
    read_buffer = api('oidnReadBuffer', None, pointer, size, size, pointer)
    release_buffer = api('oidnReleaseBuffer', None, pointer)
    new_filter = api('oidnNewFilter', pointer, pointer, string)
    set_image = api('oidnSetFilterImage', None, pointer, string, pointer,
                    C.c_int, size, size, size, size, size)
    set_int = api('oidnSetFilterInt', None, pointer, string, C.c_int)
    set_bool = api('oidnSetFilterBool', None, pointer, string, C.c_bool)
    commit_filter = api('oidnCommitFilter', None, pointer)
    execute_filter = api('oidnExecuteFilter', None, pointer)
    release_filter = api('oidnReleaseFilter', None, pointer)
    results = []
    for run in (1, 2):
        device = new_device(ids[0])
        commit_device(device)
        def check():
            error = string()
            code = get_error(device, C.byref(error))
            assert code == 0, (code, error.value)
        check()
        buffers = {}
        byte_count = width*height*3*4
        for label, data in inputs.items():
            buffers[label] = new_buffer(device, byte_count)
            host = (C.c_float * len(data)).from_buffer(data)
            write_buffer(buffers[label], 0, byte_count, host)
        buffers['output'] = new_buffer(device, byte_count)
        def filter(labels, auxiliary=False):
            f = new_filter(device, b'RT')
            set_int(f, b'quality', 6)  # HIGH, as in Cycles.
            set_bool(f, b'srgb', False)
            if not auxiliary:
                set_bool(f, b'hdr', True)
                set_int(f, b'cleanAux', 1)
            for label, buffer in labels.items():
                set_image(f, label.encode(), buffer, 3, width, height, 0, 0, 0) # FLOAT3
            commit_filter(f)
            execute_filter(f)
            check()
            release_filter(f)
        for label in ('albedo', 'normal'):  # Cycles ACCURATE prefilter.
            filter({label:buffers[label], 'output':buffers[label]}, auxiliary=True)
        filter(buffers)
        data = array.array('f', [0]) * (width*height*3)
        host = (C.c_float * len(data)).from_buffer(data)
        read_buffer(buffers['output'], 0, byte_count, host)
        check()
        (output / f'oidn-{run}.float3').write_bytes(data.tobytes())
        results.append(data)
        for buffer in buffers.values():
            release_buffer(buffer)
        release_device(device)
    assert sha == hashlib.sha256(source.read_bytes()).hexdigest()
    report = dict(input=str(source.resolve()), input_sha256=sha, dll_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                  device=name(ids[0], b'name').decode(), device_type='CUDA', cpu_fallback=False,
                  quality='HIGH', prefilter='ACCURATE', width=width, height=height,
                  max_output_difference=max(abs(a-b) for a,b in zip(*results)),
                  output_identical=results[0].tobytes() == results[1].tobytes())
    (output / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
