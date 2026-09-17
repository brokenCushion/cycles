# SPDX-License-Identifier: Apache-2.0
"""Static perspective DOF acceptance. Gaffer Python: EXE OUT."""
import csv
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import CyclesDeep
import Gaffer
import GafferImage
import imath
sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_transparency_gaffer import material, plane, scene, reader
from validate_gaffer import check, deep_pixel, tile_index

exe, out = (Path(p).resolve() for p in sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
W, H, S = 32, 24, 64
report = {'scenes': {}, 'rejections': []}
materials = material('near', .4) + material('middle', .6) + material('far', 0)
geometry = (plane('near', 2, 'translate="-0.5 0 0" scale="0.08 0.08 1"') +
            plane('middle', 5, 'scale="0.17 0.17 1"') +
            plane('far', 9, 'translate="1 0 0" scale="0.3 0.3 1"'))
base = scene(materials, geometry)
edge = scene(material('near', 0), plane('near', 5, 'translate="0.031 0 0" scale="0.15 0.15 1"'))

def camera(xml, aperture, focus):
    return xml.replace('fov="0.9"', f'fov="0.9" aperturesize="{aperture}" focaldistance="{focus}"')

def render(name, xml, device='CPU', deep=True, extra=(), failure=False):
    source = out / (name + '.xml')
    source.write_text(xml)
    beauty, target, records = (out / (name + suffix) for suffix in ('.beauty.exr', '.deep.exr', '.csv'))
    cmd = [str(exe), '--background', '--quiet', '--device', device, '--shadingsys', 'svm',
           '--threads', '4', '--samples', str(S), '--width', str(W), '--height', str(H),
           '--output', str(beauty)]
    if deep:
        cmd += ['--deep-output', str(target), '--deep-records', str(records), '--deep-transparent', '--deep-max-events', '4']
    if failure:
        target.write_bytes(b'previous-complete-frame')
    p = subprocess.run(cmd + list(extra) + [str(source)], capture_output=True, text=True, timeout=900)
    (out / (name + '.log')).write_text(p.stdout + p.stderr)
    if failure:
        check(p.returncode != 0 and target.read_bytes() == b'previous-complete-frame', 'Unsafe failure: ' + name)
        report['rejections'].append(name)
        return
    check(p.returncode == 0 and 'ERROR:' not in p.stderr, name + ': ' + p.stderr[-3000:])
    return beauty, target, records

def ledger(path):
    raw = {}
    misses = set()
    with path.open() as stream:
        for row in csv.DictReader(stream):
            x, y, sample, event = (int(row[k]) for k in ('file_x', 'file_y', 'sample', 'event'))
            check(0 <= x < W and 0 <= y < H and 0 <= sample < S, 'Invalid identity')
            key = x, y, sample
            values = raw.setdefault(key, [])
            z, a = (struct.unpack('f', struct.pack('f', float(row[k])))[0] for k in ('depth', 'alpha'))
            if event < 0:
                check(key not in misses and not values and z == a == 0, 'Invalid miss')
                misses.add(key)
            else:
                check(key not in misses and event == len(values), 'Incomplete chain')
                check(min(abs(z-d) for d in (2, 5, 9)) < 2e-5, 'Depth is not camera-axis depth')
                values.append((z, a))
    for y in range(H):
        for x in range(W):
            ids = sorted(s for xx, yy, s in raw if xx == x and yy == y)
            check(ids and ids == list(range(len(ids))), 'Incomplete camera population')
    return raw

def inspect(name, rendered, off, repeated_off=None):
    raw = ledger(rendered[2])
    deep, onbeauty, offbeauty = reader(rendered[1]), reader(rendered[0]), reader(off[0])
    fmt = deep['out']['format'].getValue()
    repeat = reader(repeated_off[0]) if repeated_off else None
    result = {'max_curve_error': 0., 'max_beauty_error': 0., 'max_off_repeat_error': 0., 'accepted_samples': len(raw)}
    for y in range(H):
        for x in range(W):
            samples = [raw[x, y, s] for s in range(S) if (x, y, s) in raw]
            point = fmt.fromEXRSpace(imath.V2i(x, y))
            origin, index = tile_index(point)
            actual = deep_pixel(deep['out'], point)
            depths = {z for events in samples for z, a in events} | {z for z, back, a in actual}
            for depth in depths:
                for inclusive in (False, True):
                    def include(z): return z <= depth if inclusive else z < depth
                    expected = sum(math.prod(1-a for z, a in events if include(z)) for events in samples) / len(samples)
                    observed = math.prod(1-a for z, back, a in actual if include(z))
                    result['max_curve_error'] = max(result['max_curve_error'], abs(expected-observed))
            for channel in ('R', 'G', 'B', 'A'):
                delta = abs(onbeauty['out'].channelData(channel, origin)[index] - offbeauty['out'].channelData(channel, origin)[index])
                result['max_beauty_error'] = max(result['max_beauty_error'], delta)
                if repeat:
                    delta = abs(repeat['out'].channelData(channel, origin)[index] - offbeauty['out'].channelData(channel, origin)[index])
                    result['max_off_repeat_error'] = max(result['max_off_repeat_error'], delta)
    # Conservative FLOAT sum bound for <=64 contributions in these unit-range fixtures.
    tolerance = S * 2**-23 if repeated_off else 0.
    check(result['max_curve_error'] < 1e-6 and result['max_beauty_error'] <= tolerance,
          name + ' reconstruction or beauty mismatch: ' + str(result))
    report['scenes'][name] = result
    print(name, result, flush=True)
    return raw

fixtures = {f'focus_{focus}': camera(base, .35, focus) for focus in (2, 5, 9)}
fixtures['pinhole_edge'] = camera(edge, 0, 5)
fixtures['focused_edge'] = camera(edge, .35, 5)
fixtures['defocused_edge'] = camera(edge, .35, 9)
fixtures['adaptive_dof'] = camera(base, .35, 5).replace('use_adaptive_sampling="false"',
    'use_adaptive_sampling="true" adaptive_min_samples="8" adaptive_threshold="0.1"')
fixtures['polygon_anamorphic'] = camera(base, .35, 5).replace('focaldistance="5"',
    'focaldistance="5" blades="6" bladesrotation="0.2" aperture_ratio="1.5"')
ledgers = {}
for name, xml in fixtures.items():
    pair = {}
    for device in ('CPU', 'CUDA'):
        tag = name + '_' + device
        on = render(tag, xml, device)
        off = render(tag + '_off', xml, device, deep=False)
        repeat = render(tag + '_off_repeat', xml, device, deep=False) if device == 'CUDA' else None
        pair[device] = inspect(tag, on, off, repeat)
    check(pair['CPU'].keys() == pair['CUDA'].keys(), name + ' CPU/GPU populations differ')
    maximum_depth = 0.
    for key, events in pair['CPU'].items():
        other = pair['CUDA'][key]
        check(len(events) == len(other), name + ' CPU/GPU chain mismatch')
        for (z, a), (gz, ga) in zip(events, other):
            maximum_depth = max(maximum_depth, abs(z-gz))
            check(abs(a-ga) < 1e-6, 'CPU/GPU alpha differs')
    check(maximum_depth < 2e-5, 'CPU/GPU axial depth differs')
    report['scenes'][name + '_CUDA']['max_cpu_depth_difference'] = maximum_depth
    ledgers[name] = pair['CPU']

focused_changes = sum(bool(v) != bool(ledgers['focused_edge'][k]) for k, v in ledgers['pinhole_edge'].items())
defocused_changes = sum(bool(v) != bool(ledgers['defocused_edge'][k]) for k, v in ledgers['pinhole_edge'].items())
check(focused_changes == 0 and defocused_changes > 0, 'Lens sampling/focus fixture failed')
report['focused_plane_hit_changes'] = focused_changes
report['defocused_plane_hit_changes'] = defocused_changes
osl = render('osl_dof', fixtures['focus_5'], extra=('--shadingsys', 'osl'))
inspect('osl_dof', osl, render('osl_dof_off', fixtures['focus_5'], deep=False, extra=('--shadingsys', 'osl')))
for name, xml in {
    'negative_aperture': camera(base, -.1, 5),
    'zero_focus': camera(base, .1, 0),
    'zero_ratio': camera(base, .1, 5).replace('focaldistance="5"', 'focaldistance="5" aperture_ratio="0"'),
    'invalid_rotation': camera(base, .1, 5).replace('focaldistance="5"', 'focaldistance="5" bladesrotation="nan"'),
    'infinite_aperture': camera(base, 'inf', 5),
    'nan_focus': camera(base, .1, 'nan'),
    'infinite_fov': camera(base, .1, 5).replace('fov="0.9"', 'fov="inf"'),
}.items():
    for device in ('CPU', 'CUDA'):
        render(name + '_' + device, xml, device, failure=True)

script = Gaffer.ScriptNode()
for i, focus in enumerate((2, 5, 9)):
    for j, device in enumerate(('CPU', 'CUDA')):
        name = f'focus_{focus}_{device}'
        r = reader(out / (name + '.deep.exr')); script[name] = r
        points = CyclesDeep.DeepToPointCloud(); script[name + '_Points'] = points
        points['in'].setInput(r['out']); points['verticalFieldOfView'].setValue(math.degrees(.9))
        cut = GafferImage.DeepSlice(); script[name + '_Cut'] = cut
        cut['in'].setInput(r['out']); cut['farClip']['enabled'].setValue(True)
        cut['farClip']['value'].setValue(6); cut['flatten'].setValue(True)
        beauty = reader(out / (name + '.beauty.exr')); script[name + '_Beauty'] = beauty
        for node, dy in ((r, 0), (points, -20), (cut, -40), (beauty, 20)):
            node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(i*45, -j*110+dy),
                flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
        Gaffer.Metadata.registerValue(points, 'description', 'DOF deep distribution projected through the central pinhole. Not original lens-ray hit positions.')
        check(points['out'].object('/deepPoints').numPoints > 0, 'Empty point cloud')
script['fileName'].setValue((out / 'm7_dof_review.gfr').as_posix()); script.save()
loaded = Gaffer.ScriptNode(); loaded['fileName'].setValue(script['fileName'].getValue()); loaded.load()
check(loaded['focus_5_CUDA_Points']['out'].object('/deepPoints').numPoints > 0, 'Failed to reload review')
(out / 'report.json').write_text(json.dumps(report, indent=2))
print('Static perspective DOF acceptance passed', flush=True)
