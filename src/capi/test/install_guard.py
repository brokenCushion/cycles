"""List generated CMake install destinations; reject writes outside this fresh install/build."""
import json, re, sys
from pathlib import Path

def check(build, prefix):
    build, prefix = Path(build).resolve(), Path(prefix).resolve()
    records = []
    for script in sorted(set(build.rglob('cmake_install.cmake')) | set(build.rglob('install-cxx-module-bmi-*.cmake'))):
        text = script.read_text(encoding='utf-8')
        if re.search(r'\bexecute_process\s*\(', text):
            raise ValueError('Unreviewed executable install action: '+str(script))
        for match in re.finditer(r'file\(\s*(INSTALL\s+DESTINATION|WRITE|APPEND|MAKE_DIRECTORY)\s+"([^"]+)"', text, re.I):
            action, raw = match.groups()
            target = raw.replace('${CMAKE_INSTALL_PREFIX}', prefix.as_posix()).replace('${CMAKE_INSTALL_MANIFEST}', 'install_manifest.txt')
            if '$' in target: raise ValueError('Unresolved install destination: '+target)
            path = Path(target).resolve()
            allowed = prefix if action.upper().startswith('INSTALL') else build
            if not path.is_relative_to(allowed):
                raise ValueError('Unsafe install destination: '+str(path)+' in '+str(script))
            records.append(dict(script=str(script), line=text[:match.start()].count('\n')+1,
                                action=action, destination=str(path)))
        # Generated includes must also stay in this build (optional absent BMI scripts are fine).
        for raw in re.findall(r'include\(\s*"([^"]+)"', text):
            if '$' in raw or not Path(raw).resolve().is_relative_to(build):
                raise ValueError('Foreign install script: '+raw)
    if not records or not any(r['action'].upper().startswith('INSTALL') for r in records):
        raise ValueError('No install destinations found')
    return dict(passed=True, build=str(build), prefix=str(prefix), destinations=records,
                rule='All file(INSTALL) destinations under fresh prefix; bookkeeping writes/includes under fresh build; no unresolved variables or execute_process')
