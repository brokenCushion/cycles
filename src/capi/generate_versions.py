"""Freeze dependency version resources from CMake's actual bundled DLL list."""
import ctypes as C
import json
import os
import re
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
    runtime = re.compile(r'^(?:vcruntime140.*|msvcp140.*|concrt140|vcomp140)\.dll$')
    redist = os.environ.get('VCToolsRedistDir')
    if not redist:
        raise RuntimeError('VCToolsRedistDir required to record backend runtime minimum')
    toolchain = {str(p.resolve()): version(p) for p in (Path(redist)/'x64').rglob('*.dll')
                 if runtime.match(p.name.lower())}
    assert toolchain, 'No toolchain x64 Microsoft runtime DLLs found'
    package = {k: v for k, v in versions.items() if runtime.match(k)}
    values = list(toolchain.values()) + list(package.values())
    assert all(v and v.split('.')[0] == '14' for v in values), values
    minimum = max(values, key=lambda v: tuple(map(int, v.split('.'))))
    output.with_suffix('.json').write_text(json.dumps(dict(
        microsoft_runtime_minimum=minimum, toolchain=toolchain, package=package,
        renderer_versions=versions), indent=2)+'\n', encoding='utf-8')
    code = ['/* Generated from build dependency version resources. */',
            'static const char *microsoft_runtime_minimum = '+json.dumps(minimum)+';',
            'static const struct { const char *name, *version; } build_versions[] = {']
    code += ['  {' + json.dumps(k) + ', ' + json.dumps(v) + '},' for k, v in sorted(versions.items())]
    code += ['};']
    output.write_text('\n'.join(code)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
