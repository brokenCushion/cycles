"""Record the Phase 9 native-version baseline decision with local evidence."""
import hashlib
import json
import re
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk
root = output('D:/temp/cycles_c_api/phase9-gaffer-build')
api_root = output('D:/temp/cycles_c_api/phase9-matched-cycles-7')
package = output('D:/temp/cycles_c_api/phase9-package/extracted/gafferDependencies-11.0.0a8-windows-platform25')

def version(path):
    text = path.read_text()
    parts = [re.search(r'#define CYCLES_VERSION_'+part+r'\s+(\d+)',text)[1]
             for part in ('MAJOR','MINOR','PATCH')]
    return dict(version='.'.join(parts),path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest())

tests = (api_root/'tests-corrected-environment.log').read_text()
assert '100% tests passed, 0 tests failed out of 14' in tests
result = dict(phase=9, passed=False, status='stopped-native-renderer-baseline-decision',
    build_tool=json.loads((REPO/'builds/validation/c-api/phase9-build-tools.json').read_text()),
    baseline=version(package/'cycles/include/util/version.h'),
    api=version(REPO/'src/util/version.h'),
    cortex=json.loads((root/'cortex-revision.json').read_text()),
    cortex_status='Build explicitly cancelled at baseline design stop; partial objects preserved, not qualified.',
    gaffer_status='Dependency preparation only; initial SCons dry-run cannot create configure cache. No Gaffer binary or alternative backend yet.',
    ctests=dict(passed=14,total=14,log=tests),
    loader=json.loads((api_root/'loader-exports.json').read_text()),
    proposed_decision='Build the existing Gaffer C++ backend and C API backend against the same Cycles 5.3 source/configuration and host dependencies; keep cross-version 5.1 comparison separate.',
    rationale='An exact old/new-backend comparison using bundled Cycles 5.1 versus API Cycles 5.3 would confound native-version and wrapper differences. No render difference is claimed: comparison has not run.',
    pending=['User decision on native renderer baseline', 'Complete Cortex/Gaffer builds',
             'C API backend and in-host dependency test', 'Render parity and library swap',
             'Phase 10 local proposal'],
    native_changes=False, published=False, free_bytes=disk())
output(REPO/'builds/validation/c-api/phase9-baseline-decision.json',committed=True).write_text(
    json.dumps(result,indent=2)+'\n')
print('Recorded baseline decision and retained qualification')
