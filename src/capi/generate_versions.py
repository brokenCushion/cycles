"""Freeze dependency version resources from CMake's actual bundled DLL list."""
import ctypes as C
import json
import sys
from pathlib import Path


def version(path):
    api = C.WinDLL('version')
    api.GetFileVersionInfoSizeW.argtypes = [C.c_wchar_p, C.POINTER(C.c_uint32)]
    api.GetFileVersionInfoW.argtypes = [C.c_wchar_p, C.c_uint32, C.c_uint32, C.c_void_p]
    api.VerQueryValueW.argtypes = [C.c_void_p, C.c_wchar_p, C.POINTER(C.c_void_p), C.POINTER(C.c_uint32)]
    unused = C.c_uint32()
    size = api.GetFileVersionInfoSizeW(str(path), C.byref(unused))
    if not size:
        return ''
    data = C.create_string_buffer(size)
    assert api.GetFileVersionInfoW(str(path), 0, size, data)
    pointer, length = C.c_void_p(), C.c_uint32()
    assert api.VerQueryValueW(data, '\\', C.byref(pointer), C.byref(length)) and length.value >= 52
    words = C.cast(pointer, C.POINTER(C.c_uint32))
    assert words[0] == 0xfeef04bd
    return '.'.join(map(str, (words[2] >> 16, words[2] & 65535, words[3] >> 16, words[3] & 65535)))


def main():
    paths, output = map(Path, sys.argv[1:])
    versions = {}
    for text in paths.read_text(encoding='utf-8').splitlines():
        p = Path(text)
        if p.suffix.lower() != '.dll':
            continue
        value = version(p)
        key = p.name.lower()
        assert key not in versions or versions[key] == value, ('Conflicting dependency versions', key)
        versions[key] = value
    assert versions
    code = ['/* Generated from build dependency version resources. */',
            'static const struct { const char *name, *version; } build_versions[] = {']
    code += ['  {' + json.dumps(k) + ', ' + json.dumps(v) + '},' for k, v in sorted(versions.items())]
    code += ['};']
    output.write_text('\n'.join(code)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
