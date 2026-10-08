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


def toolchain_difference(before, after, mode='strict'):
    """Audit physical curves rather than pairing differently partitioned records.

    Strict has a 1e-6 cross-build ceiling. Numeric approximations are checked
    against their own oracles; their cross-build difference is informational.
    This comparison never replaces an independent oracle check.
    """
    import numpy as np
    from deep_exr import read, pixel, bound
    from compare_deep_ids import curve_error, flattened_transmittance
    result = compare(before, after)
    a, b = read(before), read(after)
    ca, cb = a.channels(), b.channels()
    if ca.keys() != cb.keys() or ca['A'].pixels.shape != cb['A'].pixels.shape:
        raise ValueError('Toolchain audit channel/image layout differs')
    h, w = cb['A'].pixels.shape
    totals = [0, 0]; changed = 0; count_changed = 0; deltas = []
    maximum = 0.; flat = 0.; worst = None
    for y in range(h):
        for x in range(w):
            lengths = [0 if c['A'].pixels[y,x] is None else len(c['A'].pixels[y,x]) for c in (ca, cb)]
            for i in (0, 1): totals[i] += lengths[i]
            delta = lengths[1]-lengths[0]; deltas.append(delta)
            count_changed += delta != 0
            def values(c, name):
                v = c[name].pixels[y,x]
                return np.empty(0, dtype=np.uint32) if v is None else v.view(np.uint32)
            if all(np.array_equal(values(ca, n), values(cb, n)) for n in ca):
                continue
            changed += 1
            left, right = pixel(a,x,y), pixel(b,x,y)
            error = curve_error(left, right)
            if error > maximum: maximum, worst = error, [x,y]
            flat = max(flat, abs(flattened_transmittance(left)-flattened_transmittance(right)))
    result.update(samples_before=totals[0], samples_after=totals[1],
                  changed_pixels=changed, count_changed_pixels=count_changed,
                  count_delta_min=min(deltas), count_delta_max=max(deltas),
                  max_transmittance_difference=maximum, max_flattened_alpha_difference=flat,
                  worst_pixel=worst, header_bound=bound(b),
                  difference_limit=1e-6 if mode=='strict' else None,
                  triangle_inequality_bound=bound(a)+bound(b),
                  audit_passed=not result['changed_deterministic_attributes'] and (mode!='strict' or maximum<=1e-6))
    return result


if __name__ == '__main__':
    result = compare(*sys.argv[1:3])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
