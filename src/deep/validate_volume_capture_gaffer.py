# SPDX-License-Identifier: Apache-2.0
"""CPU/CUDA homogeneous render, Beer-Lambert checks, Gaffer cuts and review.
Run with gaffer env python SCRIPT CYCLES_EXE OUTPUT_DIRECTORY [CPU|CUDA] [CPU_BASELINE].
"""
import csv
import json
import math
from pathlib import Path
import subprocess
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

exe, out = (Path(p).resolve() for p in sys.argv[1:3])
device = sys.argv[3] if len(sys.argv) > 3 else 'CPU'
if device not in ('CPU', 'CUDA'):
    raise ValueError('Device must be CPU or CUDA')
baseline = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
out.mkdir(parents=True, exist_ok=True)
tile_size = GafferImage.ImagePlug.tileSize()
report = {'scope': device + ' SVM static homogeneous absorption', 'fixtures': {}, 'rejections': []}


def check(value, message):
    if not value:
        raise RuntimeError(message)


def material(name, density):
    return f'''<shader name="{name}"><absorption_volume name="v" color="0 0 0" density="{density}"/>
<connect from="v volume" to="output volume"/></shader>'''


def box(name, bounds):
    x0, y0, z0, x1, y1, z1 = bounds
    points = [(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),
              (x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
    return f'''<state shader="{name}"><mesh P="{' '.join(str(v) for p in points for v in p)}"
nverts="4 4 4 4 4 4" verts="0 3 2 1 4 5 6 7 0 1 5 4 3 7 6 2 0 4 7 3 1 2 6 5"/></state>'''


def scene(media, surface=False, near=.125, far=20):
    text = f'''<cycles><camera camera_type="perspective" fov="0.9" nearclip="{near}" farclip="{far}"/>
<film filter_type="box" filter_width="1"/><integrator seed="123" use_adaptive_sampling="false"/>
<background transparent="true"><background name="b" color="0.1 0.1 0.1"/>
<connect from="b background" to="output surface"/></background>'''
    for i, (bounds, density) in enumerate(media):
        text += material('fog' + str(i), density) + box('fog' + str(i), bounds)
    if surface:
        text += f'''<shader name="wall"><emission name="e" color="0.3 0.5 0.7"/>
<transparent_bsdf name="t" color="1 1 1"/><mix_closure name="m" fac="{1-float(surface)}"/>
<connect from="e emission" to="m closure1"/><connect from="t bsdf" to="m closure2"/>
<connect from="m closure" to="output surface"/></shader>
<state shader="wall"><mesh P="-10 -10 5 10 -10 5 10 10 5 -10 10 5" nverts="4" verts="0 1 2 3"/></state>'''
    return text + '</cycles>'


def render(name, xml, w, h, samples, deep=True, extra=(), failure=False):
    source = out / (name + '.xml')
    source.write_text(xml)
    beauty, deepfile, ledger = [out / (name + suffix) for suffix in ('.beauty.exr','.deep.exr','.csv')]
    args = [str(exe), '--background', '--quiet', '--device', device, '--shadingsys', 'svm',
            '--width',str(w),'--height',str(h),'--samples',str(samples),'--threads','8',
            '--output',str(beauty)]
    if deep:
        args += ['--deep-volume','--deep-output',str(deepfile),'--deep-records',str(ledger),
                 '--deep-memory-mb','64','--deep-max-events','16']
    if failure:
        deepfile.write_bytes(b'preserve')
    result = subprocess.run(args + list(extra) + [str(source)], capture_output=True, text=True, timeout=900)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    if failure:
        check(result.returncode != 0 and deepfile.read_bytes() == b'preserve', name + ': expected atomic failure')
        report['rejections'].append(name)
    else:
        check(result.returncode == 0 and 'ERROR:' not in result.stderr, name + ': ' + result.stderr[-2500:])
    return beauty, deepfile, ledger


def reader(path):
    node = GafferImage.ImageReader()
    node['fileName'].setValue(path.as_posix())
    return node


def raw_t(events, z):
    t = 1
    for front, back, value, kind in events:
        if kind == 'surface':
            if front < z:
                t *= 1-value
        else:
            t *= math.exp(-value * max(0, min(1, (z-front)/(back-front))))
    return t


def analytic(media, x, y, w, h, z, surface, near, far):
    # The baseline's even 1024-entry symmetric box-filter table maps random
    # input .5 to .5 + 1/(4*511), not exactly to the pixel centre. See
    # util_cdf_invert and lookup_table_read; preserve the accepted beauty ray.
    offset = .5 + 1/(4*511)
    dx = (2*(x+offset)-w)/h*math.tan(.45)
    dy = (2*(h-1-y+offset)-h)/h*math.tan(.45)
    norm = math.sqrt(1+dx*dx+dy*dy)
    tau = 0
    for bounds, density in media:
        lo, hi = near, min(far, z)
        for d, lower, upper in zip((dx,dy,1), bounds[:3], bounds[3:]):
            if d == 0:
                if not lower <= 0 <= upper:
                    hi = lo
            else:
                a, b = sorted((lower/d, upper/d))
                lo, hi = max(lo,a), min(hi,b)
        tau += max(0, hi-lo)*norm*density
    return math.exp(-tau) * (1-float(surface) if 5 < z and near < 5 <= far else 1)


def validate(name, media, surface=False, near=.125, far=20, samples=1, w=16, h=12, extra=()):
    xml = scene(media, surface, near, far)
    paths = render(name, xml, w, h, samples, extra=extra)
    off = reader(render(name+'_off', xml, w, h, samples, False)[0])
    repeated_off = reader(render(name+'_off_repeat', xml, w, h, samples, False)[0]) if device == 'CUDA' and samples > 1 else None
    on, deep = reader(paths[0]), reader(paths[1])
    check(deep['out']['deep'].getValue(), 'Not a deep EXR')
    raw, identities = {}, set()
    for row in csv.DictReader(paths[2].open()):
        key = tuple(int(row[k]) for k in ('file_x','file_y','sample'))
        check(0 <= key[0] < w and 0 <= key[1] < h and 0 <= key[2] < samples, 'Bad raw identity')
        identity = (*key, int(row['event']))
        check(identity not in identities, 'Duplicate raw record')
        identities.add(identity)
        events = raw.setdefault(key, [])
        if row['kind'] != 'miss':
            events.append((*[float(row[k]) for k in ('front','back','value')], row['kind']))
    check(len(raw) == w*h*samples, 'Missing accepted camera samples')
    cut = GafferImage.DeepSlice()
    cut['in'].setInput(deep['out'])
    cut['nearClip']['enabled'].setValue(False)
    cut['farClip']['enabled'].setValue(True)
    cut['flatten'].setValue(True)
    maximum, analytic_error, beauty_error, repeat_error = 0, 0, 0, 0
    for depth in (.25,1.125,2.125,3.125,4.125,4.875,5.125,6.125,7.125,8.125,10):
        cut['farClip']['value'].setValue(depth)
        tiles = {}
        for y in range(h):
            for x in range(w):
                gy = h-1-y
                origin = (x//tile_size*tile_size, gy//tile_size*tile_size)
                if origin not in tiles:
                    tiles[origin] = cut['out'].channelData('A', imath.V2i(*origin))
                actual = tiles[origin][gy%tile_size*tile_size+x%tile_size]
                expected = 1-sum(raw_t(raw[x,y,s],depth) for s in range(samples))/samples
                maximum = max(maximum, abs(actual-expected))
                # Sample zero is the deterministic central camera ray, even in multisample runs.
                analytic_error = max(analytic_error, abs(raw_t(raw[x,y,0],depth)-
                    analytic(media,x,y,w,h,depth,surface,near,far)))
    for y in range(0,h,tile_size):
        for x in range(0,w,tile_size):
            for channel in ('R','G','B','A'):
                a = on['out'].channelData(channel,imath.V2i(x,y))
                b = off['out'].channelData(channel,imath.V2i(x,y))
                beauty_error = max(beauty_error, max(abs(p-q) for p,q in zip(a,b)))
                if repeated_off:
                    c = repeated_off['out'].channelData(channel,imath.V2i(x,y))
                    repeat_error = max(repeat_error, max(abs(p-q) for p,q in zip(b,c)))
    # Same CUDA FLOAT accumulation bound used by the qualified DOF suite.
    # Single-contribution CUDA and all CPU comparisons remain exact.
    beauty_tolerance = samples * 2**-23 if repeated_off else 0
    stats = {'max_raw_cut_error':maximum, 'max_analytic_error':analytic_error,
             'max_beauty_difference':beauty_error, 'max_off_repeat_difference':repeat_error,
             'beauty_tolerance':beauty_tolerance, 'size':[w,h], 'samples':samples}
    if baseline:
        reference = {}
        for row in csv.DictReader((baseline / (name + '.csv')).open()):
            key = tuple(int(row[k]) for k in ('file_x','file_y','sample'))
            events = reference.setdefault(key, [])
            if row['kind'] != 'miss':
                events.append((*[float(row[k]) for k in ('front','back','value')], row['kind']))
        check(raw.keys() == reference.keys(), name + ': CPU/CUDA sample identities differ')
        difference = max(abs(raw_t(events,z)-raw_t(reference[key],z))
            for key, events in raw.items()
            for z in (.25,1.125,2.125,3.125,4.125,4.875,5.125,6.125,7.125,8.125,10))
        stats['max_cpu_cuda_transmittance_difference'] = difference
        check(difference <= 2e-6, name + ': CPU/CUDA raw transmittance mismatch')
    report['fixtures'][name] = stats
    print(name, stats, flush=True)
    check(maximum <= 1e-6, name + ': Gaffer curve mismatch')
    check(analytic_error <= 2e-6, name + ': independent geometry/extinction mismatch')
    check(beauty_error <= beauty_tolerance and repeat_error <= beauty_tolerance,
          name + ': beauty accumulation bound exceeded')
    return paths


slab = [((-10,-10,2,10,10,8),.3)]
validate('homogeneous', slab)
validate('camera_inside', [((-10,-10,-2,10,10,4),.3)])
validate('near_clip_inside', slab, near=3)
validate('far_clip_inside', slab, far=3)
validate('overlap', [((-10,-10,2,10,10,6),.3),((-10,-10,4,10,10,8),.2)])
validate('surface_in_fog', slab, surface=True)
validate('transparent_in_fog', slab, surface=.4, samples=4)
validate('surface_before_fog', [((-10,-10,6,10,10,8),.3)], surface=True)
validate('surface_after_fog', [((-10,-10,2,10,10,4),.3)], surface=True)
validate('partial_coverage', [((-.9,-.8,3,.9,.8,7),.3)], samples=16)
validate('partial_coverage_single', [((-.9,-.8,3,.9,.8,7),.3)])
validate('zero_extinction', [((-10,-10,2,10,10,8),0)])
validate('exact_capacity', slab, extra=('--deep-max-events','1'))
validate('overlap_camera_inside', [((-10,-10,-2,10,10,6),.3),((-10,-10,-1,10,10,8),.2)])
validate('both_clips_inside', slab, near=3, far=4)
validate('volume_miss', [((20,20,2,21,21,8),.3)])
validate('odd_batch', [((-.9,-.8,3,.9,.8,7),.3)], samples=3, w=19, h=13)
valid = scene(slab)
for name, xml, extra in (
    ('reject_colored', valid.replace('color="0 0 0"','color="0 .2 0"'), ()),
    ('reject_negative', valid.replace('density="0.3"','density="-1"'), ()),
    ('reject_nan', valid.replace('density="0.3"','density="nan"'), ()),
    ('reject_dof', valid.replace('camera_type="perspective"','camera_type="perspective" aperturesize="0.1"'), ()),
    ('reject_adaptive', valid.replace('use_adaptive_sampling="false"','use_adaptive_sampling="true"'), ()),
    ('reject_motion', valid.replace('seed="123"','seed="123" motion_blur="true"'), ()),
    ('reject_heterogeneous', valid.replace('<absorption_volume name="v"', '<checker_texture name="c"/><absorption_volume name="v"').replace('<connect from="v volume"', '<connect from="c fac" to="v density"/><connect from="v volume"'), ()),
    ('reject_open', valid.replace('nverts="4 4 4 4 4 4"','nverts="4 4 4 4 4"').replace(' 1 2 6 5"','"'), ()),
    ('reject_osl', valid, ('--shadingsys','osl')),
    ('reject_reduction', valid, ('--deep-reduce',)),
    ('reject_memory', valid, ('--deep-memory-mb','1')),
    ('reject_capacity', scene(slab, surface=True), ('--deep-max-events','1')),
    ('reject_medium_capacity', scene([((-10,-10,2+i*.01,10,10,8+i*.01),0) for i in range(65)]), ()),
):
    render(name,xml,16,12,1,extra=extra,failure=True)
review_media = [((-1.5,-1,3,.4,1,6),.35),((-.3,-.8,5,1.5,.8,8),.55)]
paths = validate('review', review_media, samples=4, w=160, h=120)
script = Gaffer.ScriptNode()


def add(name,node,x,y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(x,y),
        flags=Gaffer.Plug.Flags.Default|Gaffer.Plug.Flags.Dynamic))
    return node


r = add('CyclesVolumeDeep',reader(paths[1]),0,40)
beauty = add('CyclesBeauty',reader(paths[0]),-30,40)
cut = add('VolumeDepthCut',GafferImage.DeepSlice(),0,20)
cut['in'].setInput(r['out'])
cut['farClip']['enabled'].setValue(True)
cut['farClip']['value'].setValue(9)
cut['nearClip']['enabled'].setValue(False)
cut['flatten'].setValue(False)
flat = add('CutAlpha',GafferImage.DeepToFlat(),-30,0)
flat['in'].setInput(cut['out'])
for name,channel,color,x in [('FrontPoints','Z',imath.Color3f(.1,.65,1),0),
                            ('BackPoints','ZBack',imath.Color3f(1,.35,.1),30)]:
    p = add(name,CyclesDeep.DeepToPointCloud(),x,0)
    p['in'].setInput(cut['out'])
    p['depthChannel'].setValue(channel)
    p['verticalFieldOfView'].setValue(math.degrees(.9))
    p['color'].setValue(color)
    p['useImageColor'].setValue(False)
    p['maxPoints'].setValue(1000000)
    check(p['out'].object('/deepPoints').numPoints > 0, 'Empty review cloud')
g = add('VolumePoints',GafferScene.Group(),15,-20)
g['in'][0].setInput(script['FrontPoints']['out'])
g['in'][1].setInput(script['BackPoints']['out'])
Gaffer.Metadata.registerValue(g,'description','Actual Cycles ' + device + ' absorption render. Blue Z, orange ZBack. '
    'Adjust VolumeDepthCut farClip; interval boundaries are not scattering particles.')
if baseline:
    cpu = add('CPUDeep', reader(baseline/'review.deep.exr'), 85, 40)
    cpu_cut = add('CPUDepthCut', GafferImage.DeepSlice(),85,20)
    cpu_cut['in'].setInput(cpu['out'])
    cpu_cut['flatten'].setValue(False)
    cpu_cut['farClip'].setInput(cut['farClip'])
    cpu_group = add('CPUVolumePoints', GafferScene.Group(),100,-20)
    for i, channel in enumerate(('Z','ZBack')):
        p = add('CPU'+('FrontPoints' if i == 0 else 'BackPoints'), CyclesDeep.DeepToPointCloud(),85+30*i,0)
        p['in'].setInput(cpu_cut['out'])
        p['depthChannel'].setValue(channel)
        p['verticalFieldOfView'].setValue(math.degrees(.9))
        p['useImageColor'].setValue(False)
        p['color'].setValue(imath.Color3f(.1,.65,1) if i == 0 else imath.Color3f(1,.35,.1))
        p['maxPoints'].setValue(1000000)
        check(p['out'].object('/deepPoints').numPoints > 0,'Empty CPU comparison cloud')
        cpu_group['in'][i].setInput(p['out'])
script['fileName'].setValue((out/('m8_' + device.lower() + '_volume_review.gfr')).as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
check(loaded['FrontPoints']['out'].object('/deepPoints').numPoints > 0, 'Reloaded cloud empty')
report['passed'] = True
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',script['fileName'].getValue(),flush=True)
