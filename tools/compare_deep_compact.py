# SPDX-License-Identifier: Apache-2.0
"""Compare a compact deep EXR with the exact same-capture EXR.

Run in an OpenImageIO environment: python TOOL EXACT COMPACT REPORT.
The compact file may use HALF alpha and may omit ZBack (read as ZBack = Z, as
the OpenEXR deep specification states). Same-object surface groups within the
header's relative depth tolerance are written as point samples at the group
front, so the gate is depth tolerant: at every evaluated depth z the compact
transmittance must lie within the bound of the exact transmittance somewhere
in [z / (1 + tol), z * (1 + tol)]. With tol = 0 this is the plain curve error.
The flattened alpha (transmittance after every sample) is compared exactly.
Depths: every boundary of both curves, the boundaries scaled by (1 +- tol),
and 8 interior points per segment for exponential extrema. Per-ID curves get
the same measurement (reported; the gate is the combined curve).
"""
import json
import math
import sys
from pathlib import Path

import numpy as np


def read(path):
    import OpenImageIO as oiio
    inp = oiio.ImageInput.open(str(path))
    if inp is None:
        raise ValueError(oiio.geterror())
    spec = inp.spec()
    data = inp.read_native_deep_image(0, 0)
    names = list(spec.channelnames)
    header = {a.name: a.value for a in spec.extra_attribs}
    z, a = names.index('Z'), names.index('A')
    zb = names.index('ZBack') if 'ZBack' in names else None
    ident = names.index('id') if 'id' in names else None
    pixels = []
    for p in range(spec.width * spec.height):
        n = data.samples(p)
        if not n:
            pixels.append(None)
            continue
        front = np.array([data.deep_value(p, z, s) for s in range(n)], dtype=np.float64)
        back = front.copy() if zb is None else np.array([data.deep_value(p, zb, s) for s in range(n)], dtype=np.float64)
        alpha = np.array([data.deep_value(p, a, s) for s in range(n)], dtype=np.float64)
        ids = (np.zeros(n, dtype=np.uint32) if ident is None else
               np.array([data.deep_value_uint(p, ident, s) for s in range(n)], dtype=np.uint32))
        pixels.append((front, back, alpha, ids))
    info = dict(width=spec.width, height=spec.height, channels=names,
                formats=[str(spec.channelformat(i)) for i in range(len(names))], header=header)
    inp.close()
    return info, pixels


def transmittance(samples, depths, before):
    """T at each depth; before=True excludes point steps exactly at the depth.
    Piecewise-linear log T via sorted prefix sums: O((n + m) log n)."""
    if samples is None:
        return np.ones_like(depths)
    front, back, alpha, _ = samples
    point = front == back
    side = 'left' if before else 'right'
    log_t = np.zeros_like(depths)
    opaque = np.zeros(depths.shape, dtype=np.int64)
    if point.any():
        f = front[point]; a = alpha[point]
        order = np.argsort(f, kind='stable'); f = f[order]; a = a[order]
        hard = a >= 1
        log1m = np.where(hard, 0.0, np.log1p(-np.where(hard, 0.0, a)))
        prefix = np.concatenate([[0.0], np.cumsum(log1m)])
        hard_prefix = np.concatenate([[0], np.cumsum(hard)])
        k = np.searchsorted(f, depths, side=side)
        log_t += prefix[k]
        opaque += hard_prefix[k]
    if (~point).any():
        f, b, a = front[~point], back[~point], alpha[~point]
        rate = np.log1p(-a) / (b - f)  # log T slope inside each interval
        for edge, sign in ((f, 1.0), (b, -1.0)):
            order = np.argsort(edge, kind='stable')
            e, r = edge[order], rate[order] * sign
            pr = np.concatenate([[0.0], np.cumsum(r)])
            pre = np.concatenate([[0.0], np.cumsum(r * e)])
            k = np.searchsorted(e, depths, side='left')
            log_t += depths * pr[k] - pre[k]
    return np.where(opaque > 0, 0.0, np.exp(np.minimum(log_t, 0.0)))


def depths_for(a, b, tolerance):
    edges = []
    for s in (a, b):
        if s is not None:
            edges.extend(s[0]); edges.extend(s[1])
    if not edges:
        return np.array([1.0])
    edges = np.unique(np.array(edges))
    inner = [edges]
    if len(edges) > 1:
        lo, hi = edges[:-1], edges[1:]
        for k in range(1, 9):
            inner.append(lo + (hi - lo) * k / 9)
    if tolerance:
        inner += [edges * (1 + tolerance), edges / (1 + tolerance)]
    return np.unique(np.concatenate(inner + [[edges[0] * 0.5, edges[-1] * 2]]))


