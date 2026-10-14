"""Run one unchanged host/plugin against core-only and deep-enabled libraries."""
import hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from workspace_guard import output,environment,REPO,disk,self_test
self_test();disk()
root=output(sys.argv[1]);root.mkdir(exist_ok=False)
os.environ.update(environment(root/'temp'))
from gaffer_passes import read,compare
import OpenEXR,numpy as np
host=output('D:/temp/cycles_c_api/phase9-gaffer-build/build')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
files=[host/'lib'/n for n in ('Gaffer.dll','GafferScene.dll','GafferCycles.dll','GafferCyclesC.dll')]
before={str(p):digest(p) for p in files}
libraries={}
for build in ('core','deep'):
    install=output('D:/temp/cycles_c_api/phase9-matched-gpu-'+build+'/install')
    libraries[build]={n:digest(install/n) for n in ('cycles_c.dll','cycles_c_backend_private.dll')}
    for fixture in ('emission','meshes','point-light','interactive'):
        args=[sys.executable,'-B',str(REPO/'src/capi/test/gaffer_run_scene.py'),
              str(output(root/(build+'-'+fixture))),'CyclesC',fixture,'CPU',str(install)]
        subprocess.run(args,check=True,timeout=300)
        manifest=json.loads((root/(build+'-'+fixture)/'fixture.json').read_text())
        assert manifest['features']['deep']==(build=='deep')
after={str(p):digest(p) for p in files}
assert before==after
comparisons={f:compare(read(root/('core-'+f)),read(root/('deep-'+f)))
             for f in ('emission','meshes','point-light','interactive')}
for f in comparisons.values(): assert all(v['bit_identical'] for v in f.values())
def edited(build):
    with OpenEXR.File(str(root/(build+'-interactive')/'edited.exr'),separate_channels=True) as f:
        return {c:v.pixels for c,v in f.channels().items()}
edits=compare(edited('core'),edited('deep'));assert all(v['bit_identical'] for v in edits.values())
assert libraries['core']['cycles_c_backend_private.dll']!=libraries['deep']['cycles_c_backend_private.dll']
output(root/'swap.json').write_text(json.dumps(dict(passed=True,host_before=before,host_after=after,
    libraries=libraries,comparisons=comparisons,edited=edits,host_rebuilds=0),indent=2))
print('Unchanged Gaffer host swap: PASS')
