"""Guarded compile of the version-matched Phase 9 configuration."""
import json
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk
from install_guard import check

root = output(sys.argv[1])
build, install = output(root/'build'), output(root/'install')
prefix = next(s.split('=',1)[1] for s in (build/'CMakeCache.txt').read_text().splitlines()
              if s.startswith('CMAKE_INSTALL_PREFIX:'))
assert output(prefix) == install
check(build,install)
disk()
env = environment(root/'temp')
dep = output('D:/temp/cycles_c_api/phase9-package/extracted/gafferDependencies-11.0.0a8-windows-platform25')
env['PATH'] = str(dep/'bin')+';'+str(dep/'lib')+';'+env['PATH']
args = [str(root/'toolchain.cmd'),'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/cmake.exe',
        '--build',str(build),'--parallel','8']
# TBB's MSVC headers request tbb12.lib via auto-link; keep that search local.
wrapper = output(root/'toolchain.cmd')
text = wrapper.read_text()
line = 'set "LIB='+str(dep/'lib')+';%LIB%"\n'
if line not in text:
    wrapper.write_text(text.replace('%*\n',line+'%*\n'))
output(root/'build-command.json').write_text(json.dumps(args,indent=2))
with output(root/'build.log').open('w') as log:
    result = subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
print('build exit:',result.returncode)
sys.exit(result.returncode)
