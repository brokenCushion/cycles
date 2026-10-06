"""Runnable identity check: run metadata may differ; data/descriptive headers may not."""
from pathlib import Path
import struct
import tempfile
from compare_deep_identity import compare


def fixture(path, pairing=b'run-a', error=b'0.001', pixels=b'alpha', counts=b'counts',
            compression=b'\x02', extra=b''):
    attributes = [('type', 'string', b'deepscanline'), ('chunkCount', 'int', struct.pack('<i', 1)),
                  ('compression', 'compression', compression), ('cycles:deepError', 'string', error),
                  ('cycles:beautyIdentity', 'string', pairing)]
    header = struct.pack('<II', 20000630, 0x802)
    for name, kind, value in attributes:
        header += name.encode()+b'\0'+kind.encode()+b'\0'+struct.pack('<I',len(value))+value
    header += b'\0'
    chunk = struct.pack('<iQQQ',0,len(counts),len(pixels),len(pixels))+counts+pixels
    path.write_bytes(header+struct.pack('<Q',len(header)+8)+chunk+extra)


with tempfile.TemporaryDirectory() as directory:
    a, b = [Path(directory)/name for name in ('a.exr','b.exr')]
    fixture(a)
    fixture(b, pairing=b'different path, timestamp-dependent checksum')
    assert compare(a,b)['passed']
    for change in ({'error':b'0.002'}, {'pixels':b'other'}, {'counts':b'other'}, {'compression':b'\x00'}):
        fixture(b, **change)
        assert not compare(a,b)['passed'], change
    fixture(b, extra=b'unreferenced')
    try:
        compare(a,b)
    except ValueError:
        pass
    else:
        raise AssertionError('Malformed payload accepted')
print('PASS run metadata exclusion; error/compression/count/pixel changes and malformed files rejected')
