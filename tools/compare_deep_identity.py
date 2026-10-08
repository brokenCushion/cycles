# SPDX-License-Identifier: Apache-2.0
"""Strict single-part deep scanline EXR identity, except named run metadata.

Compare encoded count/pixel chunks (all channels, including future UINT ids).
Absolute chunk offsets are file layout, not data; they shift with header lengths.
Unknown attributes remain deterministic until explicitly classified otherwise.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

RUN_METADATA = frozenset(('cycles:beautyIdentity', 'capDate', 'DateTime', 'filePath',
                         'cycles:runId', 'cycles:sourcePath', 'cycles:outputPath'))
CROSS_ALPHA_LIMIT = 1e-4


def cross_comparison_passed(max_flattened_alpha_difference):
    """Cross-build/backend sanity only; each side must qualify independently."""
    return bool(math.isfinite(max_flattened_alpha_difference) and
                0 <= max_flattened_alpha_difference <= CROSS_ALPHA_LIMIT)


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


def depth_window_error(old, new, ulps):
    """Maximum violation of the monotone old-curve depth envelope.

    FLOAT spacing is constant between binade rounding boundaries. Within each
    region, split at new knots and old knots shifted by +/- d. Both one-sided
    limits and the analytic stationary points of exponential differences cover
    the entire interval, rather than only a sampled set of cuts.
    """
    import numpy as np
    source=str(Path(__file__).resolve().parents[1]/'src/deep')
    if source not in sys.path:sys.path.insert(0,source)
    from validate_volume_camera_curves import curve
    from cuda_beauty_gate import float32_ulp
    def make(samples):
        return curve([(a,b,-math.log1p(-v)) for a,b,v in samples if a<b],
                     [(a,v) for a,b,v in samples if a==b])
    before, ends, steps=make(old);after, new_ends, new_steps=make(new)
    old_knots=np.unique(np.concatenate((ends,steps)))
    new_knots=np.unique(np.concatenate((new_ends,new_steps)))
    knots=np.union1d(old_knots,new_knots)
    if not len(knots):return 0.
    def spacing(z):
        return float32_ulp(float(np.float32(z)))
    lo=max(0.,float(knots[0])-ulps*spacing(knots[0]))
    hi=float(knots[-1])+ulps*spacing(knots[-1])
    # Nearest-FLOAT binade changes occur half a previous ULP below powers of two.
    bins=[lo,hi]
    for exponent in range(-126,128):
        boundary=math.ldexp(1.,exponent)-math.ldexp(1.,max(-149,exponent-24))/2
        if lo<boundary<hi:bins.append(boundary)
    bins.sort();maximum=0.
    for lo,hi in zip(bins,bins[1:]):
        d=ulps*spacing((lo+hi)/2)
        points=np.unique(np.concatenate(([lo,hi],new_knots,old_knots-d,old_knots+d)))
        points=points[(points>=lo)&(points<=hi)]
        lower,upper=before(points+d),before(points-d,True)
        for left_limit in (False,True):
            values=after(points,left_limit)
            maximum=max(maximum,float(np.max(values-upper)),float(np.max(lower-values)))
        width=np.diff(points)
        n0,n1=after(points[:-1]),after(points[1:],True)
        for offset,sign in ((-d,1),(d,-1)):
            o0,o1=before(points[:-1]+offset),before(points[1:]+offset,True)
            active=(n0>0)&(n1>0)&(o0>0)&(o1>0)&(width>0)
            rn=np.zeros_like(width);ro=np.zeros_like(width)
            rn[active]=np.log(n0[active]/n1[active])/width[active]
            ro[active]=np.log(o0[active]/o1[active])/width[active]
            active&=(rn>0)&(ro>0)&(rn!=ro)
            root=np.zeros_like(width)
            root[active]=(np.log(rn[active])+np.log(n0[active])-np.log(ro[active])-np.log(o0[active]))/(rn[active]-ro[active])
            active&=(root>0)&(root<width)
            if np.any(active):
                violation=sign*(n0[active]*np.exp(-rn[active]*root[active])-o0[active]*np.exp(-ro[active]*root[active]))
                maximum=max(maximum,float(np.max(violation)))
    return maximum


def toolchain_difference(before, after, mode='strict'):
    """Audit physical curves rather than pairing differently partitioned records.

    Across a recorded toolchain change, only flattened alpha has a sanity bound.
    Curve differences and depth windows are informational; both builds must pass
    their own independent oracles. Fixed-toolchain identity is unchanged.
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
    maximum = 0.; flat = 0.; worst = None;window_maximum=0.;shift=0;shift_failed=False
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
            if mode=='strict':
                window=error
                if error>1e-6:
                    for needed in range(1,5):
                        window=depth_window_error(left,right,needed)
                        if window<=1e-6:
                            shift=max(shift,needed);break
                    else:shift_failed=True
                window_maximum=max(window_maximum,float(window))
            flat = max(flat, abs(flattened_transmittance(left)-flattened_transmittance(right)))
    result.update(samples_before=totals[0], samples_after=totals[1],
                  changed_pixels=changed, count_changed_pixels=count_changed,
                  count_delta_min=min(deltas), count_delta_max=max(deltas),
                  max_transmittance_difference=float(maximum), max_flattened_alpha_difference=float(flat),
                  worst_pixel=worst, header_bound=bound(b),
                  difference_limit=None, flattened_alpha_limit=CROSS_ALPHA_LIMIT,
                  triangle_inequality_bound=bound(a)+bound(b),
                  max_depth_window_violation=window_maximum if mode=='strict' else None,
                  max_required_shift_ulps=None if shift_failed else shift,
                  required_shift_exceeds_four=shift_failed,shift_report='Minimum integer ULP window; 0 when the unshifted error passes',
                  audit_passed=cross_comparison_passed(flat))
    return result


if __name__ == '__main__':
    result = compare(*sys.argv[1:3])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
