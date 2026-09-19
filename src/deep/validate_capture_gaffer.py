# SPDX-License-Identifier: Apache-2.0
"""Gaffer CLI acceptance: gaffer python validate_capture_gaffer.py CYCLES OUTPUT_DIR.

Renders fixed-sample analytic scenes with deep off/on, compares beauty pixels,
checks raw camera observations against known planes, and checks Gaffer deep
transmittance against an independent count of raw hits (including misses).
"""
import csv
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time

import Gaffer
import GafferImage
import imath

sys.path.insert(0, str(Path(sys.argv[0]).resolve().parent))
from validate_gaffer import deep_pixel, flat_alpha, tile_index, check

WIDTH, HEIGHT, SAMPLES = 32, 24, 32


def mesh(points):
    return '<state shader="opaque"><mesh P="' + ' '.join(str(v) for p in points for v in p) + '" nverts="4" verts="0 1 2 3"/></state>'


def scene(geometry):
    return '''<cycles>
<camera width="32" height="24" camera_type="perspective" fov="0.9"/>
<film filter_type="box" filter_width="1"/>
<integrator use_adaptive_sampling="false" seed="123"/>
<background transparent="true"><background name="bg" color="0.1 0.1 0.1"/>
<connect from="bg background" to="output surface"/></background>
<shader name="opaque"><emission name="e" color="0.3 0.5 0.7" strength="1"/>
<connect from="e emission" to="output surface"/></shader>
''' + geometry + '</cycles>'


def read_image(path):
    node = GafferImage.ImageReader()
    node['fileName'].setValue(path.as_posix())
    return node