def errors(exact, compact, tolerance):
    z = depths_for(exact, compact, tolerance)
    ta_after, ta_before = transmittance(compact, z, False), transmittance(compact, z, True)
    tb_after, tb_before = transmittance(exact, z, False), transmittance(exact, z, True)
    plain = float(max(np.abs(ta_after - tb_after).max(), np.abs(ta_before - tb_before).max()))
    if tolerance:
        lower = transmittance(exact, z * (1 + tolerance), False)
        upper = transmittance(exact, z / (1 + tolerance), True)
        tolerant = float(max(np.maximum(lower - ta_after, 0).max(), np.maximum(ta_before - upper, 0).max()))
    else:
        tolerant = plain
    inf = np.array([np.inf])
    final_a = 1.0 if compact is None else float(np.prod(1 - compact[2]))
    final_b = 1.0 if exact is None else float(np.prod(1 - exact[2]))
    return plain, tolerant, abs(final_a - final_b)


def subset(samples, ident):
    if samples is None:
        return None
    keep = samples[3] == ident
    return None if not keep.any() else tuple(v[keep] for v in samples)


def compare(exact_path, compact_path):
    ei, ep = read(exact_path)
    ci, cp = read(compact_path)
    if (ei['width'], ei['height']) != (ci['width'], ci['height']):
        raise ValueError('Image windows differ')
    h = ci['header']
    if h.get('cycles:deepSize') != 'compact' or 'cycles:deepSize' in ei['header']:
        raise ValueError('Expected an exact file and a compact file')
    for name in ('cycles:maxTransmittanceError', 'cycles:deepError', 'cycles:deepIDManifest',
                 'cycles:deepSamples', 'cycles:deepScope'):
        if ei['header'].get(name) != h.get(name):
            raise ValueError('Data header differs: ' + name)
    tolerance = float(h.get('cycles:deepCompactZTolerance', 0) or 0)
    result = dict(exact_file=str(Path(exact_path).resolve()), compact_file=str(Path(compact_path).resolve()),
                  exact_bytes=Path(exact_path).stat().st_size, compact_bytes=Path(compact_path).stat().st_size,
                  exact_channels=list(zip(ei['channels'], ei['formats'])),
                  compact_channels=list(zip(ci['channels'], ci['formats'])),
                  z_tolerance=tolerance, bound=float(h['cycles:maxTransmittanceError']),
                  ledger=float(h.get('cycles:deepCompactLedger', 0)),
                  exact_samples=0, compact_samples=0, max_plain_error=0., max_error=0.,
                  max_flatten_error=0., max_id_error=0., worst_pixel=None, pixels=len(ep))
    ids = 'id' in ci['channels']
    for p, (a, b) in enumerate(zip(ep, cp)):
        result['exact_samples'] += 0 if a is None else len(a[0])
        result['compact_samples'] += 0 if b is None else len(b[0])
        if a is None and b is None:
            continue
        plain, tolerant, flat = errors(a, b, tolerance)
        result['max_plain_error'] = max(result['max_plain_error'], plain)
        result['max_flatten_error'] = max(result['max_flatten_error'], flat)
        if tolerant > result['max_error']:
            result['max_error'] = tolerant
            result['worst_pixel'] = [p % ei['width'], p // ei['width']]
        if ids:
            present = set()
            for s in (a, b):
                if s is not None: present |= set(s[3].tolist())
            for ident in present:
                _, e, f = errors(subset(a, ident), subset(b, ident), tolerance)
                result['max_id_error'] = max(result['max_id_error'], e, f)
    result['compact_error'] = max(result['max_error'], result['max_flatten_error'])
    result['size_ratio'] = result['compact_bytes'] / result['exact_bytes']
    result['passed'] = result['compact_error'] <= result['bound']
    result['oracle'] = ('Depth-tolerant all-depth transmittance and exact flattened alpha against the '
                        'same-capture exact file; ZBack absent reads as Z')
    return result


if __name__ == '__main__':
    report = compare(sys.argv[1], sys.argv[2])
    Path(sys.argv[3]).write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, (list, dict))}))
    sys.exit(0 if report['passed'] else 1)
