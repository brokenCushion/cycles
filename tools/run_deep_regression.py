# SPDX-License-Identifier: Apache-2.0
"""One numerical regression command. No Gaffer process or module is required."""
import argparse
from contextlib import redirect_stdout
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid
import numpy as np
from compare_deep_identity import compare, toolchain_difference, cross_comparison_passed, CROSS_ALPHA_LIMIT
from compare_deep_ids import curve_error, name_hash
from deep_exr import read, bound, pixel, exact_flat
from validate_deep_render import validate, boundary_analytic
from record_beauty_build import beauty_identity

REPO=Path(__file__).resolve().parents[1]
SCRATCH=Path('D:/CyclesDeepScratch/regression')
GPU_PATHS=['src/kernel','src/device','src/integrator/path_trace_work_gpu.cpp','src/integrator/path_trace_work_gpu.h']


def cleanup(root):
    root=Path(root)
    if root.resolve().parent!=SCRATCH.resolve() or not re.fullmatch(r'[A-Za-z0-9_-]+',root.name):
        raise ValueError('Cleanup target is outside the owned regression workspace')
    for directory,dirs,files in os.walk(root):
        for p in [Path(directory),*(Path(directory)/n for n in dirs+files)]:
            if p.lstat().st_file_attributes & 0x400:
                raise ValueError('Refusing cleanup through a reparse point: '+str(p))
    shutil.rmtree(root)