def run(exe, directory):
    directory.mkdir(parents=True, exist_ok=True)
    plane = mesh([(-10,-10,4),(10,-10,4),(10,10,4),(-10,10,4)])
    edge = mesh([(-10,-10,2),(-2.9,-10,2),(3.1,10,2),(-10,10,2)])
    far = mesh([(-20,-20,8),(20,-20,8),(20,20,8),(-20,20,8)])
    tiny = mesh([(-.08,.23,3),(.08,.23,3),(.08,.4,3),(-.08,.4,3)])
    translated = mesh([(-10,-10,6),(10,-10,6),(10,10,6),(-10,10,6)])
    fixtures = {'plane': (plane, [4]), 'edge': (edge, [2]),
                'near_far': (edge+far, [2,8]), 'tiny': (tiny, [3]), 'miss': ('', []),
                'translated_camera': (translated, [4]), 'diffuse': (plane, [4])}
    report = {'gaffer': Gaffer.About.versionString(), 'scenes': {}, 'rejections': []}

    def render(name, xml, deep, extra=(), expect_error=None):
        source = directory / (name + '.xml')
        source.write_text(xml)
        beauty = directory / (name + '.exr')
        output = directory / (name + '.deep.exr')
        records = directory / (name + '.csv')
        cmd = [str(exe), '--device', 'CPU', '--background', '--quiet', '--samples', str(SAMPLES),
               '--threads','4','--width',str(WIDTH),'--height',str(HEIGHT),'--output',str(beauty)]
        if deep:
            cmd += ['--deep-output',str(output),'--deep-records',str(records)]
        cmd += list(extra) + [str(source)]
        if expect_error:
            output.write_bytes(b'preserve-existing-output')
        start = time.perf_counter()
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        elapsed = time.perf_counter()-start
        (directory/(name+'.log')).write_text(result.stdout+result.stderr)
        if expect_error:
            check(result.returncode != 0 and expect_error in result.stderr,
                  f'{name}: missing diagnostic {expect_error}: {result.stderr}')
            check(output.read_bytes()==b'preserve-existing-output', f'{name}: invalid scene overwrote output')
            report['rejections'].append(name)
        else:
            check(result.returncode==0, f'{name}: render failed: {result.stderr}')
            check('ERROR:' not in result.stderr, f'{name}: scene parser reported an error: {result.stderr}')
        return beauty, output, records, elapsed

    for name, (geometry, depths) in fixtures.items():
        xml = scene(geometry)
        if name=='translated_camera':
            xml=xml.replace('<camera width="32" height="24" camera_type="perspective" fov="0.9"/>',
                            '<transform translate="0 0 2"><camera width="32" height="24" camera_type="perspective" fov="0.9" nearclip="1"/></transform>')
        if name=='diffuse':
            xml=xml.replace('<emission name="e"','<diffuse_bsdf name="e"').replace('from="e emission"','from="e bsdf"').replace(' strength="1"','')
        flat_path, _, _, off_time = render(name+'_off', xml, False)
        beauty_path, deep_path, records_path, on_time = render(name, xml, True)
        raw = {(x,y): [] for y in range(HEIGHT) for x in range(WIDTH)}
        with records_path.open(newline='') as f:
            rows = list(csv.DictReader(f))
        max_depth_error=0
        check(len(rows)==WIDTH*HEIGHT*SAMPLES, 'incomplete raw observations')
        for row in rows:
            x,y,s = (int(row[k]) for k in ('file_x','file_y','sample'))
            check(s==len(raw[x,y]), 'duplicate or missing raw sample identity')
            z = struct.unpack('f', struct.pack('f',float(row['depth'])))[0]
            check(z==0 or any(abs(z-d)<2e-5 for d in depths), f'{name}: non-axial depth {z}')
            if z>0: max_depth_error=max(max_depth_error,min(abs(z-d) for d in depths))
            raw[x,y].append(z)
        if name in ('plane','translated_camera','diffuse'):
            check(all(z>0 for zs in raw.values() for z in zs), 'full plane missed')
        if name=='miss': check(all(z==0 for zs in raw.values() for z in zs), 'miss produced geometry')
        off,on,reader = [read_image(p) for p in (flat_path,beauty_path,deep_path)]
        flatten = GafferImage.DeepToFlat(); flatten['in'].setInput(reader['out'])
        clip = GafferImage.DeepSlice(); clip['in'].setInput(reader['out'])
        clip['nearClip']['enabled'].setValue(False); clip['farClip']['enabled'].setValue(True)
        clip['flatten'].setValue(True)
        fmt = reader['out']['format'].getValue()
        check(reader['out']['deep'].getValue(), 'not native deep')
        check(set(reader['out'].channelNames())=={'Z','ZBack','A'}, 'wrong deep channels')
        result = {'max_beauty_error':0, 'max_alpha_error':0, 'max_curve_error':0,
                  'max_depth_error':max_depth_error,
                  'partial_pixels':0, 'mixed_depth_pixels':0, 'samples':0,
                  'off_seconds':off_time, 'on_seconds':on_time}
        for (x,y), zs in raw.items():
            point = fmt.fromEXRSpace(imath.V2i(x,y))
            origin,index = tile_index(point)
            for c in ('R','G','B','A'):
                a = float(off['out'].channelData(c,origin)[index]); b = float(on['out'].channelData(c,origin)[index])
                check(math.isfinite(a) and a==b, f'{name}: beauty changed at {x},{y},{c}: {a} != {b}')
            actual = deep_pixel(reader['out'],point)
            result['samples'] += len(actual)
            last = 0
            for z,back,a in actual:
                check(math.isfinite(z) and z>last and z==back and 0<a<=1, 'invalid deep sample')
                last=z
            for cut in sorted(set(zs + [z for z,_,_ in actual])):
                if cut<=0: continue
                for inclusive in (False,True):
                    expected = sum(z==0 or (z>cut if inclusive else z>=cut) for z in zs)/SAMPLES
                    observed = math.prod(1-a for z,_,a in actual if (z<=cut if inclusive else z<cut))
                    error=abs(expected-observed)
                    check(error<1e-6, f'{name}: transmittance mismatch at {x},{y},cut={cut}')
                    result['max_curve_error']=max(result['max_curve_error'],error)
            alpha=sum(z>0 for z in zs)/SAMPLES
            result['partial_pixels'] += 0<alpha<1
            result['mixed_depth_pixels'] += any(0<z<4 for z in zs) and any(z>6 for z in zs)
            for observed in (flat_alpha(flatten['out'],point), flat_alpha(on['out'],point)):
                error=abs(alpha-observed); check(error<1e-6, f'{name}: coverage/orientation mismatch')
                result['max_alpha_error']=max(result['max_alpha_error'],error)
        for cut in (1,3,6,10):
            clip['farClip']['value'].setValue(cut)
            for (x,y),zs in raw.items():
                expected=sum(0<z<cut for z in zs)/SAMPLES
                observed=flat_alpha(clip['out'],fmt.fromEXRSpace(imath.V2i(x,y)))
                check(abs(expected-observed)<1e-6, f'{name}: Gaffer depth slice mismatch')
        if name in ('edge','tiny'): check(result['partial_pixels']>0, 'fixture has no partial coverage')
        if name=='near_far': check(result['mixed_depth_pixels']>0, 'fixture has no mixed depths')
        report['scenes'][name]=result
        print(name, result, flush=True)

    # Identical sample data across worker counts and repeated renders.
    _,_,repeat,_=render('near_far_repeat',scene(edge+far),True,('--threads','1'))
    check(repeat.read_bytes()==(directory/'near_far.csv').read_bytes(), 'capture varies with worker count')
    report['repeat_records_identical']=True
    valid=scene(plane)
    bad = [
        ('invalid_filter_width',valid.replace('filter_width="1"','filter_width="0"'),(), 'positive-width'),
        ('invalid_focus',valid.replace('fov="0.9"','fov="0.9" focaldistance="0"'),(), 'focal distance'),
        ('rolling',valid.replace('fov="0.9"','fov="0.9" rolling_shutter_type="top"'),(), 'rolling shutter'),
        ('transparent',valid.replace('<emission name="e"','<transparent_bsdf name="e"').replace('from="e emission"','from="e bsdf"'),(), 'constant diffuse'),
        ('osl',valid,('--shadingsys','osl'), 'OSL'),
        ('budget',valid,('--width','1024','--height','1024','--deep-memory-mb','1'), 'budget'),
        ('zero_samples',valid,('--samples','0'), 'maximum samples'),
        ('tiling',valid,('--tile-size','16'), 'tiling'),
        ('panorama',valid.replace('camera_type="perspective"','camera_type="panorama"'),(), 'perspective'),
        ('volume',valid.replace('to="output surface"/></shader>', 'to="output surface"/><connect from="e emission" to="output volume"/></shader>'),(), 'volume'),
        ('invalid_budget',valid,('--deep-memory-mb','0'), 'budget'),
        ('alias',valid,('--deep-output',str(directory/'reject_alias.exr')), 'distinct'),
        ('output_directory',valid,('--deep-output',str(directory)), 'Deep render failed'),
    ]
    for name,xml,extra,message in bad: render('reject_'+name,xml,True,extra,message)
    script=Gaffer.ScriptNode()
    script['reader']=read_image(directory/'near_far.deep.exr')
    script['flatten']=GafferImage.DeepToFlat(); script['flatten']['in'].setInput(script['reader']['out'])
    script['nearOpacity']=GafferImage.DeepSlice(); script['nearOpacity']['in'].setInput(script['reader']['out'])
    script['nearOpacity']['nearClip']['enabled'].setValue(False)
    script['nearOpacity']['farClip']['enabled'].setValue(True)
    script['nearOpacity']['farClip']['value'].setValue(4)
    script['nearOpacity']['flatten'].setValue(True)
    script['fileName'].setValue((directory/'capture_validation.gfr').as_posix()); script.save()
    (directory/'report.json').write_text(json.dumps(report,indent=2))
    print('M3 Gaffer acceptance passed',flush=True)


if __name__=='__main__':
    run(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
