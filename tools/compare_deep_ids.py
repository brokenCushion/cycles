# SPDX-License-Identifier: Apache-2.0
"""Compare deepID output with ordinary deep at every boundary and exponential extremum.

Run through an environment with OpenImageIO: python TOOL WITHOUT_IDS WITH_IDS REPORT.
UINT IDs are read natively; Gaffer's float channel conversion is not used for identity.
"""
import argparse
import json
import math
from pathlib import Path
import struct


def name_hash(name):
    """Independent MurmurHash3 x86/32, seed zero (Cryptomatte manifest IDs)."""
    data = name.encode('utf-8')
    mask = 0xffffffff
    def rotate(n, r): return ((n << r) | (n >> (32-r))) & mask
    value = 0
    for (block,) in struct.iter_unpack('<I', data[:len(data)//4*4]):
        block = rotate(block * 0xcc9e2d51 & mask, 15) * 0x1b873593 & mask
        value = rotate(value ^ block, 13)
        value = (value * 5 + 0xe6546b64) & mask
    tail = int.from_bytes(data[len(data)//4*4:], 'little')
    if len(data) % 4:
        value ^= rotate(tail * 0xcc9e2d51 & mask, 15) * 0x1b873593 & mask
    value ^= len(data)
    value ^= value >> 16
    value = value * 0x85ebca6b & mask
    value ^= value >> 13
    value = value * 0xc2b2ae35 & mask
    return value ^ (value >> 16)


def changes(samples):
    events = {}
    for front, back, alpha in samples:
        if not (math.isfinite(front) and math.isfinite(back) and math.isfinite(alpha)
                and 0 < front <= back and 0 < alpha <= 1):
            raise ValueError('Invalid deep interval')
        if front == back:
            events.setdefault(front, [[], []])[1].append(1-alpha)
        else:
            if alpha == 1: raise ValueError('Opaque extended interval')
            rate = -math.log1p(-alpha) / (back-front)
            events.setdefault(front, [[], []])[0].append(rate)
            events.setdefault(back, [[], []])[0].append(-rate)
    return events


def curve_error(left, right):
    """Exact in double arithmetic, allowing arbitrarily overlapping samples."""
    events = (changes(left), changes(right))
    depths = sorted(set(events[0]) | set(events[1]))
    t, rate = [1., 1.], [0., 0.]
    maximum = 0.
    for i, depth in enumerate(depths):
        maximum = max(maximum, abs(t[0]-t[1]))
        for side in (0, 1):
            increment, steps = events[side].get(depth, ([], []))
            rate[side] = math.fsum([rate[side], *increment])
            if rate[side] < -1e-8: raise ValueError('Negative extinction sweep')
            rate[side] = max(0., rate[side])
            t[side] *= math.prod(steps)
        maximum = max(maximum, abs(t[0]-t[1]))
        if i+1 == len(depths): break
        width = depths[i+1]-depth
        if all(v > 0 for v in rate) and all(v > 0 for v in t) and rate[0] != rate[1]:
            z = (math.log(rate[0])+math.log(t[0])-math.log(rate[1])-math.log(t[1]))/(rate[0]-rate[1])
            if 0 < z < width:
                maximum = max(maximum, abs(t[0]*math.exp(-rate[0]*z)-t[1]*math.exp(-rate[1]*z)))
        for side in (0, 1): t[side] *= math.exp(-rate[side]*width)
    return max(maximum, abs(t[0]-t[1]))


def read(path):
    import OpenImageIO as oiio
    source = oiio.ImageInput.open(str(path))
    if source is None: raise ValueError(oiio.geterror())
    spec = source.spec()
    data = source.read_native_deep_image()
    source.close()
    if data is None: raise ValueError('Could not read native deep data')
    return spec, data


def compare(without_ids, with_ids, verify_hashes=True):
    import OpenImageIO as oiio
    before, a = read(without_ids)
    after, b = read(with_ids)
    if (before.x,before.y,before.width,before.height) != (after.x,after.y,after.width,after.height):
        raise ValueError('Image windows differ')
    if 'id' not in after.channelnames or 'id' in before.channelnames:
        raise ValueError('Expected IDs-on versus IDs-off inputs')
    channel = after.channelnames.index('id')
    if after.channelformats[channel] != oiio.UINT: raise ValueError('ID channel is not UINT')
    manifest = json.loads(after.getattribute('cycles:deepIDManifest'))
    if verify_hashes and any(name_hash(name) != int(identifier,16) for identifier,name in manifest.items()):
        raise ValueError('Manifest does not use Cryptomatte name hashes')
    tolerance = after.getattribute('cycles:maxTransmittanceError')
    if tolerance != before.getattribute('cycles:maxTransmittanceError'):
        raise ValueError('Data error bounds differ')
    channels = [[spec.channelnames.index(c) for c in ('Z','ZBack','A')] for spec in (before,after)]
    maximum, worst, overlap_pixels, samples = 0., None, 0, 0
    counts = {}
    for p in range(before.width * before.height):
        curves = []
        for data, names in zip((a,b),channels):
            curves.append([tuple(data.deep_value(p,c,s) for c in names) for s in range(data.samples(p))])
        error = curve_error(*curves)
        if error > maximum: maximum, worst = error, [p % before.width, p // before.width]
        end = 0
        overlapping = False
        for s,(front,back,alpha) in enumerate(curves[1]):
            identifier = b.deep_value_uint(p,channel,s)
            key = f'{identifier:08x}'
            if key not in manifest: raise ValueError('Sample ID absent from manifest')
            counts[key] = counts.get(key,0)+1
            overlapping |= front < end
            end = max(end,back)
        overlap_pixels += int(overlapping)
        # Count every sample, including after the first overlap.
        samples += b.samples(p)
    return dict(passed=maximum <= tolerance, max_combined_transmittance_error=maximum,
                tolerance=tolerance, worst_pixel=worst, pixels=before.width*before.height,
                overlapping_pixels=overlap_pixels, deep_samples=samples,
                manifest_objects=len(manifest), observed_ids=counts,
                oracle='Native UINT IDs; union boundaries and exact exponential stationary points')


def isolate(source_path, object_name, output_path):
    """Select using exact UINT values before Gaffer converts image channels to FLOAT."""
    import os
    import tempfile
    import OpenImageIO as oiio
    spec, data = read(source_path)
    channel = spec.channelnames.index('id')
    if spec.channelformats[channel] != oiio.UINT: raise ValueError('ID channel is not UINT')
    manifest = json.loads(spec.getattribute('cycles:deepIDManifest'))
    keys = [int(k,16) for k,v in manifest.items() if v == object_name]
    if len(keys) != 1: raise ValueError('Object name must identify exactly one manifest ID')
    identifier = keys[0]
    selected = oiio.DeepData()
    selected.init(spec)
    for p in range(spec.width*spec.height):
        indices = [s for s in range(data.samples(p)) if data.deep_value_uint(p,channel,s) == identifier]
        selected.set_samples(p,len(indices))
        for target,source in enumerate(indices):
            for c in range(spec.nchannels):
                if spec.channelformats[c] == oiio.UINT:
                    selected.set_deep_value_uint(p,c,target,data.deep_value_uint(p,c,source))
                else: selected.set_deep_value(p,c,target,data.deep_value(p,c,source))
    spec.attribute('cycles:deepIDSelection',object_name)
    spec.attribute('cycles:deepIDManifest',json.dumps({f'{identifier:08x}':object_name}))
    output_path = Path(output_path)
    with tempfile.NamedTemporaryFile(dir=output_path.parent,suffix='.exr',delete=False) as staging:
        temporary = Path(staging.name)
    try:
        output = oiio.ImageOutput.create(str(temporary))
        if not output or not output.open(str(temporary),spec): raise RuntimeError(oiio.geterror())
        try:
            if not output.write_deep_image(selected): raise RuntimeError(output.geterror())
        finally:
            if not output.close(): raise RuntimeError(output.geterror())
        os.replace(temporary,output_path)
    finally:
        temporary.unlink(missing_ok=True)
    return identifier


def self_test():
    assert name_hash('hello') == 0x248bfa47
    assert name_hash('') == 0
    assert curve_error([(1,3,1-math.exp(-1)),(2,4,1-math.exp(-2))],
                       [(1,2,1-math.exp(-.5)),(2,3,1-math.exp(-1.5)),(3,4,1-math.exp(-1))]) < 1e-15
    assert curve_error([(2,2,.5),(2,2,1)],[(2,2,1)]) == 0
    assert curve_error([],[(2,2,.25)]) == .25


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('without_ids',type=Path)
    parser.add_argument('with_ids',type=Path)
    parser.add_argument('report',type=Path)
    parser.add_argument('--isolate', nargs=2, metavar=('OBJECT_NAME','OUTPUT_EXR'))
    args = parser.parse_args()
    self_test()
    result = compare(args.without_ids,args.with_ids)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    if result['passed'] and args.isolate:
        isolate(args.with_ids,args.isolate[0],Path(args.isolate[1]))
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