def main():
    # Fail before rendering if the separate measurement/reader dependencies are absent.
    import OpenEXR,psutil
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,default=REPO/'tools/deep_regression_config.json')
    parser.add_argument('--keep',action='store_true',help='Keep the owned D: run folder after PASS')
    parser.add_argument('--optix',action='store_true',help='Also qualify OptiX SVM matrices and boundaries')
    parser.add_argument('--toolchain-audit',action='store_true',help='Audit the approved one-time compiler change; stage new references only, not GPU qualification')
    parser.add_argument('--resume-audit','--resume',type=Path,help='Resume retained results.json without repeating completed renders')
    parser.add_argument('--cuda-beauty',type=Path,help='Optional separate calibrated CUDA beauty stage configuration')
    args=parser.parse_args();os.chdir(REPO)
    if args.toolchain_audit and (args.optix or args.cuda_beauty):
        parser.error('Toolchain audit is separate from OptiX/GPU beauty qualification')
    if args.toolchain_audit:args.keep=True
    settings=json.loads(args.config.read_text());source_baseline=settings.pop('source_baseline')
    config={k:Path(v).resolve() for k,v in settings.items()}
    for path in config.values():
        if not path.exists():raise FileNotFoundError(path)
    run_id=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    previous=json.loads(args.resume_audit.read_text()) if args.resume_audit else None
    if previous:
        if bool(previous.get('toolchain_audit'))!=args.toolchain_audit:raise ValueError('Retained run uses a different audit mode')
        root=Path(previous['root']);run_id=previous['run_id']
        if root.resolve().parent!=SCRATCH.resolve() or root.is_junction():raise ValueError('Foreign audit workspace')
        report_dir=args.resume_audit.resolve().parent
        (report_dir/'results-before-resume.json').write_text(json.dumps(previous,indent=2)+'\n')
    else:
        root=SCRATCH/run_id;root.mkdir(parents=True,exist_ok=False)
        report_dir=REPO/'builds/validation/deep-regression'/run_id;report_dir.mkdir(parents=True)
    env=dict(os.environ,TEMP=str(root/'temp'),TMP=str(root/'temp'),
             BLENDER_USER_RESOURCES=str(SCRATCH.parent/'regression-cache'))
    (root/'temp').mkdir(exist_ok=bool(previous))
    for key in ('OCIO','CYCLES_KERNEL_PATH','CYCLES_DEEP_Z_BASELINE','CYCLES_DEEP_VALIDATE_CAPTURE_ONLY','CYCLES_DEEP_HOST_ONLY_BEAUTY_PROOF'):
        env.pop(key,None)
    started=time.monotonic();result=dict(passed=False,run_id=run_id,root=str(root),keep=args.keep,
                                       toolchain_audit=args.toolchain_audit,stages=[],cases={},identity={})
    cached={s['name'] for s in previous['stages'] if not s['exit_code']} if previous else set()
    if previous:result['stages']=previous['stages'];result['resumed']=True
    def save():(report_dir/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    def run(label,command):
        if label in cached:return
        t=time.monotonic()
        with (root/(label+'.log')).open('w') as log:
            p=subprocess.run(list(map(str,command)),env=env,cwd=REPO,stdout=log,stderr=subprocess.STDOUT)
        result['stages'].append(dict(name=label,seconds=time.monotonic()-t,exit_code=p.returncode));save()
        if p.returncode:raise RuntimeError(label+' failed; evidence retained: '+str(root/(label+'.log')))
    def numerical(label,operation):
        t=time.monotonic()
        with (root/(label+'.log')).open('w') as log,redirect_stdout(log):value=operation()
        result['stages'].append(dict(name=label,seconds=time.monotonic()-t,exit_code=0));save()
        return value
    def identity(label,reference,target,mode):
        value=toolchain_difference(reference,target,mode) if args.toolchain_audit else compare(reference,target)
        if args.toolchain_audit:
            evidence=root/'oracle-before'/reference.relative_to(config['golden']).parent
            evidence.mkdir(parents=True,exist_ok=True)
            if reference.name=='scene.deep.exr':
                old=numerical(label.replace('/','-')+'-old-oracle',lambda:validate(reference.parent,output=evidence))
            else:
                # Standalone references retain their all-pixel ledgers beside named EXRs.
                shutil.copy2(reference,evidence/'scene.deep.exr')
                name=reference.name.removesuffix('.deep.exr')
                case=next(c for c in json.loads((REPO/'tools/deep_boundary_cases.json').read_text())['cases'] if c['name']==name)
                old=numerical(label+'-old-oracle',lambda:validate(evidence,samples=case['samples'],adaptive=False,ledger=reference.parent/(name+'.csv')))
            value['before_oracle']=old['oracle']
        result['identity'].setdefault(mode,{})[label]=value;save()
        if args.toolchain_audit:
            if not value['audit_passed']:raise RuntimeError('Toolchain flattened-alpha sanity gate failed: '+label+'; '+str(value))
            staged=root/'references'/reference.relative_to(config['golden'])
            staged.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,staged)
            return
        if not value['passed']:raise RuntimeError('Deep identity failed: '+label)
    def native(d,scene,samples,percentage,device,mode='strict',ids=False,deep=True,measure=False,max_events=16):
        d.mkdir(parents=True,exist_ok=bool(previous))
        if previous and (d/'render.json').exists():
            recorded=json.loads((d/'render.json').read_text())
            expected=dict(renderer_sha256=digest,source_sha256=hashlib.sha256(Path(scene).read_bytes()).hexdigest(),
                          samples=samples,percentage=percentage,device=device,deep=deep)
            if deep:expected.update(deep_error=0. if mode=='strict' else float(mode),deep_ids=ids,deep_z_tolerance=0)
            if any(recorded.get(k)!=v for k,v in expected.items()):raise ValueError('Retained render settings differ: '+str(d))
        c=[config['blender'],'--factory-startup','--background','--disable-autoexec','--log','cycles','--log-level','info',
           scene,'--python-exit-code',1,'--python',REPO/'tools/render_blender_deep_scene.py','--','--output',d,
           '--samples',samples,'--percentage',percentage,'--device',device,'--threads',24,'--save-render-passes','--diagnostic-sample-count']
        if deep:
            c+=['--deep','--deep-volume','--deep-error',mode,'--deep-z-tolerance',0,'--deep-memory-mb',8192,'--deep-max-events',max_events]
            if ids:c+=['--deep-ids']
        if measure:c=[sys.executable,REPO/'tools/measure_deep_render.py',d,'--']+c
        run(d.relative_to(root).as_posix().replace('/','-'),c)
        return d
    def verify(d,off=None,**kwargs):
        v=numerical(d.relative_to(root).as_posix().replace('/','-')+'-check',lambda:validate(d,**kwargs))
        if off is not None:
            v['cpu_beauty']=exact_flat(d/'beauty.exr',off/'beauty.exr')
            if (d/'render-passes.exr').exists():v['cpu_raw']=exact_flat(d/'render-passes.exr',off/'render-passes.exr')
        key=d.relative_to(root).as_posix()
        if 'OPTIX' in key.split('/'):
            reference=config['golden']/'optix'/key/'scene.deep.exr'
            if reference.exists():
                identity_result=compare(reference,d/'scene.deep.exr')
                result.setdefault('optix_identity',{})[key]=identity_result
                if not identity_result['passed']:raise ValueError('OptiX SVM identity failed: '+key)
        result['cases'][key]=v;save()
        return v
    def ids_compare(off,on):
        a,b=read(off/'scene.deep.exr'),read(on/'scene.deep.exr')
        if bound(a)!=bound(b):raise ValueError('ID comparison bounds differ')
        if any(name_hash(v)!=int(k,16) for k,v in json.loads(b.header()['cycles:deepIDManifest']).items()):
            raise ValueError('Invalid deep ID name hashes')
        h,w=b.channels()['A'].pixels.shape
        maximum=max(curve_error(pixel(a,x,y),pixel(b,x,y)) for y in range(h) for x in range(w))
        if maximum>bound(b):raise ValueError('Combined ID alpha exceeds header bound')
        return dict(passed=True,max_error=maximum,bound=bound(b))
    def backend_compare(cuda,optix):
        a,b=read(cuda/'scene.deep.exr'),read(optix/'scene.deep.exr')
        h,w=b.channels()['A'].pixels.shape
        maximum=0.;combined=0.;worst=None;totals=[0,0];count_changed=0
        from compare_deep_ids import flattened_transmittance
        for y in range(h):
            for x in range(w):
                left,right=pixel(a,x,y),pixel(b,x,y)
                totals[0]+=len(left);totals[1]+=len(right);count_changed+=len(left)!=len(right)
                error=curve_error(left,right)
                if error>maximum:maximum,worst=error,[x,y]
                combined=max(combined,abs(flattened_transmittance(left)-flattened_transmittance(right)))
        value=dict(passed=cross_comparison_passed(combined),max_curve_error=float(maximum),
                   max_combined_alpha_error=float(combined),flattened_alpha_limit=CROSS_ALPHA_LIMIT,
                   bound=bound(b),worst_pixel=worst,curve_depth_counts_informational=True,
                   samples_before=totals[0],samples_after=totals[1],count_changed_pixels=count_changed)
        (optix/'backend-comparison.json').write_text(json.dumps(value,indent=2))
        if not value['passed']:raise ValueError('OptiX/CUDA flattened alpha exceeds cross-comparison bound: '+str(value))
        return value
    try:
        # Source/build provenance is explicit: an executable cannot infer its Git revision.
        digest=hashlib.sha256(config['blender'].read_bytes()).hexdigest()
        if previous and previous['beauty_sources']['renderer_sha256']!=digest:raise ValueError('Retained audit uses a different executable')
        build=json.loads(config['beauty_builds'].read_text())['builds'][digest]
        source_commit=build['source_commit'];head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
        old_hash=beauty_identity(source_baseline)[0];new_hash=beauty_identity(head)[0]
        if old_hash!=new_hash:raise ValueError('Beauty sources changed')
        changed_sources=subprocess.check_output(['git','diff','--name-only',source_commit,head,'--','src','tools/prepare_blender_deep.py'],text=True).splitlines()
        if any(p.endswith(('.h','.cpp','.cu','CMakeLists.txt','prepare_blender_deep.py')) for p in changed_sources):
            raise ValueError('Renderer source differs from the recorded executable; build and register it first')
        gpu_hash=lambda c:hashlib.sha256(subprocess.check_output(['git','ls-tree','-r',c,'--']+GPU_PATHS)).hexdigest()
        gpu_changed=gpu_hash(source_baseline)!=gpu_hash(head)
        if subprocess.run(['git','diff','--quiet','HEAD','--']+GPU_PATHS).returncode:
            raise ValueError('Commit GPU changes and record the corresponding build before qualification')
        if gpu_changed and not args.cuda_beauty and not args.toolchain_audit:raise ValueError('GPU-side changes require --cuda-beauty')
        result['beauty_sources']=dict(passed=True,before=old_hash,after=new_hash,renderer_sha256=digest,renderer_commit=source_commit,baseline_commit=source_baseline,head=head)
        result['gpu_sources']=dict(changed=gpu_changed,paths=GPU_PATHS)
        run('kernel-resources',[sys.executable,REPO/'src/deep/measure_cuda_resources.py',config['on_cubin'],config['off_cubin'],config['resource_baseline'],root/'resources.json'])
        resources=json.loads((root/'resources.json').read_text());expected=json.loads(config['resource_baseline'].read_text())
        names=set(resources['off'])
        changed=sorted(n for n in names if resources['on'][n]!=expected['on'][n])
        if len(names)!=75 or resources['changed_common_kernels'] or changed:raise ValueError('Common kernel resources changed')
        result['kernel_resources']=dict(passed=True,common_kernel_count=75,changed_common_kernels=[],records=resources['on'])
        result['kernel_resources']['cubin_sha256']={k:hashlib.sha256(config[k].read_bytes()).hexdigest() for k in ('on_cubin','off_cubin')}
        # Run precisely the nine configured CTests, redirecting fixture outputs to D:.
        tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(config['ctest_build']),'-C','Release','--show-only=json-v1']))['tests']
        if len(tests)!=9:raise ValueError('Expected nine CTests')
        ctest=root/'ctest';ctest.mkdir(exist_ok=bool(previous));lines=[]
        for test in tests:
            c=list(test['command'])
            if test['name'] in ('cycles_deep_production','cycles_deep_volume','cycles_deep_density','cycles_deep_exr'):
                c[1]=str(ctest/test['name'])
            quoted=lambda s:'"'+str(s).replace('\\','/').replace('"','\\"')+'"'
            lines.append('add_test('+quoted(test['name'])+' '+' '.join(map(quoted,c))+')')
            timeout=next((p['value'] for p in test['properties'] if p['name']=='TIMEOUT'),150)
            lines.append('set_tests_properties('+quoted(test['name'])+' PROPERTIES TIMEOUT '+str(timeout)+' WORKING_DIRECTORY '+quoted(ctest)+')')
        (ctest/'CTestTestfile.cmake').write_text('\n'.join(lines)+'\n')
        run('ctest',['ctest','--test-dir',ctest,'-C','Release','--output-on-failure'])
        # One beauty-off render per fixture; all CPU modes must match it exactly.
        rejected={'reject_ao_opacity':'ray-traced ambient occlusion cannot drive deep opacity','reject_dof':'static pinhole camera',
                  'reject_motion':'static pinhole camera','reject_orthographic':'requires mono perspective','reject_cubic':'require linear interpolation',
                  'reject_color':'requires scalar extinction','reject_nonlinear':'nonlinear products of density','reject_reflection':'unreflected volumes'}
        for device in (('CPU','CUDA','OPTIX') if args.optix else ('CPU','CUDA')):
            cases=json.loads(config['cpu_cases' if device=='CPU' else 'cuda_cases'].read_text())['cases']
            for name,case in cases.items():
                base=root/'matrix'/device/name
                if name.startswith('reject_'):
                    d=base/'rejection';d.mkdir(parents=True,exist_ok=bool(previous));target=d/'scene.deep.exr';target.write_bytes(b'preserve')
                    cmd=[config['blender'],'--factory-startup','--background','--disable-autoexec',case['scene'],'--python-exit-code',1,
                         '--python',REPO/'tools/render_blender_deep_scene.py','--','--output',d,'--samples',case['samples'],
                         '--percentage',100,'--device',device,'--deep','--deep-volume','--deep-error','strict','--deep-max-events',case.get('deep_max_events',16)]
                    with (d/'rejection.log').open('w') as log:p=subprocess.run(list(map(str,cmd)),env=env,stdout=log,stderr=subprocess.STDOUT)
                    if not p.returncode or target.read_bytes()!=b'preserve' or any('.partial-' in f.name for f in d.iterdir()) or rejected[name] not in (d/'rejection.log').read_text(errors='replace'):
                        raise ValueError('Native atomic rejection failed: '+name)
                    result['cases'][f'matrix/{device}/{name}']=dict(passed=True,rejection=True);continue
                off=native(base/'beauty',case['scene'],case['samples'],100,device,deep=False) if device=='CPU' else None
                for mode,ids in [('strict',False),('1e-4',False),('1e-3',False),('1e-4',True),('1e-3',True)]:
                    d=native(base/(mode+('-ids' if ids else '')),case['scene'],case['samples'],100,device,mode,ids,max_events=case.get('deep_max_events',16))
                    v=verify(d,off)
                    if name=='adaptive_volume' and not min(v['oracle']['accepted_populations'])<case['samples']:
                        raise ValueError('Adaptive case did not converge early')
                    if name=='zero_extinction' and v['total_deep_samples']!=0:raise ValueError('Zero extinction was not empty')
                    if ids:v['combined_alpha']=numerical(f'ids-{device}-{name}-{mode}',lambda:ids_compare(base/mode,d))
                    elif device!='OPTIX':identity(f'matrix-{device}-{mode}/{name}',config['golden']/f'matrix-{device}-{mode}'/name/'deep/scene.deep.exr',d/'scene.deep.exr',mode)
                    if device=='OPTIX':v['cuda_alpha']=numerical(f'optix-alpha-{name}-{mode}-{ids}',lambda:backend_compare(root/'matrix/CUDA'/name/(mode+('-ids' if ids else '')),d))
        # Strict boundary cases retain their original XML, flags and analytic gates.
        for device in (('CPU','CUDA','OPTIX') if args.optix else ('CPU','CUDA')):
            for case in json.loads((REPO/'tools/deep_boundary_cases.json').read_text())['cases']:
                d=root/'boundary'/device/case['name'];d.mkdir(parents=True,exist_ok=bool(previous));source=d/'scene.xml';source.write_text(case['xml'])
                base=[config['cycles'],'--background','--quiet','--device',device,'--shadingsys','svm','--threads',8,
                      '--samples',case['samples'],'--width',case['width'],'--height',case['height']]
                c=base+['--output',d/'beauty.exr','--deep-volume','--deep-error','strict','--deep-output',d/'scene.deep.exr',
                        '--deep-records',d/'scene.csv','--deep-memory-mb',64,'--deep-max-events',16]+case['extra']+[source]
                label='boundary-'+device+'-'+case['name']
                if case['rejection']:
                    (d/'scene.deep.exr').write_bytes(b'preserve')
                    with (d/'rejection.log').open('w') as log:p=subprocess.run(list(map(str,c)),env=env,stdout=log,stderr=subprocess.STDOUT)
                    if not p.returncode or (d/'scene.deep.exr').read_bytes()!=b'preserve' or any('.partial-' in f.name for f in d.iterdir()):raise ValueError('Boundary atomic rejection failed')
                    result['cases'][label]=dict(passed=True,rejection=True);continue
                run(label,c)
                if device=='CPU':run(label+'-off',base+['--output',d/'off.beauty.exr',source])
                v=verify(d,samples=case['samples'],adaptive=False,ledger=d/'scene.csv')
                v['max_analytic_error']=boundary_analytic(d,case)
                if device=='CPU':v['cpu_beauty']=exact_flat(d/'beauty.exr',d/'off.beauty.exr')
                if device!='OPTIX':identity(label,config['golden']/('boundary-'+device)/(case['name']+'.deep.exr'),d/'scene.deep.exr','strict')
                else:v['cuda_alpha']=numerical(label+'-cuda-alpha',lambda:backend_compare(root/'boundary/CUDA'/case['name'],d))
        for name,samples,percentage in [('small',16,2),('performance',4,25)]:
            for mode in ('strict','1e-4','1e-3'):
                d=native(root/'landscape'/name/mode,config['landscape'],samples,percentage,'CUDA',mode,measure=True,max_events=8192)
                verify(d)
                identity(name+'-'+mode,config['golden']/name/('noids-'+mode+'-cap0')/'scene.deep.exr',d/'scene.deep.exr',mode)
                if args.optix:
                    o=native(root/'landscape/OPTIX'/name/mode,config['landscape'],samples,percentage,'OPTIX',mode,measure=True,max_events=8192)
                    verify(o)['cuda_alpha']=numerical('optix-landscape-'+name+'-'+mode,lambda:backend_compare(d,o))
        counts={m:len(v) for m,v in result['identity'].items()}
        if counts!={'strict':81,'1e-4':15,'1e-3':15}:raise ValueError('Identity coverage changed: '+str(counts))
        result['identity_counts']=counts
        if args.toolchain_audit:
            result.update(passed=True,toolchain_audit=True,gpu_qualification=False,
                          staged_references=str(root/'references'),total_seconds=time.monotonic()-started)
            save();print('Compiler audit PASS; staged references '+str(root/'references'))
            return
        # Additional surface/adaptive/lens/motion and exact-ID fixtures are shared below.
        from run_deep_smokes import run_smokes
        run_smokes(root,config,env,run,verify,native,numerical,ids_compare,backend_compare,result,optix=args.optix)
        if args.optix:
            result['optix_identity_count']=len(result.get('optix_identity',{}))
            if result['optix_identity_count']!=65:raise ValueError('Expected OptiX SVM identity 65/65')
        if args.cuda_beauty:
            from cuda_beauty_gate import validate_cuda_beauty
            targets=json.loads(args.cuda_beauty.read_text())['targets'];result['cuda_beauty']={}
            if not targets:raise ValueError('CUDA beauty stage needs at least one target')
            for entry in targets:
                d=root/entry['case']
                if not d.resolve().is_relative_to(root.resolve()) or json.loads((d/'render.json').read_text())['renderer_sha256']!=digest:
                    raise ValueError('CUDA beauty target is not a render from the qualified executable in this run')
                v=numerical('cuda-beauty-'+entry['case'].replace('/','-'),lambda:validate_cuda_beauty(
                    d,entry['references'],pool=entry.get('pool',[]),seed_references=entry.get('seed_references',[]),
                    builds=config['beauty_builds'],snapshot_off=entry.get('snapshot_off',[]),snapshot_on=entry.get('snapshot_on',[]),
                    snapshot_build=entry.get('snapshot_build'),reader_backend='openexr'))
                if not v['passed']:raise ValueError('CUDA beauty gate failed')
                result['cuda_beauty'][entry['case']]=v
        result['passed']=True;result['checks_seconds']=time.monotonic()-started
        # Preserve only small text/JSON evidence. No CSV trace, EXR or spill is copied.
        for p in root.rglob('*'):
            if p.is_file() and p.suffix in ('.json','.log','.txt') and p.stat().st_size<=1024*1024 and 'stored_diagnostic' not in p.name:
                target=report_dir/'details'/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        result['large_bytes_before_cleanup']=sum(p.stat().st_size for p in root.rglob('*') if p.is_file())
        if not args.keep:cleanup(root)
        result['cleaned']=not root.exists();result['total_seconds']=time.monotonic()-started;save()
        print('Check                          Result')
        for label in ('CPU beauty exact','Beauty source + 75 resources','Matrices / boundaries / oracle','Strict identity 81/81','Numeric identity 30/30'):
            print(f'{label:30s} PASS')
        print(f"Total {result['total_seconds']:.2f} s; evidence {report_dir}; cleanup {result['cleaned']}")
    except Exception as error:
        result['passed']=False;result['error']=str(error);result['total_seconds']=time.monotonic()-started;save()
        raise


if __name__=='__main__':main()
