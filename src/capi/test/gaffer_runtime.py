"""Deploy the already-installed supported MSVC runtime to the disposable host."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, disk, self_test
self_test()
disk()
source = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT')
destination = output('D:/temp/cycles_c_api/phase9-gaffer-build/build/bin')
records = []
for path in sorted(source.glob('*.dll')):
    if not path.name.lower().startswith(('msvcp140','vcruntime140','concrt140')):
        continue
    target = output(destination/path.name)
    records.append(dict(source=str(path), destination=str(target),
        prior_sha256=hashlib.sha256(target.read_bytes()).hexdigest() if target.exists() else None,
        sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    shutil.copy2(path,target)
assert records
output(destination.parent.parent/'runtime-deployment.json').write_text(json.dumps(records,indent=2))
print('Deployed installed MSVC 14.44 runtime locally:',len(records),'files')
