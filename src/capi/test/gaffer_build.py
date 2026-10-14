"""Guarded local Gaffer build using the approved dependency package."""
import json
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, self_test

self_test()
disk()
root = output('D:/temp/cycles_c_api/phase9-gaffer-build')
root.mkdir(exist_ok=True)
source = output('D:/temp/cycles_c_api/phase9-20261013/gaffer')
package = output('D:/temp/cycles_c_api/phase9-package/extracted/gafferDependencies-11.0.0a8-windows-platform25')
build, install = output(root/'build'), output(root/'install')
env = environment(root/'temp')
env.update(PYTHONUTF8='1', PYTHONNOUSERSITE='1')
env['CYCLES_MATCHED_CONFIG'] = str(output(root/'native-config.json'))
if not build.exists():
    shutil.copytree(package, build)
env['PATH'] = str(build/'bin')+';'+str(build/'lib')+';'+env['PATH']
options = dict(BUILD_DIR=str(build), INSTALL_DIR=str(install),
               CYCLES_ROOT=str(output(root/'cycles53')), CXXSTD='c++17',
               BUILD_CACHEDIR=str(output(root/'cache')), BUILD_TYPE='RELEASE',
               INKSCAPE='disableGraphics', SPHINX='', GLEW_LIB_SUFFIX='32',
               LOCATE_DEPENDENCY_LIBPATH=str(build/'lib'),
               LOCATE_DEPENDENCY_PYTHONPATH=str(build/'python'),
               ENV_VARS_TO_IMPORT='PATH TEMP TMP TMPDIR PYTHONUTF8 PYTHONDONTWRITEBYTECODE')
options_path = output(root/'options.py')
options_path.write_text('\n'.join(k+' = '+repr(v) for k,v in options.items())+'\n')
wrapper = output(root/'toolchain.cmd')
wrapper.write_text('@echo off\ncall "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul\nif errorlevel 1 exit /b %errorlevel%\n%*\n')
python = output('D:/temp/cycles_c_api/build-tools/venv/Scripts/python.exe')
args = [str(wrapper),str(python),'-B','-m','SCons','-j','8','buildCore','buildExtensions',
        'OPTIONS='+str(options_path)]
if '--dry-run' in sys.argv: args.append('-n')
attempt = len(list(root.glob('build-*.log'))) + 1
output(root/f'command-{attempt}.json').write_text(json.dumps(args,indent=2))
with output(root/f'build-{attempt}.log').open('w') as log:
    result = subprocess.run(args,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT)
print('Gaffer build exit:',result.returncode,flush=True)
sys.exit(result.returncode)
