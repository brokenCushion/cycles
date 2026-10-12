"""Apply the existing beauty policy to preserved native-vs-C-API fixture outputs.

These renders disable denoising. Noisy Combined and accepted counts are supplied
unchanged; the other captured passes remain in the raw artifact for inspection.
The legacy validator uses a `deep` boolean to select subject versus control.
In these adapter-only JSON files that flag means API versus native, NOT a deep
render setting; actual_deep is false for every render. The core build has no deep.
No scene, renderer, acceptance threshold or golden output is modified here.
"""
import argparse, hashlib, json, sys
from pathlib import Path
import OpenEXR
import numpy as np
REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO/'src/deep'))
from cuda_beauty_gate import validate_cuda_beauty, float32_ulp

FIXTURES = {
    'scene_cube_surface': 'examples/scene_cube_surface.xml',
    'deep_output_driver_transparent_test': 'src/app/deep_output_driver_transparent_test.xml',
    'scene_cube_volume': 'examples/scene_cube_volume.xml',
    'scene_osl_stripes': 'examples/scene_osl_stripes.xml',
}

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def prepare(root, run, fixture, route, identity, seed=None):
    source = root/'renders'/run/(fixture+'-'+route+'.exr')
    output = root/'policy'/fixture/(run+'-'+route)
    output.mkdir(parents=True, exist_ok=True)
    channels = {k: v.pixels for k,v in OpenEXR.File(str(source), separate_channels=True).channels().items()}
    assert all(v.shape == (20,47) and v.dtype == np.float32 and np.isfinite(v).all() for v in channels.values())
    raw = {('ViewLayer.Debug Sample Count.X' if k=='Sample Count.X' else 'ViewLayer.'+k):v for k,v in channels.items()}
    beauty = {k:channels['Combined.'+k] for k in 'RGBA'}
    OpenEXR.File({}, dict(raw)).write(str(output/'render-passes.exr'))
    OpenEXR.File({}, dict(beauty)).write(str(output/'beauty.exr'))
    # Confirm the adapter changes names/containers only, never FLOAT values.
    reread=OpenEXR.File(str(output/'render-passes.exr'),separate_channels=True).channels()
    assert all(np.array_equal(v,reread[k].pixels) for k,v in raw.items())
    settings=dict(source_sha256=digest(REPO/FIXTURES[fixture]), samples=16,
        resolution=[47,20], percentage=100, device='OPTIX', frame=0,
        adaptive=False, seed=(123 if 'transparent' in fixture else 0) if seed is None else seed,
        use_animated_seed=False, denoising=False, denoiser=None, denoising_use_gpu=False,
        pixel_filter='unchanged XML/native defaults', threads=4, save_render_passes=True,
        diagnostic_sample_count_pass=True, deep=(route=="api"), actual_deep=False,
        metadata_kind="legacy_policy_adapter_not_renderer_manifest", renderer_sha256=identity,
        route=route, source_exr=str(source), source_exr_sha256=digest(source))
    (output/'render.json').write_text(json.dumps(settings,indent=2)+'\n')
    return output

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('root',type=Path)
    parser.add_argument('--controls',type=int,default=5)
    parser.add_argument('--seeds',action='store_true')
    args=parser.parse_args();root=args.root
    identities=json.loads((root/'render-identities.json').read_text())
    native=identities['native_executable_sha256'];api=identities['dll_sha256'];current=identities['current_host_sha256']
    registry={'builds':{k:{'beauty_source_sha256':identities['native_source_sha256']} for k in (native,current,api)}}
    (root/'policy').mkdir(exist_ok=True)
    builds=root/'policy/builds.json';builds.write_text(json.dumps(registry,indent=2)+'\n')
    results={}
    for fixture in FIXTURES:
        controls=[prepare(root,'OPTIX-'+str(i),fixture,'native',native if i==1 else current) for i in range(1,args.controls+1)]
        target=prepare(root,'OPTIX-1',fixture,'api',api)
        seeds=[prepare(root,'OPTIX-seed-'+str(i),fixture,'native',current,100+i) for i in range(1,5)] if args.seeds else []
        report=validate_cuda_beauty(target,controls[:5],pool=controls,builds=builds,
            seed_references=seeds,output=root/'policy'/(fixture+'.json'),reader_backend='openexr')
        a=OpenEXR.File(str(root/'renders/OPTIX-1'/(fixture+'-native.exr')),separate_channels=True).channels()
        b=OpenEXR.File(str(root/'renders/OPTIX-1'/(fixture+'-api.exr')),separate_channels=True).channels()
        raw={}
        for c in a:
            x=a[c].pixels.astype(np.float64);y=b[c].pixels.astype(np.float64);delta=np.abs(x-y)
            ulp=np.array([float32_ulp(float(v)) for v in x.flat]).reshape(x.shape)
            raw[c]=dict(different_pixels=int(np.count_nonzero(delta)),max_absolute=float(delta.max()),max_reference_ulp=float((delta/ulp).max()))
        results[fixture]=dict(passed=report['passed'],raw_passed=report['raw_passed'],
            bias_passed=report.get('bias_passed'),count_passed=report['count_mismatch']['passed'],
            statistical_pixels=len(report['statistical_pixels']),calibration=report.get('calibration'),
            report=str(root/'policy'/(fixture+'.json')),raw_comparison=raw)
        print(fixture, results[fixture]['passed'], 'fallback',len(report['statistical_pixels']),flush=True)
    summary=dict(passed=all(x['passed'] for x in results.values()),device='OPTIX',
        policy_sha256=digest(REPO/'src/deep/cuda_beauty_gate.py'),denoising=False,
        ordinary_controls=args.controls,seed_controls=4 if args.seeds else 0,fixtures=results)
    (root/'gpu-parity.json').write_text(json.dumps(summary,indent=2)+'\n')
    return 0 if summary['passed'] else 1
if __name__=='__main__':sys.exit(main())
