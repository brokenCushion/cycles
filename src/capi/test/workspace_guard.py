"""Local qualification write boundary; inputs/toolchains may be outside it."""
import os
from pathlib import Path
import shutil

ROOT = Path('D:/temp/cycles_c_api').resolve()
REPO = Path(__file__).resolve().parents[3]


def output(value, *, committed=False):
    path = Path(value).resolve()
    allowed = REPO if committed else ROOT
    if not path.is_relative_to(allowed):
        raise ValueError(f'Output outside {allowed}: {path}')
    return path


def environment(temp):
    temp = output(temp)
    temp.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(TEMP=str(temp), TMP=str(temp), TMPDIR=str(temp),
               PYTHONDONTWRITEBYTECODE='1', CUDA_CACHE_PATH=str(output(temp / 'cuda-cache')))
    for name in ('TEMP', 'TMP', 'TMPDIR', 'CUDA_CACHE_PATH'):
        output(env[name])
    return env


def cmake_paths(args):
    """Resolve actual configure destinations, not a script's intended root."""
    build = None
    prefix = None
    for i, arg in enumerate(args):
        if arg == '-B':
            build = output(args[i + 1])
        elif arg.startswith('-DCMAKE_INSTALL_PREFIX='):
            prefix = output(arg.split('=', 1)[1])
        elif arg.startswith('-D') and '=' in arg:
            key, value = arg[2:].split('=', 1)
            if any(s in key for s in ('OUTPUT_DIRECTORY', 'BINARY_DIR', 'INSTALL_PREFIX')):
                output(value)
    if build is None or prefix is None:
        raise ValueError('Explicit guarded -B and CMAKE_INSTALL_PREFIX required')
    return build, prefix


def disk():
    free = shutil.disk_usage(ROOT.anchor).free
    if free < 200_000_000_000:
        raise RuntimeError(f'D: free space below 200 GB: {free}')
    return free


def self_test():
    for bad in ('D:/CyclesDeepScratch/c-api/phase8-20261013',
                'D:/temp/cycles_osl_aov', str(ROOT / '..' / 'escape')):
        try:
            output(bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Guard allowed {bad}')
    good = ROOT / 'guard-test'
    assert output(good) == good
    try:
        cmake_paths(['-B', str(good), '-DCMAKE_INSTALL_PREFIX=D:/CyclesDeepScratch/install'])
    except ValueError:
        pass
    else:
        raise AssertionError('Guard allowed old install prefix')
    print('PASS path guard: scratch, other worktree temp, traversal and old prefix rejected')


if __name__ == '__main__':
    self_test()
