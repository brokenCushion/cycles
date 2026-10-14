"""Install the approved build tool in a guarded, isolated environment."""
import hashlib
import json
import subprocess
import sys
import urllib.request
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO, self_test

self_test()
disk()
root = output('D:/temp/cycles_c_api/build-tools')
root.mkdir(exist_ok=True)
env = environment(root/'temp')
env['PIP_CACHE_DIR'] = str(output(root/'pip-cache'))
env['PYTHONNOUSERSITE'] = '1'
metadata_url = 'https://pypi.org/pypi/SCons/4.10.1/json'
metadata = json.load(urllib.request.urlopen(metadata_url))
artifact, = [a for a in metadata['urls'] if a['filename'].endswith('py3-none-any.whl')]
assert artifact['url'].startswith('https://files.pythonhosted.org/')
wheel = output(root/artifact['filename'])
if not wheel.exists():
    with urllib.request.urlopen(artifact['url']) as response, wheel.open('wb') as target:
        import shutil
        shutil.copyfileobj(response, target)
digest = hashlib.sha256(wheel.read_bytes()).hexdigest()
assert digest == artifact['digests']['sha256'] and wheel.stat().st_size == artifact['size']
venv = output(root/'venv')
python = output(venv/'Scripts/python.exe')
if not python.exists():
    subprocess.run([sys.executable, '-B', '-m', 'venv', str(venv)], env=env, check=True)
subprocess.run([str(python), '-B', '-m', 'pip', '--isolated', '--cache-dir',
                str(output(root/'pip-cache')), 'install', '--no-index',
                '--no-deps', str(wheel)], env=env, check=True)
version = subprocess.check_output([str(python), '-B', '-m', 'SCons', '--version'],
                                  env=env, text=True)
assert 'v4.10.1' in version
result = dict(name='SCons', version='4.10.1', source=artifact['url'],
              metadata_source=metadata_url, wheel=wheel.name, sha256=digest,
              bytes=wheel.stat().st_size, environment=str(venv), version_output=version)
output(REPO/'builds/validation/c-api/phase9-build-tools.json', committed=True).write_text(
    json.dumps(result, indent=2)+'\n')
print(version)
