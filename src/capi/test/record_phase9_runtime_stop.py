"""Save small Phase 9 build/initialization evidence at the policy stop."""
import hashlib
import json
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk
root = output('D:/temp/cycles_c_api/phase9-matched-cycles-7')
package = output('D:/temp/cycles_c_api/phase9-package')
log = (root/'tests.log').read_text()
assert '6 tests failed out of 13' in log
assert 'missing symbols: none' in log
result = dict(phase=9, passed=False, status='stopped-msvc-runtime-version-policy',
    package=json.loads((package/'verified.json').read_text()),
    build='Compiled and installed front, backend, standalone and tests against the approved host dependency package; no native source changes.',
    root=str(root), sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (root/'install').glob('cycles_c*.dll')},
    package_versions=dict(OIIO='3.0.6.1', OSL='1.14.8', OpenVDB='12.1.1',
                          Embree='4.4.0', TBB='2021.13.0', USD='0.26.5'),
    usd_note='USD is present in the host package; this core API build does not link USD.',
    ctests=dict(passed=7,total=13,log=log),
    blocker=dict(modules=['MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll'],
                 package_version='14.40.33816.0', loaded_version='14.51.36247.0',
                 missing_symbols=[],
                 explanation='The build harness records all bundled DLL versions, including Microsoft runtimes. Existing exact-version policy rejects the newer process runtime at ccl_init.'),
    harness_repairs=['OIIO library-only CMake imports avoid missing optional command-line tools.',
                     'OCIO imports relocated to actual package DLL/import-library paths.',
                     'Pugixml uses the package OIIO header-only implementation.',
                     'Explicit bundled runtime DLL list supplies build version resources.',
                     'Long DLL list passed through guarded CMake cache file.',
                     'TBB auto-link search uses package lib directory.'],
    proposed_decision='Keep exact version checks for renderer dependencies; permit MSVC v14 runtime updates only under Microsoft compatibility rules, meeting the build-tool minimum and all imported symbols. Not implemented.',
    source='https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170',
    pending=['Gaffer local build and proof-of-concept backend', 'Phase 7 initialization inside Gaffer',
             'Old/new Gaffer scene parity and two-build swap', 'Phase 10 local proposal'],
    no_native_or_criteria_changes=True, published=False, free_bytes=disk())
output(REPO/'builds/validation/c-api/phase9.json',committed=True).write_text(json.dumps(result,indent=2)+'\n')
print('Recorded Phase 9 runtime-policy stop')
