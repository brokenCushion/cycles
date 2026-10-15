"""Record only small, auditable Phase 9 evidence in the repository."""
import hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
from workspace_guard import output,environment,REPO,disk,self_test
self_test();disk()
base=output('D:/temp/cycles_c_api');os.environ.update(environment(base/'phase9-record-temp'))
from gaffer_passes import read,compare
from check_exports import exported,check
from check_loader import imports,os_only
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text())
source=output(base/'phase9-20261013/gaffer')
patch=subprocess.check_output(['git','-C',str(source),'diff','--','SConstruct','src/GafferCycles'],text=True)
patchfile=output(REPO/'src/capi/gaffer/native-cycles53.patch',committed=True)
patchfile.write_text(patch,encoding='utf-8')
inventory={};current=None
for line in patch.splitlines():
    if line.startswith('+++ b/'):
        current=line[6:];inventory[current]=[]
    elif line.startswith('@@ '):inventory[current].append(line.split('@@')[1].strip())
cpu=load(base/'phase9-final-cpu/cpu-parity.json');assert cpu['passed']
gpu={d:load(base/('phase9-final-cuda' if d=='CUDA' else 'phase9-final-optix-2')/'gpu-parity.json') for d in ('CUDA','OPTIX')}
assert all(v['passed'] for v in gpu.values())
swap=load(base/'phase9-host-swap-final/swap.json');assert swap['passed']
final_cpu={f:compare(read(base/'phase9-final-cpu'/(f+'-native')),read(base/'phase9-host-swap-final'/('core-'+f)))
           for f in ('emission','meshes','point-light')}
assert all(c['bit_identical'] for f in final_cpu.values() for c in f.values())
interactive=compare(read(base/'phase9-scenes/native-interactive-7'),read(base/'phase9-host-swap-final/core-interactive'))
assert all(c['bit_identical'] for c in interactive.values())
# The saved RGBA initial and edited comparisons independently check the edit.
import OpenEXR
def rgba(p):
    with OpenEXR.File(str(p),separate_channels=True) as f:return {c:v.pixels for c,v in f.channels().items()}
edited=compare(rgba(base/'phase9-scenes/native-interactive-7/edited.exr'),rgba(base/'phase9-host-swap-final/core-interactive/edited.exr'))
assert all(c['bit_identical'] for c in edited.values())
tool='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe'
builds={}
for kind,total in (('core',14),('deep',23)):
    root=output(base/('phase9-matched-gpu-'+kind));install=root/'install'
    def dump(flag,name):return subprocess.check_output([tool,flag,str(install/name)],text=True)
    public=exported(dump('/exports','cycles_c.dll'),'win32');check(public)
    private=exported(dump('/exports','cycles_c_backend_private.dll'),'win32')
    assert private==['cycles_c_private_get_table']
    dependencies=imports(dump('/dependents','cycles_c.dll'));os_only(dependencies)
    test=(root/'tests.log').read_text();assert f'100% tests passed, 0 tests failed out of {total}' in test
    cache=(root/'build/CMakeCache.txt').read_text()
    builds[kind]=dict(ctests=total,public_exports=len(public),private_exports=private,front_imports=sorted(dependencies),
        hashes={n:digest(install/n) for n in ('cycles_c.dll','cycles_c_backend_private.dll')},
        deep_enabled='WITH_CYCLES_DEEP_OPAQUE:BOOL=ON' in cache,tests_sha256=digest(root/'tests.log'))
assert builds['deep']['deep_enabled'] and not builds['core']['deep_enabled']
native=load(base/'phase9-gaffer-build/native-config.json')
for path,expected in native['static_hashes'].items():assert digest(output(path))==expected
runtime=load(base/'phase9-gaffer-build/runtime-deployment.json')
package=base/'phase9-package/extracted/gafferDependencies-11.0.0a8-windows-platform25'
for item in runtime:
    target=output(item['destination']);assert digest(target)==item['sha256']
    original=package/'bin'/target.name
    item['original_package_sha256']=digest(original) if original.exists() else None
record=dict(phase=9,passed=True,status='qualified-local-proof-of-concept',
    gaffer_commit=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip(),
    scope='Pre-release Gaffer 1.8 main; approved dependencies 11.0.0a8 platform25; Windows only',
    builds=builds,native_baseline=native,patch=dict(path=str(patchfile.relative_to(REPO)),sha256=digest(patchfile),hunks=inventory),
    cpu=final_cpu,gpu=gpu,interactive=dict(initial_and_auxiliary=interactive,edited_rgba=edited),swap=swap,
    runtime_deployment=runtime,dependency_check_inside_gaffer='ccl_init succeeded for both builds in every swap fixture',
    native_cycles_changes=[],published=False,free_bytes=disk(),timings='Provisional; concurrent work may affect timings. No timing-based acceptance in Phase 9.',
    repairs=['Adapter transferred vertex normals and smoothing instead of flat-shading the lit mesh.',
      'Adapter explicitly resumes after reset preserves interactive pause state.',
      'Harness uses native ieDisplay for interactive capture, removes/re-adds outputs, avoids stale display buffers and Python message-handler lock contention.',
      'Harness recognizes native OptiX device ID suffix, unwraps Python command scalars, preserves FLOAT channels and normalized sample counts.',
      'Build harness confines C++20 to native Cycles adapter, stages atomic headers/buildExtensions, and releases loaded test DLL before relinking.'],
    limitations=['Subset: static triangle meshes, unindexed vertex normals, basic SVM shaders, constant point lights, one camera, final and interactive transform/shader edits.',
      'No claim of full GafferCycles replacement, OSL/volume/subdivision/motion qualification inside Gaffer, or arbitrary future major-version compatibility.',
      'No performance acceptance claim; prior Phase 7 forwarding benchmark remains the performance evidence.'])
dest=output(REPO/'builds/validation/c-api/phase9-final.json',committed=True)
dest.write_text(json.dumps(record,indent=2)+'\n')
print('Phase 9 evidence verified:',dest)
