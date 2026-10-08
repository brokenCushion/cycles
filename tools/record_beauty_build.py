"""Record executable/source identity for accumulated CUDA deep-off controls.

Run: python tools/record_beauty_build.py REGISTRY EXE SOURCE_COMMIT
The executable must have been built from that commit using the pinned Blender
overlay. This records provenance; it does not infer a build from an executable.
"""
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEEP_HOST = {'src/app/deep_output.cpp', 'src/app/deep_output.h',
             'src/integrator/path_trace_deep_tile.h', 'src/session/output_driver.h',
             'src/session/deep.h', 'src/session/deep.cpp', 'src/app/deep_output_driver_test.cpp'}
DEEP_DEVICE = {'src/kernel/device/optix/kernel_deep.cu',
               'src/kernel/device/optix/kernel_deep_osl.cu'}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def without_deep_blocks(source):
    """Exclude only explicitly guarded deep host/scheduling code, keep all else."""
    output, depth = [], 0
    for line in source.splitlines(keepends=True):
        directive = re.match(rb'\s*#\s*(ifdef|ifndef|if|endif)\b(.*)', line)
        if depth:
            if directive:
                depth += -1 if directive[1] == b'endif' else 1
            continue
        if directive and directive[1] == b'ifdef' and directive[2].strip() == b'WITH_CYCLES_DEEP_OPAQUE':
            depth = 1
        else:
            output.append(line)
    if depth:
        raise ValueError('Unclosed deep conditional')
    return b''.join(output)


def beauty_identity(commit):
    files = {}
    for line in git('ls-tree', '-r', commit, '--', 'src').decode().splitlines():
        metadata, path = line.split('\t')
        if path.startswith(('src/deep/', 'src/kernel/deep/')) or path in DEEP_HOST | DEEP_DEVICE:
            continue
        if path == 'src/kernel/CMakeLists.txt':
            source = re.sub(rb'(?ms)^set\(SRC_KERNEL_DEEP_HEADERS\n.*?^\)\n', b'',
                            git('show', commit + ':' + path))
            files[path] = hashlib.sha256(source).hexdigest()
        elif path in ('src/integrator/path_trace.cpp', 'src/app/cycles_standalone.cpp',
                    'src/integrator/path_trace_work_cpu.cpp', 'src/integrator/path_trace_work_gpu.cpp',
                    'src/integrator/path_trace_work_gpu.h',
                    'src/device/queue.h', 'src/device/queue.cpp',
                    'src/device/cuda/queue.h', 'src/device/cuda/queue.cpp',
                    'src/device/cpu/kernel.h', 'src/kernel/device/cpu/kernel_arch.h',
                    'src/kernel/device/cpu/kernel_arch_impl.h'):
            files[path] = hashlib.sha256(without_deep_blocks(git('show', commit + ':' + path))).hexdigest()
        elif path == 'src/session/session.cpp':
            source = git('show', commit + ':' + path)
            # Only this exact host validation hook is excluded. Existing Session
            # code and any change to its beauty/sampling body retain their hash.
            source = source.replace(b"#ifdef WITH_CYCLES_DEEP_OPAQUE\n  if (params.deep.enabled && scene->params.shadingsystem == SHADINGSYSTEM_OSL) {\n    validate_deep_osl(scene.get());\n  }\n#endif\n", b'')
            files[path] = hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        elif path in ('src/scene/osl.cpp', 'src/scene/osl.h', 'src/scene/shader.h'):
            source = without_deep_blocks(git('show', commit + ':' + path))
            files[path] = hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        elif path.startswith('src/device/optix/'):
            source = without_deep_blocks(git('show', commit + ':' + path))
            # Preserve the original Git blob identity when a deep-only block is added.
            files[path] = hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        elif path == 'src/kernel/device/optix/CMakeLists.txt':
            source = re.sub(rb'(?ms)^([ ]*)if\(WITH_CYCLES_DEEP_OPAQUE\)\n.*?^\1endif\(\)\n',
                            b'', git('show', commit + ':' + path))
            files[path] = hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        else:
            files[path] = metadata.split()[2]
    adapter = git('show', commit + ':tools/prepare_blender_deep.py')
    # Deep properties/sync and output callback are excluded; all beauty compatibility edits
    # and the pinned Blender revision remain in the source identity.
    properties = adapter.index(b"edit('blender/addon/properties.py'")
    driver = adapter.index(b"edit('blender/output_driver.h'", properties)
    adapter = adapter[:properties] + adapter[driver:]
    begin = adapter.index(b"edit('blender/output_driver.cpp', 'CCL_NAMESPACE_END'")
    end = adapter.index(b'# Only the offline render driver', begin)
    files['tools/prepare_blender_deep.py:outside-deep-output'] = hashlib.sha256(
        adapter[:begin] + adapter[end:]).hexdigest()
    digest = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return digest, files


if __name__ == '__main__':
    if sys.argv[1] == '--compare-sources':
        before, after = (beauty_identity(v)[0] for v in sys.argv[2:4])
        print(json.dumps(dict(passed=before == after, before=before, after=after)))
        raise SystemExit(0 if before == after else 1)
    registry, executable, commit = sys.argv[1:]
    commit = git('rev-parse', commit).decode().strip()
    digest, files = beauty_identity(commit)
    path = Path(registry)
    data = json.loads(path.read_text()) if path.exists() else {'builds': {}}
    executable_hash = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
    data['builds'][executable_hash] = dict(source_commit=commit,
        beauty_source_sha256=digest, source_files=files,
        deep_host_exclusions=sorted(DEEP_HOST), executable=str(Path(executable).resolve()))
    data['builds'][executable_hash]['deep_device_exclusions'] = sorted(DEEP_DEVICE)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')
    print(executable_hash, digest)
