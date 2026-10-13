"""Create the user-approved local Phase 9 source checkout, without publishing."""
import json
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk

root = output(sys.argv[1])
root.mkdir(exist_ok=False)
env = environment(root / 'temp')
disk()
project = sys.argv[2] if len(sys.argv) > 2 else 'gaffer'
repositories = {'gaffer': 'GafferHQ/gaffer', 'dependencies': 'GafferHQ/dependencies',
                'cortex': 'ImageEngine/cortex'}
checkout = output(root / project)
args = ['git', 'clone', '--depth', '1', 'https://github.com/' + repositories[project] + '.git', str(checkout)]
output(root / 'clone-command.json').write_text(json.dumps(args, indent=2))
with output(root / 'clone.log').open('w') as log:
    subprocess.run(args, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=checkout, env=env, text=True).strip()
output(root / 'checkout.json').write_text(json.dumps(dict(repository=args[-2], revision=revision), indent=2))
print(revision)
