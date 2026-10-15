"""Persist the local Phase 9 dependency-build decision, without starting builds."""
import ast
import json
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk

root = output('D:/temp/cycles_c_api')
deps = root / 'phase9-dependencies-20261013/dependencies'
projects = ('Python', 'OpenImageIO', 'OpenShadingLanguage', 'OpenVDB', 'Embree')
recipes = {}
for name in projects:
    config = ast.literal_eval((deps / name / 'config.py').read_text())
    recipes[name] = dict(downloads=config['downloads'],
        windows_override='platform:windows' in config,
        commands=config.get('commands', []))
result = dict(phase=9, passed=False, status='stopped-dependency-build-scope-decision',
    checkouts={name: json.loads((root / directory / 'checkout.json').read_text())
               for name, directory in [('gaffer', 'phase9-20261013'),
                                       ('dependencies', 'phase9-dependencies-20261013'),
                                       ('cortex', 'phase9-cortex-20261013')]},
    recipes=recipes, free_bytes=disk(),
    evidence=[
        'Gaffer .github/workflows/main/installDependencies.py defaults to prebuilt Cortex 10.7.3.1 platform25 archive.',
        'Cortex .github/workflows/main.yml uses prebuilt gafferDependencies-11.0.0a8-windows-platform25.zip.',
        'Dependencies .github/workflows/main.yml Windows jobs build only GafferResources, publish=false.',
        'Python recipe invokes ./configure, make, make install without a Windows override; other core recipes likewise lack Windows overrides.'
    ],
    decision='Local source-only Windows dependency build requires extending upstream build recipes. Request direction before expanding scope or replacing it with prebuilt dependencies.',
    recommendation='Allow official prebuilt Windows dependencies, then build Cortex/Gaffer and two matching cycles_c variants locally; keep compatibility checks and parity criteria unchanged.',
    alternative='Keep every dependency source-built and authorize the Windows dependency-build port as additional work.',
    limitations=['No Gaffer build or alternative backend implemented yet.',
                 'No Phase 9 render parity or library-swap acceptance achieved.',
                 'Phase 10 not started at this stop condition.',
                 'No publication, push, issue or PR created.'])
output(REPO / 'builds/validation/c-api/phase9-preflight.json', committed=True).write_text(
    json.dumps(result, indent=2) + '\n')
print('Recorded Phase 9 stop; D: free bytes:', result['free_bytes'])
