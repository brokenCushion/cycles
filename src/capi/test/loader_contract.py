"""Reject malformed private PE images before any executable mapping occurs."""
import ctypes as C
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path


class Init(C.Structure):
    _fields_ = [('size', C.c_uint32), ('resource', C.c_char_p), ('user', C.c_char_p)]


def child(directory):
    api = C.CDLL(str(directory/'cycles_c.dll'))
    api.ccl_last_error.restype = C.c_char_p
    api.ccl_init.argtypes = [C.POINTER(Init)]
    params = Init(C.sizeof(Init), str(directory).encode(), str(directory).encode())
    status = api.ccl_init(C.byref(params))
    error = api.ccl_last_error().decode()
    assert status == 5 and error, (status, error)
    print(json.dumps({'status': status, 'error': error}))


def main():
    if sys.argv[1] == '--child':
        return child(Path(sys.argv[2]))
    install, output = map(Path, sys.argv[1:])
    assert not output.exists(), 'Use a fresh test output directory'
    output.mkdir(parents=True)
    invalid_offset = bytearray(64)
    struct.pack_into('<H', invalid_offset, 0, 0x5a4d)
    struct.pack_into('<i', invalid_offset, 60, 0x7fffffff)
    cases = {'empty': b'', 'bad_magic': bytes(128), 'out_of_bounds': invalid_offset}
    results = {}
    for name, data in cases.items():
        folder = output/name
        folder.mkdir()
        shutil.copy2(install/'cycles_c.dll', folder/'cycles_c.dll')
        (folder/'cycles_c_backend_private.dll').write_bytes(data)
        process = subprocess.run([sys.executable, __file__, '--child', str(folder)],
                                 check=True, capture_output=True, text=True, timeout=30)
        results[name] = json.loads(process.stdout)
    (output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    print('PASS all malformed PE images rejected safely')


if __name__ == '__main__':
    main()
