"""Record the guarded Phase 9 runtime-policy qualification, without rendering."""
import hashlib
import json
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk, self_test

self_test()
root = output('D:/temp/cycles_c_api/phase9-matched-cycles-7')
log = (root/'tests.log').read_text()
assert '100% tests passed, 0 tests failed out of 14' in log
manifest = json.loads((root/'build/src/capi/dispatch/dependency_versions.json').read_text())
exports = json.loads((root/'loader-exports.json').read_text())
assert exports['passed'] and exports['public_exports'] == 98
result = dict(phase=9, status='runtime-policy-passed-gaffer-build-tool-download-pending',
    phase_complete=False, runtime_policy_passed=True,
    versions=manifest, ctests=dict(passed=14, total=14, log=log), exports=exports,
    sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (root/'install').glob('cycles_c*.dll')},
    policy_tests=['newer 14 passes', 'equal minimum passes', 'older fails',
                  'non-14 fails', 'missing symbol fails with newer runtime',
                  'unavailable version fails', 'renderer exact version unchanged',
                  'UCRT missing symbol fails'],
    package_note='Pre-release dependencies 11.0.0a8, matching Gaffer 1.8 development (main), not a released Gaffer.',
    runtime_minimum=manifest['microsoft_runtime_minimum'],
    pending=['Approval to obtain missing SCons build tool into guarded temp root',
             'Gaffer build and C API backend', 'Dependency check inside Gaffer',
             'Gaffer rendering parity and two-library swap', 'Phase 10 local proposal'],
    benchmark_note='CTest benchmark is functional only; no timing acceptance claim.',
    native_changes=False, published=False, free_bytes=disk())
output(REPO/'builds/validation/c-api/phase9-runtime-policy.json', committed=True).write_text(
    json.dumps(result, indent=2)+'\n')
print('Recorded runtime policy and build-tool stop')
