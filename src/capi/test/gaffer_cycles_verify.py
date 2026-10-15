"""Install/test the host-matched build after validating every install path."""
import json
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO
from install_guard import check
root = output(sys.argv[1])
build, install = output(root/'build'), output(root/'install')
cache = (build/'CMakeCache.txt').read_text()
assert output(next(s.split('=',1)[1] for s in cache.splitlines() if s.startswith('CMAKE_INSTALL_PREFIX:'))) == install
check(build,install)
disk()
env = environment(root/'temp')
env['PATH'] = str(install)+';'+env['PATH']
cmake = 'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/'
commands = [('install',[cmake+'cmake.exe','--install',str(build)]),
            ('tests',[cmake+'ctest.exe','--test-dir',str(build),'--output-on-failure'])]
for label,args in commands:
    with output(root/(label+'.log')).open('w') as log:
        result = subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
    print(label,result.returncode,flush=True)
    if result.returncode: sys.exit(result.returncode)
    if label == 'install':
        for p in (build/'install_manifest.txt').read_text().splitlines():
            assert output(p).is_relative_to(install)
