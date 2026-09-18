# SPDX-License-Identifier: Apache-2.0
"""Rigid motion acceptance. Gaffer Python: EXE OUT [CPU|CUDA] [CPU_RESULTS]."""
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
device = sys.argv[3] if len(sys.argv) > 3 else 'CPU'
cpu_dir = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
out.mkdir(parents=True, exist_ok=True)
W, H, S = 24, 18, 64
report = {'device': device, 'scenes': {}, 'rejections': []}

def tfm(x=0, z=0, angle=0):
    c, s = math.cos(angle), math.sin(angle)
    return f'{c} 0 {s} {x} 0 1 0 0 {-s} 0 {c} {z}'

def moving(name, z, transforms, small=False):
    geo = plane(name, z, 'scale="0.09 0.09 1"' if small else '')
    return f'<object name="{name}" motion="{" ".join(transforms)}"/><state object="{name}">{geo}</state>'

def source(geometry, camera_motion=(), extra_camera='', adaptive=False):
    xml = scene(material('near', .5) + material('far', .5), geometry)
    attrs = 'shuttertime="1" ' + extra_camera
    if camera_motion:
        attrs += ' motion="' + ' '.join(camera_motion) + '"'
    xml = xml.replace('fov="0.9"', 'fov="0.9" ' + attrs)
    xml = xml.replace('seed="123"', 'seed="123" motion_blur="true"')
    if adaptive:
        xml = xml.replace('use_adaptive_sampling="false"',
            'use_adaptive_sampling="true" adaptive_min_samples="8" adaptive_threshold="0.1"')
    return xml

translations = (tfm(z=0), tfm(z=2))
rotations = (tfm(angle=-.2), tfm(angle=.2))
fixtures = {
    'object_depth': source(moving('near', 5, (tfm(z=-1), tfm(z=1)))),
    'camera_depth': source(plane('near', 6), translations),
    'matched_translation': source(moving('near', 6, translations), translations),
    'matched_rotation': source(moving('near', 6, rotations), rotations),
    'moving_edge': source(moving('near', 4, (tfm(x=-1), tfm(x=1)), True) + plane('far', 8)),
    'crossing_depths': source(moving('near', 5, (tfm(z=-1), tfm(z=1))) + moving('far', 5, (tfm(z=1), tfm(z=-1)))),
    'adaptive_lens_motion': source(moving('near', 6, translations), translations,
                                   'aperturesize="0.2" focaldistance="6"', True),
}
opaque = source(moving('near', 6, translations), translations)
opaque = opaque.replace(material('near', .5), '<shader name="near"><emission name="e" color="0.3 0.5 0.7"/><connect from="e emission" to="output surface"/></shader>')
fixtures['opaque_motion'] = opaque
fixtures['thin_separate_layers'] = source(plane('near', 6) + plane('far', 6.000002))
fixtures['thin_same_object'] = source('''<state shader="near"><mesh
 P="-10 -10 6 10 -10 6 10 10 6 -10 10 6 -10 -10 6.000002 10 -10 6.000002 10 10 6.000002 -10 10 6.000002"
 nverts="4 4" verts="0 1 2 3 4 5 6 7"/></state>''')
fixtures['thin_connected_fold'] = source('''<state shader="near"><mesh
 P="-10 -10 6 10 -10 6 0 10 5.999996 0 10 6.000004"
 nverts="3 3" verts="0 1 2 1 0 3"/></state>''')
if device == 'CPU':
    fixtures['osl_motion'] = fixtures['matched_rotation']

