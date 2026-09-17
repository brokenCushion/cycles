# SPDX-License-Identifier: Apache-2.0
"""M7a adaptive acceptance. Gaffer Python: EXE OUT [CPU|CUDA] [CPU_RESULTS]."""
import csv
import json
import math
import re
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
device = sys.argv[3] if len(sys.argv) > 3 else 'CPU'
check(device in ('CPU', 'CUDA'), 'Unsupported validation device')
cpu_results = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
W, H, maximum = 32, 24, 128
report = {'device': device, 'scenes': {}, 'limitations': 'Beauty convergence does not establish depth convergence.'}

def render(name, source, adaptive=True, deep=True, samples=maximum, extra=(), failure=False):
    source = source.replace('width="16" height="12"', f'width="{W}" height="{H}"')
    if adaptive:
        threshold = '0.001' if name.startswith('edge') else '0.1'
        source = source.replace('use_adaptive_sampling="false"',
            f'use_adaptive_sampling="true" adaptive_min_samples="8" adaptive_threshold="{threshold}"')
    path = out / (name + '.xml')
    path.write_text(source)
    beauty, target, records = (out / (name + suffix) for suffix in ('.beauty.exr', '.deep.exr', '.csv'))
    command = [str(exe), '--background', '--quiet', '--device', device, '--shadingsys',
        'osl' if name.startswith('osl') else 'svm',
        '--samples', str(samples), '--threads', '4', '--width', str(W), '--height', str(H),
        '--output', str(beauty)]
    if deep:
        command += ['--deep-output', str(target), '--deep-records', str(records)]
        if not name.startswith('opaque'):
            command += ['--deep-transparent', '--deep-max-events', '4']
    if failure:
        target.write_bytes(b'previous-complete-deep-frame')
    p = subprocess.run(command + list(extra) + [str(path)], capture_output=True, text=True, timeout=600)
    (out / (name + '.log')).write_text(p.stdout + p.stderr)
    if failure:
        check(p.returncode != 0 and target.read_bytes() == b'previous-complete-deep-frame',
              name + ' did not preserve the previous deep frame')
        report.setdefault('rejections', []).append(name)
        return
    check(p.returncode == 0 and 'ERROR:' not in p.stderr, name + ': ' + p.stderr[-3000:])
    return beauty, target, records

def ledger(path):
    pixels = {(x, y): {} for y in range(H) for x in range(W)}
    misses = set()
    with path.open() as stream:
        for row in csv.DictReader(stream):
            x, y, s, event = (int(row[k]) for k in ('file_x', 'file_y', 'sample', 'event'))
            check((x, y) in pixels and 0 <= s < 512, 'Invalid camera identity')
            events = pixels[x, y].setdefault(s, [])
            z, a = (struct.unpack('f', struct.pack('f', float(row[k])))[0] for k in ('depth', 'alpha'))
            if event < 0:
                check(not events and (x, y, s) not in misses and z == a == 0, 'Duplicate miss')
                misses.add((x, y, s))
            else:
                check((x, y, s) not in misses and event == len(events), 'Incomplete event chain')
                events.append((z, a))
    for samples in pixels.values():
        check(samples and sorted(samples) == list(range(len(samples))), 'Missing effective identity')
    return pixels

def inspect(name, on, off):
    pixels = ledger(on[2])
    counts = [len(samples) for samples in pixels.values()]
    check(min(counts) < maximum, 'Fixture did not exercise adaptive stopping')
    if name == 'edge':
        check(min(counts) < max(counts), 'Fixture did not exercise variable populations')
    result = {'min_samples': min(counts), 'max_samples': max(counts),
        'accepted_samples': sum(counts), 'max_curve_error': 0., 'max_beauty_error': 0.}
    deep, beauty, disabled = reader(on[1]), reader(on[0]), reader(off[0])
    fmt = deep['out']['format'].getValue()
    for (x, y), samples in pixels.items():
        point = fmt.fromEXRSpace(imath.V2i(x, y))
        origin, index = tile_index(point)
        actual = deep_pixel(deep['out'], point)
        depths = {z for events in samples.values() for z, a in events} | {e[0] for e in actual}
        for depth in depths:
            for inclusive in (False, True):
                def include(z): return z <= depth if inclusive else z < depth
                expected = sum(math.prod(1-a for z, a in events if include(z))
                    for events in samples.values()) / len(samples)
                observed = math.prod(1-a for z, back, a in actual if include(z))
                result['max_curve_error'] = max(result['max_curve_error'], abs(expected-observed))
        for channel in ('R', 'G', 'B', 'A'):
            error = abs(beauty['out'].channelData(channel, origin)[index] -
                disabled['out'].channelData(channel, origin)[index])
            result['max_beauty_error'] = max(result['max_beauty_error'], error)
    check(result['max_curve_error'] < 1e-6, 'Adaptive normalization mismatch')
    check(result['max_beauty_error'] < 2e-6 if device == 'CUDA' else result['max_beauty_error'] == 0,
          'Adaptive beauty changed')
    if cpu_results:
        cpu = ledger(cpu_results / (name + '.csv'))
        result['cpu_population_mismatches'] = sum(len(cpu[k]) != len(v) for k, v in pixels.items())
        result['cpu_max_depth_error'] = 0.
        result['cpu_max_local_alpha_error'] = 0.
        check(result['cpu_population_mismatches'] == 0, name + ' CPU/CUDA populations differ')
        for key, samples in pixels.items():
            for sample, events in samples.items():
                other = cpu[key][sample]
                check(len(other) == len(events), 'CPU/CUDA adaptive chain length mismatch')
                for (z, a), (cz, ca) in zip(events, other):
                    result['cpu_max_depth_error'] = max(result['cpu_max_depth_error'], abs(z-cz))
                    result['cpu_max_local_alpha_error'] = max(result['cpu_max_local_alpha_error'], abs(a-ca))
        check(result['cpu_max_depth_error'] < 2e-5 and result['cpu_max_local_alpha_error'] < 1e-6,
              name + ' CPU/CUDA adaptive events differ')
    report['scenes'][name] = result
    if device == 'CUDA':
        log = (out / (name + '.log')).read_text()
        captures = re.findall(r'Deep CUDA capture: records=(\d+) skipped=(\d+)', log)
        check(captures, 'Missing GPU capture counters')
        if captures:
            result['logged_accepted'] = sum(int(a) for a, b in captures)
            result['logged_skipped'] = sum(int(b) for a, b in captures)
            check(result['logged_accepted'] == sum(counts), 'GPU capture accounting mismatch')
            if name == 'edge':
                check(result['logged_skipped'] > 0, 'Adaptive skipped-lane path was not exercised')
    print(name, result, flush=True)
    return pixels

