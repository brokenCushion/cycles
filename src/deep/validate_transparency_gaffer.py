# SPDX-License-Identifier: Apache-2.0
"""M4 acceptance: gaffer python validate_transparency_gaffer.py CYCLES OUTPUT_DIR.

Small standalone Cycles renders; Gaffer is the independent deep EXR reader.
Uses ordinary graphs compiled by both SVM and OSL, never a deep-only shader.
"""
import csv
import json
import math
from pathlib import Path
import subprocess
import struct
import sys

import Gaffer
import GafferImage
import imath

sys.path.insert(0, str(Path(sys.argv[0]).resolve().parent))
from validate_gaffer import check, deep_pixel, tile_index

W, H, S = 16, 12, 16


def material(name, transmission, source=''):
    return f'''<shader name="{name}">
<emission name="e" color="0.3 0.5 0.7"/>
<transparent_bsdf name="t" color="1 1 1"/>
<mix_closure name="mix" fac="{transmission}"/>
<connect from="e emission" to="mix closure1"/>
<connect from="t bsdf" to="mix closure2"/>
{source}<connect from="mix closure" to="output surface"/></shader>'''


def plane(name, z, transform=''):
    node = f'''<state shader="{name}"><mesh P="-10 -10 {z} 10 -10 {z} 10 10 {z} -10 10 {z}"
 nverts="4" verts="0 1 2 3" UV="0 0 1 0 1 1 0 1"/></state>'''
    return f'<transform {transform}>{node}</transform>' if transform else node


def scene(materials, geometry):
    return f'''<cycles><camera width="{W}" height="{H}" camera_type="perspective" fov="0.9"/>
<film filter_type="box" filter_width="1"/>
<integrator use_adaptive_sampling="false" seed="123"/>
<background transparent="true"><background name="bg" color="0.1 0.1 0.1"/>
<connect from="bg background" to="output surface"/></background>
{materials}{geometry}</cycles>'''


def reader(path):
    n = GafferImage.ImageReader()
    n['fileName'].setValue(path.as_posix())
    return n


