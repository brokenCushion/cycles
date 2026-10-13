"""Read-only strict-identity diagnosis, preserving every per-pixel result."""
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO
sys.path.insert(0, str(REPO / 'tools'))
from compare_deep_identity import compare, identity, RUN_METADATA
from deep_exr import read
import numpy as np


def chunks(path):
    data = Path(path).read_bytes()
    pos = 8
    def string():
        nonlocal pos
        end = data.index(b'\0', pos)
        result = data[pos:end]
        pos = end + 1
        return result
    attributes = {}
    while name := string():
        kind = string()
        length, = struct.unpack_from('<I', data, pos)
        pos += 4
        attributes[name] = (kind, data[pos:pos + length])
        pos += length
    count, = struct.unpack('<i', attributes[b'chunkCount'][1])
    offsets = struct.unpack_from('<' + 'Q' * count, data, pos)
    result = []
    for offset in offsets:
        y, counts, pixels, unpacked = struct.unpack_from('<iQQQ', data, offset)
        result.append(dict(y=y, offset=offset, packed_counts=counts, packed_pixels=pixels,
                           unpacked_pixels=unpacked,
                           counts_sha256=hashlib.sha256(data[offset+28:offset+28+counts]).hexdigest(),
                           pixels_sha256=hashlib.sha256(data[offset+28+counts:offset+28+counts+pixels]).hexdigest()))
    return dict(compression=attributes[b'compression'][1][0], chunks=result)


def diagnose(left, right):
    report = compare(left, right)
    _, ha = identity(left)
    _, hb = identity(right)
    a, b = read(left), read(right)
    report['header_differences'] = []
    for name in sorted(ha.keys() | hb.keys()):
        if ha.get(name) != hb.get(name):
            report['header_differences'].append(dict(name=name,
                classification='run-specific' if name in RUN_METADATA else 'deterministic',
                before=repr(a.header().get(name)), after=repr(b.header().get(name)),
                before_encoded=repr(ha.get(name)), after_encoded=repr(hb.get(name))))
    ca, cb = a.channels(), b.channels()
    assert ca.keys() == cb.keys()
    report['channels'] = sorted(ca)
    report['layouts'] = [chunks(left), chunks(right)]
    h, w = ca['A'].pixels.shape
    rows = []
    def values(c, name, y, x):
        v = c[name].pixels[y, x]
        return np.empty(0, dtype=np.float32) if v is None else v
    for y in range(h):
        for x in range(w):
            va, vb = values(ca, 'A', y, x), values(cb, 'A', y, x)
            eq = {n: values(ca,n,y,x).tobytes() == values(cb,n,y,x).tobytes() for n in ca}
            row = dict(x=x,y=y,counts=[len(va),len(vb)],equal=eq)
            if not all(eq.values()):
                row['values']={n:[values(ca,n,y,x).tolist(),values(cb,n,y,x).tolist()]
                               for n in ca if not eq[n]}
            rows.append(row)
    report['pixels'] = rows
    report['summary'] = dict(pixels=w*h, counts_equal=all(r['counts'][0]==r['counts'][1] for r in rows),
        total_samples=[sum(r['counts'][i] for r in rows) for i in (0,1)],
        unequal_pixels={n:sum(not r['equal'][n] for r in rows) for n in ca})
    return report


if __name__ == '__main__':
    result = diagnose(sys.argv[1], sys.argv[2])
    output(sys.argv[3]).write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('pixels','layouts')}, indent=2))