stack = material('near', .75) + material('far', .5)
fixtures = {
    'stack': scene(stack, plane('near', 2) + plane('far', 8)),
    'miss': scene(material('near', .5), ''),
    'edge': scene(stack, plane('near', 2, 'translate="1.1 0 0" scale="0.07 0.07 1"') + plane('far', 8)),
    'opaque': scene('<shader name="near"><emission name="e"/><connect from="e emission" to="output surface"/></shader>', plane('near', 2)),
    'osl_stack': scene(stack, plane('near', 2) + plane('far', 8)),
}
for name, source in fixtures.items():
    if device == 'CUDA' and name.startswith('osl'):
        continue
    on = render(name, source)
    inspect(name, on, render(name + '_off', source, deep=False))

# Identical emission hides a depth discontinuity from beauty's stopping test.
same = material('same', 0)
source = scene(same, plane('same', 2, 'translate="0.91 0 0" scale="0.09 1 1"') + plane('same', 8))
reference = render('depth_reference', source, adaptive=False, samples=512)
reference_pixels = ledger(reference[2])
for seed in (1, 7, 23):
    name = 'depth_seed_' + str(seed)
    seeded = source.replace('seed="123"', f'seed="{seed}"')
    on = render(name, seeded)
    pixels = inspect(name, on, render(name + '_off', seeded, deep=False))
    errors = []
    for key, samples in pixels.items():
        def transmittance(population):
            return sum(math.prod(1-a for z, a in events if z < 5)
                       for events in population.values()) / len(population)
        errors.append(abs(transmittance(samples) - transmittance(reference_pixels[key])))
    report['scenes'][name]['max_depth_cut_error_vs_512'] = max(errors)
    report['scenes'][name]['mean_depth_cut_error_vs_512'] = sum(errors) / len(errors)
    print('depth uncertainty', name, max(errors), flush=True)

render('adaptive_overflow', fixtures['stack'], extra=('--deep-max-events', '1'), failure=True)
if device == 'CUDA':
    render('adaptive_gpu_osl', fixtures['stack'], extra=('--shadingsys', 'osl'), failure=True)

script = Gaffer.ScriptNode()
for i, name in enumerate(('depth_reference', 'depth_seed_1', 'depth_seed_7', 'depth_seed_23')):
    r = reader(out / (name + '.deep.exr'))
    script[name] = r
    nodes = [(r, 30)]
    points = CyclesDeep.DeepToPointCloud()
    script[name + '_Points'] = points
    points['in'].setInput(r['out'])
    points['verticalFieldOfView'].setValue(math.degrees(.9))
    nodes.append((points, 10))
    cut = GafferImage.DeepSlice()
    script[name + '_Cut'] = cut
    cut['in'].setInput(r['out'])
    cut['farClip']['enabled'].setValue(True)
    cut['farClip']['value'].setValue(5)
    cut['flatten'].setValue(True)
    nodes.append((cut, -10))
    for node, y in nodes:
        node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(i*40, y),
            flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    check(points['out'].object('/deepPoints').numPoints > 0, 'Empty review cloud')
    if cpu_results:
        cpu_reader = reader(cpu_results / (name + '.deep.exr'))
        script['CPU_' + name] = cpu_reader
        cpu_points = CyclesDeep.DeepToPointCloud()
        script['CPU_' + name + '_Points'] = cpu_points
        cpu_points['in'].setInput(cpu_reader['out'])
        cpu_points['verticalFieldOfView'].setValue(math.degrees(.9))
        for node, y in ((cpu_reader, -40), (cpu_points, -60)):
            node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(i*40, y),
                flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
        check(cpu_points['out'].object('/deepPoints').numPoints > 0, 'Empty CPU comparison cloud')
script['fileName'].setValue((out / 'm7_adaptive_review.gfr').as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
check(loaded['depth_seed_1_Points']['out'].object('/deepPoints').numPoints > 0,
      'Saved review could not be reloaded')
(out / 'report.json').write_text(json.dumps(report, indent=2))
print(device + ' adaptive acceptance passed', flush=True)
