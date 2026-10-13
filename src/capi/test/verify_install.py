"""Guarded tests/export verification of an already built local installation."""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO
from install_guard import check

root = output(sys.argv[1])
build, install = output(root / 'build'), output(root / 'install')
env = environment(root / 'temp')
env['PATH'] = str(install) + ';' + env['PATH']
check(build, install)
cache = (build / 'CMakeCache.txt').read_text()
prefix = next(x.split('=', 1)[1] for x in cache.splitlines() if x.startswith('CMAKE_INSTALL_PREFIX:'))
assert output(prefix) == install
for path in (build / 'install_manifest.txt').read_text().splitlines():
    assert output(path).is_relative_to(install)
commands = []
if '--tests' in sys.argv:
    commands.append(('tests-corrected-environment', [
        'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/ctest.exe',
        '--test-dir', str(build), '--output-on-failure']))
commands.append(('loader-exports', [sys.executable, '-B',
    str(REPO / 'src/capi/test/check_loader.py'),
    'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe',
    str(install), str(output(root / 'loader-exports.json'))]))
for label, args in commands:
    disk()
    with output(root / (label + '.log')).open('w') as log:
        subprocess.run(args, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('PASS', label, flush=True)
