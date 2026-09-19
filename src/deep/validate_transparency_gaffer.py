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


def run(exe, directory, device='CPU'):
    check(device in ('CPU', 'CUDA'), 'Expected CPU or CUDA device')
    backends = ('svm','osl') if device == 'CPU' else ('svm',)
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
    for name, attributes in (('orthographic', ''),
                             ('orthographic_dof', ' aperturesize="0.1" focaldistance="5" nearclip="0.5"')):
        fixtures[name] = (fixtures['stack'][0].replace(
            'camera_type="perspective"', 'camera_type="orthograph"'+attributes), [(2,.25),(8,.5)])
    identity = '1 0 0 0 0 1 0 0 0 0 1 0'
    fixtures['orthographic_static_motion_slots'] = (fixtures['orthographic'][0].replace(
        'camera_type="orthograph"', f'camera_type="orthograph" motion="{identity} {identity} {identity}"'),
        [(2,.25),(8,.5)])
    # Standard Blender materials retain their native shading. Refractive
    # transmission is not Transparent BSDF opacity: glass still has alpha 1.
    def closure(kind, attributes=''):
        return (f'<shader name="near"><{kind} name="c" {attributes}/>'
                '<connect from="c bsdf" to="output surface"/></shader>')
    fixtures.update({
        'principled_opaque': (scene(closure('principled_bsdf',
            'base_color="0.1 0.6 0.9" metallic="0.7"'), plane('near',2)), [(2,1)]),
        'principled_alpha': (scene(closure('principled_bsdf',
            'base_color="0.8 0.2 0.1" alpha="0.25"')+material('far',.5),
            plane('near',2)+plane('far',8)), [(2,.25),(8,.5)]),
        'principled_transmission': (scene(closure('principled_bsdf',
            'transmission_weight="1" roughness="0.1"'), plane('near',2)), [(2,1)]),
        'glass': (scene(closure('glass_bsdf', 'color="0.2 0.7 0.9"'),
            plane('near',2)), [(2,1)]),
        'translucent': (scene(closure('translucent_bsdf', 'color="0.7 0.2 0.1"'),
            plane('near',2)), [(2,1)]),
    })
    for filter_name in ('gaussian', 'blackman_harris'):
        fixtures[filter_name] = (fixtures['cutout'][0].replace(
            'filter_type="box" filter_width="1"',
            f'filter_type="{filter_name}" filter_width="1.5"'), None)
    lights = ''.join(f'<transform translate="0 0 {z}"><light light_type="{kind}" '
                     'strength="2 3 4"/></transform>'
                     for kind,z in (('area',1),('point',3),('spot',4),('sun',6)))
    fixtures['analytic_lights'] = (scene(stack, lights+plane('near',2)+plane('far',8)),
                                   [(2,.25),(8,.5)])
    fixtures['analytic_lights_empty'] = (scene('', lights), [])
    procedural = '''<shader name="near">
<texture_coordinate name="uv"/><mapping name="map"/>
<noise_texture name="noise" scale="8"/><gradient_texture name="grad"/>
<rgb_ramp name="ramp" ramp="0.1 0.2 0.3 0.7 0.8 0.9" ramp_alpha="1 1"/>
<invert name="inv"/><mix_color name="mix" b="0.2 0.4 0.8"/>
<bump name="bump" distance="0.02"/><principled_bsdf name="p" alpha="0.25"/>
<connect from="uv UV" to="map vector"/><connect from="map vector" to="noise vector"/>
<connect from="map vector" to="grad vector"/><connect from="noise fac" to="ramp fac"/>
<connect from="ramp color" to="inv color"/><connect from="inv color" to="mix A"/>
<connect from="grad fac" to="mix fac"/><connect from="mix Result" to="p base_color"/>
<connect from="noise fac" to="bump Height"/><connect from="bump Normal" to="p Normal"/>
<connect from="p BSDF" to="output surface"/></shader>'''
    fixtures['procedural_principled'] = (scene(procedural+material('far',.5),
        plane('near',2)+plane('far',8)), [(2,.25),(8,.5)])
    fixtures['numeric_socket_conversion'] = (fixtures['procedural_principled'][0].replace(
        'from="map vector" to="noise vector"', 'from="grad fac" to="noise vector"'), [(2,.25),(8,.5)])
    fixtures['denoised_principled'] = (fixtures['procedural_principled'][0].replace(
        'seed="123"', 'seed="123" use_denoise="true" denoise_use_gpu="false"'),
        [(2,.25),(8,.5)])
    # CPU denoising is qualified. CUDA input accumulation variation can be
    # amplified by denoising beyond the current beauty bound; keep it fail-closed.
    cuda_denoise = fixtures.pop('denoised_principled')[0] if device=='CUDA' else None
    report = {'gaffer': Gaffer.About.versionString(), 'device': device,
              'beauty_relative_bound': S*2**-23 if device=='CUDA' else 0,
              'scenes': {}, 'rejections': []}

    def render(name, xml, backend, deep=True, extra=(), failure=False):
        name += '_' + backend
        source = directory / (name+'.xml'); source.write_text(xml)
        beauty = directory / (name+'.exr'); output = directory / (name+'.deep.exr')
        records = directory / (name+'.csv')
        cmd = [str(exe), '--device',device,'--background','--quiet','--samples',str(S),
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
        stats = {'max_curve_error':0, 'max_backend_alpha_error':0, 'max_depth_error':0,
                 'max_beauty_error':0, 'max_off_repeat_error':0}
        for backend in backends:
            paths = render(name, xml, backend)
            off = reader(render(name+'_off', xml, backend, False)[0])
            on, deep = reader(paths[0]), reader(paths[1])
            flat = GafferImage.DeepToFlat(); flat['in'].setInput(deep['out'])
            if name == 'denoised_principled':
                noisy = reader(directory / ('procedural_principled_'+backend+'.exr'))
                denoise_change = max(abs(float(a)-float(b))
                    for c in ('R','G','B')
                    for a,b in zip(on['out'].channelData(c,imath.V2i(0)),
                                   noisy['out'].channelData(c,imath.V2i(0))))
                check(denoise_change > 1e-7, 'Denoising fixture did not change RGB')
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
                        error = abs(a-b)
                        stats['max_beauty_error'] = max(stats['max_beauty_error'],error)
                        # CUDA floating accumulation order also varies between
                        # deep-disabled runs, including alpha for Principled.
                        # Use the existing sample-count-scaled FLOAT criterion;
                        # the separate deep-curve oracle remains at 1e-6.
                        bound = (S*2**-23*max(1,abs(a),abs(b))
                                 if device=='CUDA' else 0)
                        check(math.isfinite(a) and math.isfinite(b) and error<=bound,
                              f'{name}/{backend}: beauty changed beyond bound ({error})')
                    actual = deep_pixel(deep['out'],point)
                    expected_alpha = 1-sum(math.prod(1-a for z,a in raw[x,y,s]) for s in range(S))/S
                    check(abs(float(flat['out'].channelData('A',origin)[index])-expected_alpha)<1e-6, 'DeepToFlat alpha mismatch')
                    if name in ('gaussian', 'blackman_harris', 'analytic_lights', 'analytic_lights_empty'):
                        check(abs(float(flat['out'].channelData('A',origin)[index])-
                                  float(on['out'].channelData('A',origin)[index]))<1e-6,
                              f'{name}: native camera alpha disagrees with deep visibility')
                    boundaries = sorted({z for s in range(S) for z,a in raw[x,y,s]})
                    for z in boundaries + [100.]:
                        for inclusive in (False,True):
                            def before(depth): return depth<=z if inclusive else depth<z
                            expected = sum(math.prod(1-a for depth,a in raw[x,y,s] if before(depth)) for s in range(S))/S
                            observed = math.prod(1-a for depth,back,a in actual if before(depth))
                            error = abs(expected-observed)
                            stats['max_curve_error']=max(stats['max_curve_error'],error)
                            check(error<1e-6, f'{name}/{backend}: transmittance mismatch {error}')
            if device=='CUDA' and stats['max_beauty_error']:
                repeat = reader(render(name+'_off_repeat',xml,backend,False)[0])
                for c in ('R','G','B','A'):
                    for a,b in zip(off['out'].channelData(c,imath.V2i(0)),
                                   repeat['out'].channelData(c,imath.V2i(0))):
                        error = abs(float(a)-float(b))
                        stats['max_off_repeat_error'] = max(stats['max_off_repeat_error'],error)
                        check(math.isfinite(error) and error <= (
                            S*2**-23*max(1,abs(a),abs(b))),
                            'Deep-disabled CUDA repeat exceeds beauty bound')
        report['scenes'][name]=stats
    for backend in backends:
        if cuda_denoise is not None:
            render('cuda_denoise',cuda_denoise,backend,failure=True)
        else:
            render('denoise_upscale',fixtures['denoised_principled'][0].replace(
                'use_denoise="true"', 'use_denoise="true" denoiser_upscale_factor="2"'),
                backend,failure=True)
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
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
        sys.argv[3] if len(sys.argv)>3 else 'CPU')
