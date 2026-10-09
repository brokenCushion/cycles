"""Compare PTX while ignoring comments, debug locations and virtual-register names.

Instructions, constants, symbols, labels, register declarations and their order
remain checked. Registers are renamed bijectively by first occurrence per family;
special hardware registers and quoted data are preserved. No code is reordered.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re


def normalize(text):
    names = {}
    counts = {}

    def token(match):
        value = match.group()
        if value.startswith(('//', '/*')):
            return ' '
        if value.startswith('"'):
            return value
        family = re.match(r'%([a-z]+)', value)[1]
        if value not in names:
            index = counts.get(family, 0)
            counts[family] = index + 1
            names[value] = f'%{family}_canonical_{index}'
        return names[value]

    text = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|%(?:rd|rs|fd|r|f|p|h|b|d)\d+\b', token, text)
    return '\n'.join(line.strip() for line in text.splitlines()
                     if line.strip() and not re.match(r'\s*\.(?:file|loc)\b', line))


def read_ptx(path, dll=None):
    data = path.read_bytes()
    if path.suffix != '.zst':
        return data.decode()
    assert dll, 'Compressed PTX requires --zstd-dll from the installed runtime'
    z = ctypes.CDLL(str(dll))
    z.ZSTD_getFrameContentSize.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    z.ZSTD_getFrameContentSize.restype = ctypes.c_ulonglong
    z.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    z.ZSTD_decompress.restype = ctypes.c_size_t
    z.ZSTD_isError.argtypes = [ctypes.c_size_t]
    z.ZSTD_isError.restype = ctypes.c_uint
    size = z.ZSTD_getFrameContentSize(data, len(data))
    assert size < 200_000_000, 'Unknown or excessive PTX frame size'
    buffer = ctypes.create_string_buffer(size)
    result = z.ZSTD_decompress(buffer, size, data, len(data))
    assert not z.ZSTD_isError(result) and result == size, 'PTX decompression failed'
    return buffer.raw[:result].decode()


def compare(a, b, dll=None):
    texts = [read_ptx(p, dll) for p in (a, b)]
    return dict(passed=normalize(texts[0]) == normalize(texts[1]),
                ptx_identical=texts[0] == texts[1],
                ptx_sha256=[hashlib.sha256(t.encode()).hexdigest() for t in texts],
                normalized_sha256=[hashlib.sha256(normalize(t).encode()).hexdigest() for t in texts])


def self_check():
    a = '.reg .f32 %f<20>;\nmov.f32 %f12, 0f00000000;\nmov.f32 %f13, %f12;'
    b = '// build stamp\n.file 1 "C:/old/path.cu"\n.loc 1 8 0\n' + a.replace('%f12', '%f18')
    assert normalize(a) == normalize(b)
    assert normalize(a) != normalize(b.replace('0f00000000', '0f3F800000'))
    assert normalize(a) != normalize(b.replace('%f18;', '%f13;'))
    assert normalize(a) != normalize(a.replace('mov.f32', 'add.f32', 1))
    assert normalize('.ascii "%f12 // data";') != normalize('.ascii "%f18 // data";')
    assert normalize('mov.u64 %rd1, %clock64;') != normalize('mov.u64 %rd1, %clock;')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('old', type=Path, nargs='?')
    parser.add_argument('new', type=Path, nargs='?')
    parser.add_argument('--zstd-dll', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    self_check()
    if args.old is None and args.new is None:
        print('PASS PTX normalization self-check')
    else:
        assert args.old is not None and args.new is not None
        report = compare(args.old, args.new, args.zstd_dll)
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        raise SystemExit(0 if report['passed'] else 1)
