# SPDX-License-Identifier: Apache-2.0
"""Strict single-part deep scanline EXR identity, except named run metadata.

Compare encoded count/pixel chunks (all channels, including future UINT ids).
Absolute chunk offsets are file layout, not data; they shift with header lengths.
Unknown attributes remain deterministic until explicitly classified otherwise.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

RUN_METADATA = frozenset(('cycles:beautyIdentity', 'capDate', 'DateTime', 'filePath',
                         'cycles:runId', 'cycles:sourcePath', 'cycles:outputPath'))


def identity(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        size = stream.seek(0, 2)
        stream.seek(0)

        def read(n):
            if n < 0 or n > size - stream.tell():
                raise ValueError('Truncated EXR: ' + str(path))
            value = stream.read(n)
            if len(value) != n:
                raise ValueError('Truncated EXR: ' + str(path))
            return value

        def string():
            value = bytearray()
            while True:
                byte = read(1)
                if byte == b'\0':
                    return bytes(value)
                value.extend(byte)
                if len(value) > 255:
                    raise ValueError('Invalid EXR attribute name/type')

        prefix = read(8)
        magic, version = struct.unpack('<II', prefix)
        if magic != 20000630 or not version & 0x800 or version & (0x200 | 0x1000):
            raise ValueError('Expected single-part deep scanline EXR')
        digest.update(prefix)
        attributes = {}
        while True:
            name = string().decode('utf-8')
            if not name:
                break
            kind = string()
            value = read(struct.unpack('<I', read(4))[0])
            if name in attributes:
                raise ValueError('Duplicate EXR attribute')
            attributes[name] = (kind, value)
        if attributes.get('type') != (b'string', b'deepscanline'):
            raise ValueError('Expected deep scanline data')
        kind, count = attributes['chunkCount']
        if kind != b'int' or len(count) != 4:
            raise ValueError('Invalid chunkCount')
        count = struct.unpack('<i', count)[0]
        if count <= 0 or count > (size - stream.tell()) // 8:
            raise ValueError('Invalid EXR chunk table')
        offsets = struct.unpack('<' + 'Q' * count, read(count * 8))
        data_start = stream.tell()
        if len(set(offsets)) != count:
            raise ValueError('Duplicate EXR chunk offset')
        deterministic = {k: v for k, v in attributes.items() if k not in RUN_METADATA}
        for name, (kind, value) in deterministic.items():
            digest.update(name.encode() + b'\0' + kind + b'\0' + struct.pack('<I', len(value)) + value)
        ranges = []
        for offset in offsets:
            if not data_start <= offset <= size - 28:
                raise ValueError('Invalid EXR chunk offset')
            stream.seek(offset)
            chunk = read(28)
            _, packed_counts, packed_pixels, _ = struct.unpack('<iQQQ', chunk)
            digest.update(chunk)
            remaining = packed_counts + packed_pixels
            while remaining:
                n = min(remaining, 1024 * 1024)
                digest.update(read(n))
                remaining -= n
            ranges.append((offset, stream.tell()))
        end = data_start
        for start, stop in sorted(ranges):
            if start != end:
                raise ValueError('Unreferenced or overlapping EXR chunk bytes')
            end = stop
        if end != size:
            raise ValueError('Unreferenced EXR trailing bytes')
        return digest.hexdigest(), attributes


def compare(before, after):
    a, ha = identity(before)
    b, hb = identity(after)
    changed = sorted(k for k in ha.keys() | hb.keys() if ha.get(k) != hb.get(k))
    return dict(passed=a == b, before=a, after=b,
                changed_deterministic_attributes=[k for k in changed if k not in RUN_METADATA],
                changed_run_metadata=[k for k in changed if k in RUN_METADATA])


if __name__ == '__main__':
    result = compare(*sys.argv[1:3])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
