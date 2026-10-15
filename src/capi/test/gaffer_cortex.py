"""Build the Cortex revision selected by the local Gaffer workflow."""
import json
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, self_test
self_test()
disk()
root = output('D:/temp/cycles_c_api/phase9-gaffer-build')
source = output('D:/temp/cycles_c_api/phase9-cortex-20261013/cortex')
prefix = output(root/'build')
env = environment(root/'temp')
env.update(PYTHONUTF8='1', CORTEX_BUILD_NAME=str(prefix))
env['PATH'] = str(prefix/'bin')+';'+str(prefix/'lib')+';'+env['PATH']
if '--checkout' in sys.argv:
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=source,env=env,text=True).strip()
    subprocess.run(['git','fetch','--depth','1','origin','refs/tags/10.7.3.1'],cwd=source,env=env,check=True)
    subprocess.run(['git','checkout','--detach','FETCH_HEAD'],cwd=source,env=env,check=True)
    revision = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,env=env,text=True).strip()
    output(root/'cortex-revision.json').write_text(json.dumps(dict(tag='10.7.3.1',revision=revision)))
    sys.exit(0)
options = output(root/'cortex-options.py')
base = source/'.github/workflows/main/options.windows'
options.write_text('exec(compile(open('+repr(str(base))+').read(), '+repr(str(base))+', "exec"))\n'
                   'ENV_VARS_TO_IMPORT += " TEMP TMP TMPDIR PYTHONUTF8 PYTHONDONTWRITEBYTECODE"\n')
args = [str(output(root/'toolchain.cmd')),
        str(output('D:/temp/cycles_c_api/build-tools/venv/Scripts/python.exe')),
        '-B','-m','SCons','-j','8','install','OPTIONS='+str(options)]
attempt = len(list(root.glob('cortex-build-*.log'))) + 1
output(root/f'cortex-command-{attempt}.json').write_text(json.dumps(args,indent=2))
with output(root/f'cortex-build-{attempt}.log').open('w') as log:
    result = subprocess.run(args,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT)
print('Cortex build exit:',result.returncode,flush=True)
sys.exit(result.returncode)
