"""Guarded driver for one local Gaffer qualification fixture."""
import json
import os
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO, self_test
self_test()
disk()
root = output(sys.argv[1])
root.mkdir(parents=True, exist_ok=False)
backend, fixture, device = sys.argv[2:5]
api = output(sys.argv[5] if len(sys.argv)>5 else 'D:/temp/cycles_c_api/phase9-matched-gpu-core/install')
host = output('D:/temp/cycles_c_api/phase9-gaffer-build/build')
env = environment(root/'temp')
env.update(PYTHONNOUSERSITE='1', PYTHONUTF8='1', GAFFER_ROOT=str(host),
           GAFFER_STARTUP_PATHS=str(host/'startup'), CYCLES_ROOT=str(api),
           CYCLES_C_RESOURCES=str(api), CYCLES_C_LIBRARY=str(api/'cycles_c.dll'),
           CYCLES_C_GAFFER_PLUGIN=str(host/'lib/GafferCyclesC.dll'))
env['PATH'] = str(host/'bin')+';'+str(host/'lib')+';'+env['PATH']
env['PYTHONPATH'] = str(host/'python')+';'+str(REPO/'src/capi/test')
args = [str(host/'bin/python.exe'),'-B',str(REPO/'src/capi/test/gaffer_scene.py'),
        str(root),backend,fixture,device]
output(root/'command.json').write_text(json.dumps(args,indent=2))
with output(root/'process.log').open('w') as log:
    result = subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
print('Gaffer fixture exit:',result.returncode)
sys.exit(result.returncode)
