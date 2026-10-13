"""Guarded local CPU qualification. Never resumes or writes an old build."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from workspace_guard import output, environment, cmake_paths, disk, self_test, REPO
from install_guard import check as install_check

HERE = Path(__file__).resolve().parent
CMAKE = 'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/cmake.exe'
CTEST = str(Path(CMAKE).with_name('ctest.exe'))


def main():
    self_test()
    root = output(sys.argv[1])
    mode = sys.argv[2]
    if mode not in ('core', 'deep'):
        raise ValueError(mode)
    root.mkdir(parents=True, exist_ok=False)
    env = environment(root / 'temp')
    env['PATH'] = str(output(root / 'install')) + ';' + env['PATH']
    os.environ.update(env)
    disk()
    wrapper = output(root / 'toolchain.cmd')
    # Set TEMP before VsDevCmd as well as before compiler execution.
    wrapper.write_text('@echo off\nset "TEMP=' + env['TEMP'] + '"\nset "TMP=' + env['TMP'] +
        '"\ncall "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul\n'
        'set "TEMP=' + env['TEMP'] + '"\nset "TMP=' + env['TMP'] + '"\n'
        'set "PATH=' + str(output(root / 'install')) + ';%PATH%"\n%*\n')

    def save(name, value):
        output(root / name).write_text(json.dumps(value, indent=2) + '\n')

    def run(name, args):
        args = list(map(str, args))
        disk()
        for key in ('TEMP', 'TMP', 'TMPDIR', 'CUDA_CACHE_PATH'):
            output(env[key])
        save(name + '-command.json', args)
        with output(root / (name + '.log')).open('w') as f:
            command = args if name == 'tests' else [str(wrapper), *args]
            subprocess.run(command, cwd=output(root), env=env,
                           stdout=f, stderr=subprocess.STDOUT, check=True)
        print('PASS', name, flush=True)

    args = json.loads((HERE / 'windows_cpu_config.json').read_text())
    args[args.index('-S') + 1] = str(REPO)
    args = [a.replace('@BUILD@', str(output(root / 'build')))
             .replace('@INSTALL@', str(output(root / 'install'))) for a in args]
    if mode == 'core':
        args = [a.replace('WITH_CYCLES_DEEP_OPAQUE=ON', 'WITH_CYCLES_DEEP_OPAQUE=OFF') for a in args]
    build, prefix = cmake_paths(args)
    save('paths.json', dict(build=str(build), install=str(prefix), temp=env['TEMP'],
                            free_bytes=disk(), timing='provisional; concurrent workload not excluded'))
    run('configure', [CMAKE, *args])
    off = list(args)
    off[off.index('-B') + 1] = str(output(root / 'build-off'))
    off = [a.replace('WITH_CYCLES_C_API=ON', 'WITH_CYCLES_C_API=OFF') for a in off]
    cmake_paths(off)
    run('configure-off', [CMAKE, *off])
    def commands(folder):
        return {x['file']: x['command'].replace(str(root / folder), 'BUILD')
                .replace((root / folder).as_posix(), 'BUILD')
                for x in json.loads((root / folder / 'compile_commands.json').read_text())
                if '/capi/' not in x['file'].replace('\\', '/')}
    a, b = commands('build'), commands('build-off')
    assert a == b, [x for x in a if a.get(x) != b.get(x)]
    save('native-commands.json', dict(passed=True, count=len(a)))
    save('install-destinations.json', install_check(build, prefix))
    run('build', [CMAKE, '--build', output(build), '--parallel', '8'])
    save('install-destinations.json', install_check(build, prefix))
    run('install', [CMAKE, '--install', output(build)])
    for line in (build / 'install_manifest.txt').read_text().splitlines():
        assert output(line).is_relative_to(prefix) and Path(line).is_file(), line
    run('tests', [CTEST, '--test-dir', output(build), '--output-on-failure'])
    save('complete.json', dict(mode=mode, passed=True, free_bytes=disk()))


if __name__ == '__main__':
    main()
