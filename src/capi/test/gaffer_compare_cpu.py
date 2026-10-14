"""Phase 9 exact CPU comparison, all nine captured channels."""
import json, os, sys
sys.dont_write_bytecode=True
from workspace_guard import output, environment
root=output(sys.argv[1]);os.environ.update(environment(root/'compare-temp'))
from gaffer_passes import read,compare
results={f:compare(read(root/(f+'-native')),read(root/(f+'-api')))
         for f in ('emission','meshes','point-light')}
passed=all(c['bit_identical'] for f in results.values() for c in f.values())
output(root/'cpu-parity.json').write_text(json.dumps(dict(passed=passed,fixtures=results),indent=2))
print('CPU exact nine-channel parity:',passed)
assert passed, results