def render(name, xml, deep=True, extra=(), failure=False):
    src = out / (name + '.xml'); src.write_text(xml)
    beauty, target, records = (out / (name+s) for s in ('.beauty.exr', '.deep.exr', '.csv'))
    cmd = [str(exe), '--background', '--quiet', '--device', device, '--shadingsys', 'osl' if name.startswith('osl_') else 'svm',
           '--samples', str(S), '--threads', '4', '--width', str(W), '--height', str(H), '--output', str(beauty)]
    if deep:
        cmd += ['--deep-output', str(target), '--deep-records', str(records), '--deep-max-events', '4']
        if not name.startswith('opaque_'): cmd += ['--deep-transparent']
    if failure: target.write_bytes(b'previous-complete-frame')
    p = subprocess.run(cmd + list(extra) + [str(src)], capture_output=True, text=True, timeout=900)
    (out / (name + '.log')).write_text(p.stdout+p.stderr)
    if failure:
        check(p.returncode != 0 and target.read_bytes() == b'previous-complete-frame', 'Unsafe failure '+name)
        report['rejections'].append(name)
        return
    check(p.returncode == 0 and 'ERROR:' not in p.stderr, name+': '+p.stderr[-3000:])
    return beauty, target, records

def ledger(path):
    pixels = {(x,y): {} for y in range(H) for x in range(W)}
    misses = set()
    with path.open() as stream:
        for row in csv.DictReader(stream):
            x,y,s,i = (int(row[k]) for k in ('file_x','file_y','sample','event'))
            check((x,y) in pixels and 0 <= s < S, 'Invalid identity')
            events = pixels[x,y].setdefault(s, [])
            z,a = (struct.unpack('f',struct.pack('f',float(row[k])))[0] for k in ('depth','alpha'))
            if i < 0:
                check((x,y,s) not in misses and not events and z == a == 0, 'Invalid miss')
                misses.add((x,y,s))
            else:
                check((x,y,s) not in misses and i == len(events), 'Incomplete chain')
                events.append((z,a))
    for samples in pixels.values():
        check(samples and sorted(samples) == list(range(len(samples))), 'Incomplete population')
    return pixels

all_raw = {}
for name, xml in fixtures.items():
    on, off = render(name, xml), render(name+'_off', xml, False)
    raw = ledger(on[2]); all_raw[name] = raw
    deep, beauty, disabled = reader(on[1]), reader(on[0]), reader(off[0])
    fmt = deep['out']['format'].getValue()
    stats = {'max_curve_error':0., 'max_beauty_error':0., 'samples':sum(map(len,raw.values()))}
    for (x,y), population in raw.items():
        point = fmt.fromEXRSpace(imath.V2i(x,y)); origin,index = tile_index(point)
        actual = deep_pixel(deep['out'], point)
        depths = {z for events in population.values() for z,a in events} | {e[0] for e in actual}
        for depth in depths:
            for inclusive in (False,True):
                def include(z): return z<=depth if inclusive else z<depth
                expected = sum(math.prod(1-a for z,a in events if include(z)) for events in population.values())/len(population)
                observed = math.prod(1-a for z,back,a in actual if include(z))
                stats['max_curve_error'] = max(stats['max_curve_error'], abs(expected-observed))
        for channel in ('R','G','B','A'):
            error = abs(beauty['out'].channelData(channel,origin)[index] - disabled['out'].channelData(channel,origin)[index])
            stats['max_beauty_error'] = max(stats['max_beauty_error'],error)
    check(stats['max_curve_error'] < 1e-6, name+' export mismatch')
    check(stats['max_beauty_error'] <= (S*2**-23 if device=='CUDA' else 0), name+' beauty mismatch')
    if cpu_dir:
        cpu = ledger(cpu_dir / (name+'.csv')); max_z = 0.
        for key, population in raw.items():
            check(population.keys() == cpu[key].keys(), 'CPU/GPU population mismatch')
            for sample, events in population.items():
                other = cpu[key][sample]
                check(len(events)==len(other), 'CPU/GPU chain mismatch')
                for (z,a),(cz,ca) in zip(events,other):
                    max_z = max(max_z,abs(z-cz)); check(abs(a-ca)<1e-6,'CPU/GPU alpha mismatch')
        check(max_z<2e-5,'CPU/GPU depth mismatch'); stats['max_cpu_depth_difference']=max_z
    values = [z for p in raw.values() for e in p.values() for z,a in e]
    stats.update(min_depth=min(values), max_depth=max(values))
    if name in ('matched_translation','matched_rotation','adaptive_lens_motion','opaque_motion','osl_motion'):
        check(max(abs(z-6) for z in values)<2e-5,'Camera-relative depth is not invariant')
    if name in ('object_depth','camera_depth'):
        check(min(values)<4.1 and max(values)>5.9,'Shutter time is not sampled')
    if name == 'crossing_depths':
        for population in raw.values():
            for events in population.values():
                check(len(events)==2 and events[0][0]<=events[1][0], 'Crossing chain is incomplete or unordered')
                check(abs(sum(z for z,a in events)-10)<2e-5, 'Crossing geometry time mismatch')
                check(all(abs(a-.5)<1e-6 for z,a in events), 'Crossing opacity mismatch')
    if name in ('thin_separate_layers', 'thin_same_object', 'thin_connected_fold'):
        allowed_lengths = (0,2) if name == 'thin_connected_fold' else (2,)
        check(values and all(len(events) in allowed_lengths and all(abs(a-.5)<1e-6 for z,a in events)
                  for population in raw.values() for events in population.values()),
              'Thin distinct surfaces were incorrectly discarded')
    report['scenes'][name]=stats; print(name,stats,flush=True)

