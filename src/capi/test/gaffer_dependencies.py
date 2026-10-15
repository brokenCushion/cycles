"""Download the explicitly approved Phase 9 archive; verify before extraction."""
import hashlib
import json
import os
import shutil
import stat
import urllib.request
import zipfile
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, self_test

root = output('D:/temp/cycles_c_api/phase9-package')
root.mkdir(exist_ok=True)
os.environ.update(environment(root / 'temp'))
self_test()
disk()
name = 'gafferDependencies-11.0.0a8-windows-platform25.zip'
url = 'https://github.com/GafferHQ/dependencies/releases/download/11.0.0a8/' + name
archive = output(root / name)
expected = '9dc1c6eea1eb6ffd762dbe73af0913e824193dde61ca51433d1559cba74b5712'
if not archive.exists():
    with urllib.request.urlopen(url) as source, archive.open('xb') as target:
        shutil.copyfileobj(source, target)
with archive.open('rb') as source:
    digest = hashlib.file_digest(source, 'sha256').hexdigest()
assert archive.stat().st_size == 415817133 and digest == expected, 'Archive size/digest mismatch'
print('PASS published archive size and SHA-256', flush=True)
dest = output(root / 'extracted')
assert not dest.exists(), 'Refuse to overwrite an extraction'
with zipfile.ZipFile(archive) as z:
    for entry in z.infolist():
        target = output(dest / entry.filename)
        assert target.is_relative_to(dest) and not stat.S_ISLNK(entry.external_attr >> 16)
    dest.mkdir()
    z.extractall(dest)
output(root / 'verified.json').write_text(json.dumps(dict(url=url, bytes=archive.stat().st_size,
    sha256=digest, verified_before_extraction=True,
    scope='Pre-release dependency set matching Gaffer 1.8 development (main), not a released Gaffer.'), indent=2))
print('PASS extraction within guarded root', flush=True)
