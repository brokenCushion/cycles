"""Record the preserved diagnosis and final guarded Phase 8 qualification."""
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO
from diagnose_deep import diagnose

root = output(sys.argv[1])
evidence = output(REPO / 'builds/validation/c-api/phase8.json', committed=True)
result = json.loads(evidence.read_text())
before = diagnose(root / 'standalone-comparison/opaque-native.exr',
                  root / 'preserved-original-api/opaque-api.exr')
after = diagnose(root / 'standalone-matched-static/opaque-native.exr',
                 root / 'matched-static/outputs/opaque-api.exr')
assert not before['passed'] and after['passed']
for label, report in [('before', before), ('after', after)]:
    output(REPO / f'builds/validation/c-api/phase8-opaque-{label}.json', committed=True).write_text(
        json.dumps(report, indent=2) + '\n')
result['original_parity_failure'] = result.get('standalone_parity', result.get('parity'))
result['diagnosis'] = dict(before=before['summary'], after=after['summary'],
    differing_headers=before['header_differences'],
    cause='Native XML forces static BVH; API test used default dynamic BVH. Set the fixture bvh_static=1 to compare the same settings. No wrapper/native/comparator changes.',
    full_before='phase8-opaque-before.json', full_after='phase8-opaque-after.json')
result['standalone_parity'] = json.loads((root / 'standalone-matched-static/deep-parity.json').read_text())
assert all(r['byte_identical'] for r in result['standalone_parity'])
log = (root / 'tests-corrected-environment.log').read_text()
assert '100% tests passed, 0 tests failed out of 22' in log
result['builds']['deep']['test_log'] = log
result['builds']['deep']['exports'] = json.loads((root / 'loader-exports.json').read_text())
result['builds']['deep']['sha256'] = {
    f: hashlib.sha256((root/'install'/f).read_bytes()).hexdigest()
    for f in ('cycles_c.dll', 'cycles_c_backend_private.dll')}
result.update(passed=True, status='phase8-passed',
    correction='Test fixture scene settings aligned with native XML; literal byte criterion unchanged. Added direct native Session byte assertion for all four cases.')
evidence.write_text(json.dumps(result, indent=2) + '\n')
print('PASS Phase 8; before/after per-pixel, headers and chunk reports recorded')