# Same shutter sample: (4+2t) + (6-2t) = 10, independent of t.
error = max(abs(events[0][0] + all_raw['camera_depth'][key][sample][0][0] - 10)
            for key,pop in all_raw['object_depth'].items() for sample,events in pop.items())
check(error<2e-5,'Object/camera shutter-time mismatch'); report['paired_time_error']=error
for name, xml in {
    'rolling': fixtures['object_depth'].replace('shuttertime="1"','shuttertime="1" rolling_shutter_type="top"'),
    'animated_fov': fixtures['camera_depth'].replace('fov="0.9"','fov="0.9" use_perspective_motion="true"'),
    'nonuniform_shutter': fixtures['object_depth'].replace('shuttertime="1"','shuttertime="1" shutter_curve="0 1"'),
    'scaled_motion': source(moving('near',6,('2 0 0 0 0 1 0 0 0 0 1 0',tfm(z=1)))),
    'malformed_camera_motion': source(plane('near',6), ('1 0 0',)),
    'nonfinite_camera_motion': source(plane('near',6), (tfm(),tfm(z=float('nan')))),
    'malformed_object_motion': source(moving('near',6,('1 0 0',))),
}.items(): render(name,xml,failure=True)

script=Gaffer.ScriptNode()
for i,name in enumerate(fixtures):
    r=reader(out/(name+'.deep.exr')); script[name]=r
    cloud=CyclesDeep.DeepToPointCloud(); script[name+'_Points']=cloud
    cloud['in'].setInput(r['out']); cloud['verticalFieldOfView'].setValue(math.degrees(.9))
    cut=GafferImage.DeepSlice(); script[name+'_Cut']=cut
    cut['in'].setInput(r['out']); cut['farClip']['enabled'].setValue(True); cut['farClip']['value'].setValue(5); cut['flatten'].setValue(True)
    for node,y in ((r,20),(cloud,0),(cut,-20)):
        node.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(i*40,y),flags=Gaffer.Plug.Flags.Default|Gaffer.Plug.Flags.Dynamic))
    Gaffer.Metadata.registerValue(cloud,'description','Shutter-integrated camera-relative depth distribution. Not world-space trajectories or original time-tagged hit positions.')
    check(cloud['out'].object('/deepPoints').numPoints>0,'Empty point cloud')
script['fileName'].setValue((out/'m7_motion_review.gfr').as_posix()); script.save()
loaded=Gaffer.ScriptNode(); loaded['fileName'].setValue(script['fileName'].getValue()); loaded.load()
check(loaded['matched_rotation_Points']['out'].object('/deepPoints').numPoints>0,'Reloaded cloud is empty')
(out/'report.json').write_text(json.dumps(report,indent=2)); print(device+' rigid motion acceptance passed',flush=True)
