# SPDX-License-Identifier: Apache-2.0
"""Check same-capture z=0 / merged publications outside every merged span.

Run in an OpenImageIO environment: python TOOL BEFORE AFTER REPORT [--workers N].
Unchanged records must be exact. Each replacement must consume consecutive hard
surfaces of one ID, with no volume overlap. Collapsing both representations to
the back of that span permits the existing independent all-depth curve check
to prove the exterior bound, including flattened alpha, without skipping any
volume region or increasing its transmittance allowance.
"""
import argparse
import concurrent.futures
import json
import math
from pathlib import Path

from compare_deep_ids import read, curve_error


def collapse(before, after, tolerance):
    left, right = [], []
    i = groups = removed = 0
    volume_back = 0
    for sample in after:
        if i < len(before) and sample == before[i]:
            left.append(sample[:3]); right.append(sample[:3])
            if sample[0] < sample[1]: volume_back = max(volume_back, sample[1])
            i += 1
            continue
        front, back, alpha, identifier = sample
        if not (front < back and back-front <= tolerance*front and volume_back <= front):
            raise ValueError('Changed record is not a permitted surface depth span')
        begin = i
        transmission = 1.
        while i < len(before) and before[i][0] <= back:
            z, zback, a, obj = before[i]
            if z != zback or obj != identifier or z < front:
                raise ValueError('Merge crossed a volume or another object')
            transmission *= 1-a
            i += 1
        if i-begin < 2 or before[begin][0] != front or before[i-1][1] != back:
            raise ValueError('Merged span does not exactly cover a group of original surfaces')
        left.append((back, back, 1-transmission))
        right.append((back, back, alpha))
        groups += 1
        removed += i-begin-1
    if i != len(before): raise ValueError('Publication dropped records')
    return left, right, groups, removed


def compare(arguments):
    import OpenImageIO as oiio
    oiio.attribute('threads', 1)
    before_path, after_path, rows = arguments
    a, ad = read(before_path, rows); b, bd = read(after_path, rows)
    if (a.x,a.y,a.width,a.height,a.channelnames,a.channelformats) != (
            b.x,b.y,b.width,b.height,b.channelnames,b.channelformats):
        raise ValueError('Image windows or channel layout differ')
    tolerance = b.getattribute('cycles:deepZTolerance') or 0
    error = b.getattribute('cycles:maxTransmittanceError')
    if not (tolerance > 0 and error == a.getattribute('cycles:maxTransmittanceError')):
        raise ValueError('Missing depth tolerance or mismatched transmittance budget')
    if a.getattribute('cycles:deepZTolerance'): raise ValueError('Before must disable depth merging')
    for name in ('cycles:deepError','cycles:deepSamples','cycles:deepScope',
                 'cycles:deepIDManifest','cycles:deepIDHash','compression'):
        if a.getattribute(name) != b.getattribute(name): raise ValueError('Data header differs: '+name)
    channels = [a.channelnames.index(c) for c in ('Z','ZBack','A')]
    id_channel = a.channelnames.index('id') if 'id' in a.channelnames else None
    result = dict(passed=True, pixels=a.width*a.height, before_samples=0, after_samples=0,
                  before_surfaces=0, after_surfaces=0, merged_groups=0, removed_samples=0,
                  max_exterior_error=0., max_flatten_error=0., worst_pixel=None,
                  tolerance=error, z_tolerance=tolerance)
    for p in range(a.width*a.height):
        curves = []
        for data in (ad,bd):
            curves.append([(*[data.deep_value(p,c,s) for c in channels],
                            data.deep_value_uint(p,id_channel,s) if id_channel is not None else 0)
                           for s in range(data.samples(p))])
        try: left,right,groups,removed = collapse(*curves,tolerance)
        except ValueError as exc:
            raise ValueError(f'Pixel ({a.x+p%a.width},{a.y+p//a.width}): {exc}') from exc
        difference = curve_error(left,right)
        flatten = abs(math.prod(1-v[2] for v in curves[0])-math.prod(1-v[2] for v in curves[1]))
        if difference > result['max_exterior_error']:
            result['max_exterior_error']=difference
            result['worst_pixel']=[a.x+p%a.width,a.y+p//a.width]
        result['max_flatten_error']=max(result['max_flatten_error'],flatten)
        result['before_samples'] += len(curves[0]); result['after_samples'] += len(curves[1])
        result['before_surfaces'] += sum(v[0] == v[1] for v in curves[0])
        result['after_surfaces'] += sum(v[0] == v[1] for v in curves[1])+groups
        result['merged_groups'] += groups; result['removed_samples'] += removed
    result['passed'] = result['max_exterior_error'] <= error and result['max_flatten_error'] <= error
    return result


def run(before, after, workers):
    spec,_ = read(after,(0,1))
    if workers <= 0: raise ValueError('Workers must be positive')
    step=max(1, math.ceil(spec.height/(workers*4)))
    tasks=[(before,after,(first,min(spec.height,first+step))) for first in range(0,spec.height,step)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:
        parts=list(pool.map(compare,tasks))
    result=parts[0].copy()
    for field in ('pixels','before_samples','after_samples','before_surfaces','after_surfaces',
                  'merged_groups','removed_samples'):
        result[field]=sum(p[field] for p in parts)
    worst=max(parts,key=lambda p:p['max_exterior_error'])
    result['max_exterior_error']=worst['max_exterior_error'];result['worst_pixel']=worst['worst_pixel']
    result['max_flatten_error']=max(p['max_flatten_error'] for p in parts)
    result['passed']=all(p['passed'] for p in parts)
    result['before_file']=str(Path(before).resolve()); result['after_file']=str(Path(after).resolve())
    result['oracle']='Unchanged records exact; consecutive same-ID hard surfaces; exterior exponential extrema'
    return result


def self_test():
    source=[(500.,500.,.2,7),(500.01,500.01,.3,7)]
    left,right,groups,removed=collapse(source,[(500.,500.01,.44,7)],1e-4)
    assert groups == removed == 1 and curve_error(left,right) < 1e-15
    for barrier in ((500.005,500.005,.1,8),(500.005,500.006,.1,7)):
        try: collapse([source[0],barrier,source[1]],[(500.,500.01,.44,7)],1e-4)
        except ValueError: pass
        else: raise AssertionError('Barrier was accepted')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before',type=Path);parser.add_argument('after',type=Path)
    parser.add_argument('report',type=Path);parser.add_argument('--workers',type=int,default=1)
    args=parser.parse_args();self_test()
    result=run(args.before,args.after,args.workers)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
