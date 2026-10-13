"""Guarded incremental wrapper/test rebuild and new deep diagnostic outputs."""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO
from install_guard import check

root = output(sys.argv[1])
dest = output(root / sys.argv[2])
dest.mkdir(exist_ok=False)
build, install = output(root / 'build'), output(root / 'install')
env = environment(root / 'temp')
env['PATH'] = str(install) + ';' + env['PATH']
cache = (build / 'CMakeCache.txt').read_text()
prefix = next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_INSTALL_PREFIX:'))
assert output(prefix) == install
check(build, install)
cmake = 'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/cmake.exe'
commands = [
    ('build', [str(root/'toolchain.cmd'), cmake, '--build', str(build), '--parallel', '8']),
    ('install', [cmake, '--install', str(build)]),
    ('deep', [str(build/'bin/cycles_c_deep.exe'), str(install), str(REPO), str(dest/'outputs')])]
for label, args in commands:
    disk()
    check(build, install)
    output(dest / (label+'-command.json')).write_text(json.dumps(args,indent=2))
    with output(dest / (label+'.log')).open('w') as log:
        subprocess.run(args,cwd=dest,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('PASS',label,flush=True)
