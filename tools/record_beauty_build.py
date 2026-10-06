"""Record executable/source identity for accumulated CUDA deep-off controls.

Run: python tools/record_beauty_build.py REGISTRY EXE SOURCE_COMMIT
The executable must have been built from that commit using the pinned Blender
overlay. This records provenance; it does not infer a build from an executable.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEEP_HOST = {'src/app/deep_output.cpp', 'src/app/deep_output.h',
             'src/integrator/path_trace_deep_tile.h', 'src/session/output_driver.h'}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def beauty_identity(commit):
    files = {}
    for line in git('ls-tree', '-r', commit, '--', 'src').decode().splitlines():
        metadata, path = line.split('\t')
        if path.startswith(('src/deep/', 'src/kernel/deep/')) or path in DEEP_HOST:
            continue
        files[path] = metadata.split()[2]
    adapter = git('show', commit + ':tools/prepare_blender_deep.py')
    # Only the deep output callback is excluded; all beauty compatibility edits
    # and the pinned Blender revision remain in the source identity.
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
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')
    print(executable_hash, digest)