def run(exe, directory):
    directory.mkdir(parents=True, exist_ok=True)
    texture = directory / 'opacity.ppm'
    texture.write_bytes(b'P6\n2 2\n255\n' + bytes([64]*3 + [192]*3 + [192]*3 + [64]*3))
    checker = '''<texture_coordinate name="uv"/><checker_texture name="c" scale="7"/>
<connect from="uv generated" to="c vector"/><connect from="c fac" to="mix fac"/>'''
    tex = '''<image_texture name="c" filename="opacity.ppm" colorspace="raw" interpolation="closest"/>
<connect from="c color" to="t color"/>'''
    stack = material('near', .75) + material('far', .5)
    fixtures = {
        'stack': (scene(stack, plane('near', 2)+plane('far', 8)), [(2,.25),(8,.5)]),
        'diffuse_stack': (scene(stack.replace('<emission name="e"', '<diffuse_bsdf name="e"').replace('from="e emission"', 'from="e bsdf"'), plane('near',2)+plane('far',8)), [(2,.25),(8,.5)]),
        'opaque_back': (scene(stack+material('back',0), plane('near',2)+plane('far',8)+plane('back',10)), [(2,.25),(8,.5),(10,1)]),
        'miss': (scene(material('near',.5), ''), []),
        'clear': (scene(material('near',1), plane('near',2)), [(2,0)]),
        'cutoff': (scene(material('near',1e-8), plane('near',2)), [(2,1)]),
        'beauty_limit': (scene(stack, plane('near',2)+plane('far',8)).replace('seed="123"','seed="123" transparent_max_bounce="1"'), [(2,.25),(8,.5)]),
        'cutout': (scene(material('near',.5,checker)+material('far',.5), plane('near',2,'translate="0.3 0.2 0" scale="0.7 0.6 1"')+plane('far',8)), None),
        'texture': (scene(material('near',1,tex), plane('near',2)), None),
    }
    report = {'gaffer': Gaffer.About.versionString(), 'scenes': {}, 'rejections': []}

    def render(name, xml, backend, deep=True, extra=(), failure=False):
        name += '_' + backend
        source = directory / (name+'.xml'); source.write_text(xml)
        beauty = directory / (name+'.exr'); output = directory / (name+'.deep.exr')
        records = directory / (name+'.csv')
        cmd = [str(exe), '--device','CPU','--background','--quiet','--samples',str(S),
               '--width',str(W),'--height',str(H),
               '--threads','4','--shadingsys',backend,'--output',str(beauty)]
        if deep:
            cmd += ['--deep-output',str(output),'--deep-records',str(records),'--deep-transparent']
        cmd += list(extra) + [str(source)]
        if failure: output.write_bytes(b'preserve-existing-output')
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        (directory/(name+'.log')).write_text(result.stdout+result.stderr)
        if failure:
            check(result.returncode != 0 and 'Deep' in result.stderr, f'{name}: expected failure: {result.stderr}')
            check(output.read_bytes()==b'preserve-existing-output', f'{name}: partial traversal published')
            report['rejections'].append(name)
        else:
            check(result.returncode==0 and 'ERROR:' not in result.stderr, f'{name}: {result.stderr}')
        return beauty, output, records

    for name, (xml, analytic) in fixtures.items():
        native = None
        stats = {'max_curve_error':0, 'max_backend_alpha_error':0, 'max_depth_error':0}
        for backend in ('svm','osl'):
            paths = render(name, xml, backend)
            off = reader(render(name+'_off', xml, backend, False)[0])
            on, deep = reader(paths[0]), reader(paths[1])
            flat = GafferImage.DeepToFlat(); flat['in'].setInput(deep['out'])
            check(deep['out']['deep'].getValue(), 'not deep EXR')
            raw = {(x,y,s): [] for y in range(H) for x in range(W) for s in range(S)}
            seen = set()
            for row in csv.DictReader(paths[2].open()):
                key = tuple(int(row[k]) for k in ('file_x','file_y','sample'))
                check(key in raw, 'invalid sample identity')
                i = int(row['event'])
                z,a = (struct.unpack('f',struct.pack('f',float(row[k])))[0] for k in ('depth','alpha'))
                if i < 0:
                    check(key not in seen and z==a==0, 'invalid miss')
                else:
                    check(i==len(raw[key]), 'missing/duplicate event')
                    raw[key].append((z,a))
                seen.add(key)
            check(len(seen)==W*H*S, 'incomplete camera ledger')
            if analytic is not None:
                for events in raw.values():
                    check(len(events)==len(analytic), f'{name}: wrong chain length')
                    for (z,a),(ez,ea) in zip(events,analytic):
                        check(abs(z-ez)<2e-5 and abs(a-ea)<1e-6, f'{name}: analytic mismatch {events}')
                        stats['max_depth_error']=max(stats['max_depth_error'],abs(z-ez))
            if name=='cutout':
                check({events[0][1] for events in raw.values()}=={0,1}, 'procedural cutout did not vary')
            if name=='texture':
                alphas = [e[0][1] for e in raw.values()]
                check(max(alphas)-min(alphas)>.4 and all(0<a<1 for a in alphas), 'texture opacity did not vary')
                check(all(min(abs(a-(1-v/255)) for v in (64,192))<1e-6 for a in alphas), 'texture opacity disagrees with texel values')
            if native is not None:
                for key,events in raw.items():
                    check(len(events)==len(native[key]), 'SVM/OSL chain mismatch')
                    for (z,a),(nz,na) in zip(events,native[key]):
                        check(abs(z-nz)<2e-5 and abs(a-na)<1e-6, f'SVM/OSL event mismatch: {events} vs {native[key]}')
                        stats['max_backend_alpha_error']=max(stats['max_backend_alpha_error'],abs(a-na))
            else: native = raw
            fmt = deep['out']['format'].getValue()
            for y in range(H):
                for x in range(W):
                    point = fmt.fromEXRSpace(imath.V2i(x,y)); origin,index = tile_index(point)
                    for c in ('R','G','B','A'):
                        a,b = (float(n['out'].channelData(c,origin)[index]) for n in (off,on))
                        check(math.isfinite(a) and a==b, f'{name}/{backend}: beauty changed')
                    actual = deep_pixel(deep['out'],point)
                    expected_alpha = 1-sum(math.prod(1-a for z,a in raw[x,y,s]) for s in range(S))/S
                    check(abs(float(flat['out'].channelData('A',origin)[index])-expected_alpha)<1e-6, 'DeepToFlat alpha mismatch')
                    boundaries = sorted({z for s in range(S) for z,a in raw[x,y,s]})
                    for z in boundaries + [100.]:
                        for inclusive in (False,True):
                            def before(depth): return depth<=z if inclusive else depth<z
                            expected = sum(math.prod(1-a for depth,a in raw[x,y,s] if before(depth)) for s in range(S))/S
                            observed = math.prod(1-a for depth,back,a in actual if before(depth))
                            error = abs(expected-observed)
                            stats['max_curve_error']=max(stats['max_curve_error'],error)
                            check(error<1e-6, f'{name}/{backend}: transmittance mismatch {error}')
        report['scenes'][name]=stats
    for backend in ('svm','osl'):
        render('limit', fixtures['stack'][0], backend, extra=('--deep-max-events','1'), failure=True)
        render('exact_limit', fixtures['stack'][0], backend, extra=('--deep-max-events','2'))
        render('colored', scene(material('near',1).replace('color="1 1 1"','color="0.2 0.5 0.8"'),plane('near',2)), backend, failure=True)
        render('refraction', scene(material('near',1).replace('transparent_bsdf','refraction_bsdf'),plane('near',2)), backend, failure=True)
        render('clear_limit', scene(material('near',1),plane('near',2)+plane('near',4)), backend, extra=('--deep-max-events','1'), failure=True)
        repeated = render('stack_single_thread', fixtures['stack'][0], backend, extra=('--threads','1'))[2]
        check(repeated.read_bytes()==(directory/('stack_'+backend+'.csv')).read_bytes(), 'thread count changed event ledger')
    (directory/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
