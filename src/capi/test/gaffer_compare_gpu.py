"""Adapt unchanged Gaffer FLOAT passes to the existing calibrated GPU policy.

The native Sample Count display pass is count / requested_samples (Cycles
film/read.h, PassAccessor scale). The existing policy restores integer counts;
the adapter preserves the normalized pass unchanged.
Normal and depth are measured separately, not mislabeled as denoiser inputs.
"""
import hashlib,json,os,sys
sys.dont_write_bytecode=True
from workspace_guard import output,environment,REPO
root=output(sys.argv[1]);os.environ.update(environment(root/'compare-temp'))
from gaffer_passes import read,compare
import numpy as np
import OpenEXR
sys.path.insert(0,str(REPO/'src/deep'))
from cuda_beauty_gate import validate_cuda_beauty
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
native=digest(output('D:/temp/cycles_c_api/phase9-gaffer-build/build/lib/GafferCycles.dll'))
api=digest(output('D:/temp/cycles_c_api/phase9-matched-gpu-core/install/cycles_c_backend_private.dll'))
source=digest(REPO/'src/capi/test/gaffer_scene.py')
policy=output(root/'policy');policy.mkdir(exist_ok=True)
# Both routes link the identical native static libraries staged in native-config.
config=output('D:/temp/cycles_c_api/phase9-gaffer-build/native-config.json')
registry=output(policy/'builds.json')
registry.write_text(json.dumps({'builds':{i:{'beauty_source_sha256':digest(config)} for i in (native,api)}}))
def prepare(fixture,label):
    saved=output(root/(fixture+'-'+label)); manifest=json.loads((saved/'fixture.json').read_text())
    channels=read(saved); target=output(policy/(fixture+'-'+label));target.mkdir(exist_ok=True)
    raw={'ViewLayer.Combined.'+c:channels['rgba.'+c] for c in 'RGBA'}
    counts=channels['sample_count.X']*np.float32(16)
    assert np.equal(counts,np.rint(counts)).all() and (counts==16).all()
    raw['ViewLayer.Debug Sample Count.X']=channels['sample_count.X']
    OpenEXR.File({},dict(raw)).write(str(output(target/'render-passes.exr')))
    OpenEXR.File({},{c:channels['rgba.'+c] for c in 'RGBA'}).write(str(output(target/'beauty.exr')))
    check=OpenEXR.File(str(target/'render-passes.exr'),separate_channels=True).channels()
    assert all(np.array_equal(v.view(np.uint32),check[k].pixels.view(np.uint32)) for k,v in raw.items())
    settings=dict(source_sha256=source,samples=16,resolution=[64,48],percentage=100,
        device=manifest['device'],frame=0,adaptive=False,seed=manifest['seed'],use_animated_seed=False,
        denoising=False,denoiser=None,denoising_use_gpu=False,pixel_filter='native Gaffer defaults',threads=4,
        save_render_passes=True,diagnostic_sample_count_pass=True,deep=label=='api',actual_deep=False,
        metadata_kind='legacy_policy_adapter_not_renderer_manifest',renderer_sha256=api if label=='api' else native)
    output(target/'render.json').write_text(json.dumps(settings,indent=2))
    return target
results={}
for fixture in ('emission','meshes','point-light'):
    controls=[prepare(fixture,'native-'+str(i)) for i in range(1,6)]
    seeds=[prepare(fixture,'seed-'+str(i)) for i in range(1,5)]
    subject=prepare(fixture,'api')
    report=validate_cuda_beauty(subject,controls,pool=controls,builds=registry,
        seed_references=seeds,output=output(policy/(fixture+'.json')),reader_backend='openexr')
    results[fixture]=dict(passed=report['passed'],raw_passed=report['raw_passed'],
        count_passed=report['count_mismatch']['passed'],bias_passed=report.get('bias_passed'),
        all_nine_channels=compare(read(root/(fixture+'-native-1')),read(root/(fixture+'-api'))))
    print(fixture,results[fixture],flush=True)
summary=dict(passed=all(v['passed'] for v in results.values()),fixtures=results,
             policy_sha256=digest(REPO/'src/deep/cuda_beauty_gate.py'),controls=5,seeds=4)
output(root/'gpu-parity.json').write_text(json.dumps(summary,indent=2))
assert summary['passed'], 'Existing GPU policy did not pass'
