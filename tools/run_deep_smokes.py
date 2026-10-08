# SPDX-License-Identifier: Apache-2.0
"""Small surface/lens/motion and exact-UINT overlap checks for the shared runner."""
import json
import math
import subprocess
from pathlib import Path
from deep_exr import read, exact_flat, pixel, bound
from compare_deep_ids import name_hash, flattened_transmittance


def run_smokes(root,config,env,run,verify,native,numerical,ids_compare,backend_compare,result):
    material='''<shader name="near"><emission name="e" color=".3 .5 .7"/><transparent_bsdf name="t" color="1 1 1"/>
<mix_closure name="m" fac=".5"/><connect from="e emission" to="m closure1"/><connect from="t bsdf" to="m closure2"/><connect from="m closure" to="output surface"/></shader>'''
    plane='<state shader="near"><mesh P="-10 -10 5 10 -10 5 10 10 5 -10 10 5" nverts="4" verts="0 1 2 3"/></state>'
    base='''<cycles><camera camera_type="perspective" fov=".9"/><film filter_type="box" filter_width="1"/>
<integrator seed="123" use_adaptive_sampling="false"/><background transparent="true"><background name="b" color=".1 .1 .1"/><connect from="b background" to="output surface"/></background>'''+material+plane+'</cycles>'
    identity='1 0 0 0 0 1 0 0 0 0 1 '
    motion=base.replace(plane,'<object name="moving" motion="'+identity+'-1 '+identity+'1"/><state object="moving">'+plane+'</state>')
    motion=motion.replace('fov=".9"','fov=".9" shuttertime="1"').replace('seed="123"','seed="123" motion_blur="true"')
    fixtures=dict(surface=base.replace('fac=".5"','fac="0"'),transparency=base,
                  adaptive=base.replace('use_adaptive_sampling="false"','use_adaptive_sampling="true" adaptive_min_samples="8" adaptive_threshold=".1"'),
                  dof=base.replace('fov=".9"','fov=".9" aperturesize=".35" focaldistance="5"'),motion=motion)
    for device in ('CPU','CUDA'):
        for name,xml in fixtures.items():
            d=root/'surface'/device/name;d.mkdir(parents=True,exist_ok=True);source=d/'scene.xml';source.write_text(xml)
            samples=128 if name=='adaptive' else 16
            c=[config['cycles'],'--background','--quiet','--device',device,'--shadingsys','svm','--threads',8,
               '--width',16,'--height',12,'--samples',samples]
            run('surface-'+device+'-'+name,c+['--output',d/'beauty.exr','--deep-output',d/'scene.deep.exr',
                 '--deep-records',d/'scene.csv','--deep-error','strict','--deep-transparent','--deep-max-events',4,source])
            if device=='CPU':run('surface-'+device+'-'+name+'-off',c+['--output',d/'off.beauty.exr',source])
            v=verify(d,samples=samples,adaptive=name=='adaptive',ledger=d/'scene.csv')
            if device=='CPU':v['cpu_beauty']=exact_flat(d/'beauty.exr',d/'off.beauty.exr')
            if name=='adaptive' and not min(v['oracle']['accepted_populations'])<samples:raise ValueError('Adaptive surface failed to stop early')
    case=json.loads(config['known_cases'].read_text())['cases']['known_overlap']
    for device in ('CPU','CUDA'):
        base=root/'known'/device
        off=native(base/'beauty',case['scene'],case['samples'],100,device,deep=False) if device=='CPU' else None
        a=native(base/'noids',case['scene'],case['samples'],100,device,'1e-3')
        b=native(base/'ids',case['scene'],case['samples'],100,device,'1e-3',True)
        verify(a,off);v=verify(b,off);v['combined_alpha']=ids_compare(a,b)
        image=read(b/'scene.deep.exr');h,w=image.channels()['A'].pixels.shape;selection={}
        for name,tau in [('KnownFogA',1.8),('KnownFogB',1.2)]:
            identifier=name_hash(name);maximum=0.
            for y in range(h):
                for x in range(w):
                    values=pixel(image,x,y);ids=image.channels()['id'].pixels[y,x]
                    selected=[value for value,i in zip(values,ids) if int(i)==identifier]
                    if len(selected)!=1:raise ValueError('Known UINT selection lost its single interval')
                    maximum=max(maximum,abs(1-flattened_transmittance(selected)-(1-math.exp(-tau))))
            if maximum>1e-6:raise ValueError('Known UINT alpha selection failed')
            selection[name]=dict(passed=True,max_alpha_error=maximum,id=identifier)
        v['uint_selection']=selection
    repo=Path(__file__).resolve().parents[1]
    run('holdout-fixtures',[config['blender'],'--factory-startup','--background','--disable-autoexec','--python-exit-code',1,
                           '--python',repo/'src/deep/create_holdout_blender.py','--',root/'holdout-fixtures'])
    cases=json.loads((root/'holdout-fixtures/cases.json').read_text())['cases']
    for device in ('CPU','CUDA'):
        for name in ('object_inside','material_front'):
            case=cases[name];base=root/'holdout'/device/name
            ordinary=native(base/'ordinary',case['ordinary'],16,100,device,'strict')
            off=native(base/'beauty',case['holdout'],16,100,device,deep=False) if device=='CPU' else None
            for mode,ids in [('strict',False),('1e-3',True)]:
                d=native(base/mode,case['holdout'],16,100,device,mode,ids)
                v=verify(d,off)
                a,b=read(ordinary/'scene.deep.exr'),read(d/'scene.deep.exr');h,w=b.channels()['A'].pixels.shape
                from compare_deep_ids import curve_error
                maximum=max(curve_error(pixel(a,x,y),pixel(b,x,y)) for y in range(h) for x in range(w))
                if maximum>bound(b):raise ValueError('Holdout differs from ordinary opacity')
                v['ordinary_opacity_error']=maximum
                if ids and json.loads(b.header()['cycles:deepIDHoldoutManifest'])!={f'{name_hash(case["holdout_name"]):08x}':case['holdout_name']}:
                    raise ValueError('Holdout manifest lost marker')
    resource=config['cycles'].parent
    if 'raytrace_cases' in config:
        for device in ('CPU','CUDA','OPTIX'):
            for name,case in json.loads(config['raytrace_cases'].read_text())['cases'].items():
                base=root/'raytrace'/device/name
                if name == 'reject_bevel_opacity':
                    base.mkdir(parents=True,exist_ok=True)
                    target=base/'scene.deep.exr';target.write_bytes(b'preserve')
                    command=[config['blender'],'--factory-startup','--background','--disable-autoexec',case['scene'],
                             '--python-exit-code',1,'--python',repo/'tools/render_blender_deep_scene.py','--',
                             '--output',base,'--device',device,'--samples',case['samples'],'--percentage',100,
                             '--deep','--deep-volume','--deep-error','strict','--deep-max-events',16]
                    with (base/'rejection.log').open('w') as log:
                        p=subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT)
                    if not p.returncode or target.read_bytes()!=b'preserve' or any('.partial-' in f.name for f in base.iterdir()) or 'ray-traced bevel cannot drive deep opacity' not in (base/'rejection.log').read_text(errors='replace'):
                        raise ValueError('Bevel opacity atomic rejection failed: '+device)
                    result['cases'][f'raytrace/{device}/{name}']=dict(passed=True,rejection=True)
                    continue
                off=native(base/'beauty',case['scene'],case['samples'],100,device,deep=False) if device=='CPU' else None
                for mode,ids in [('strict',False),('1e-3',False),('1e-3',True)]:
                    d=native(base/(mode+('-ids' if ids else '')),case['scene'],case['samples'],100,device,mode,ids)
                    v=verify(d,off)
                    if ids:v['combined_alpha']=ids_compare(base/mode,d)
                    if device=='OPTIX':v['cuda_alpha']=numerical('raytrace-alpha-'+name+'-'+mode+'-'+str(ids),
                        lambda:backend_compare(root/'raytrace/CUDA'/name/d.name,d))
    run('cuda-lifecycle',[resource/'cycles_deep_output_driver_test.exe',resource,repo/'src/app/deep_output_driver_test.xml',
                          repo/'src/app/deep_output_driver_transparent_test.xml',repo/'src/app/deep_output_driver_volume_test.xml','CUDA'])
