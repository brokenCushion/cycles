"""Expose the already-qualified Cycles 5.3 build to the native Gaffer backend."""
import hashlib
import json
import re
import shutil
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk, self_test
self_test()
disk()
root = output('D:/temp/cycles_c_api/phase9-gaffer-build')
build = output(sys.argv[1] if len(sys.argv)>1 else 'D:/temp/cycles_c_api/phase9-matched-gpu-core/build')
source = output('D:/temp/cycles_c_api/phase9-20261013/gaffer')
prefix = output(root/'cycles53')
prefix.mkdir(exist_ok=True)
for p in (REPO/'src').rglob('*'):
    if p.suffix not in ('.h', '.hpp'): continue
    dest = output(prefix/'include'/p.relative_to(REPO/'src'))
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(p,dest)
for p in (REPO/'third_party/atomic').rglob('*.h'):
    dest = output(prefix/'include'/p.relative_to(REPO/'third_party/atomic'))
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(p, dest)
commands = json.loads((build/'compile_commands.json').read_text())
command, = [c['command'] for c in commands if c['file'].endswith('native_dump.cpp')]
defines = []
for name, value in re.findall(r'-D(\w+)(?:=("[^"]*"|\S+))?',command):
    defines.append([name,value.strip('"')] if value else name)
ninja = (build/'build.ninja').read_text()
section = ninja[ninja.index('build bin\\cycles_c_native_dump.exe:'):]
line = re.search(r'^  LINK_LIBRARIES = (.+)$',section,re.M)[1]
libs = []
for token in line.split():
    if token.startswith('lib\\'): token = str(output(build/token))
    if token not in libs: libs.append(token)
record = dict(defines=defines, libs=libs, prefix=str(prefix),
              static_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in (build/'lib').glob('cycles_*.lib')
                             if not p.name.startswith('cycles_c')})
output(root/'native-config.json').write_text(json.dumps(record,indent=2))
script = output(source/'SConstruct')
text = script.read_text()
marker = '# C API proof-of-concept: native baseline uses identical build inputs.'
if marker not in text:
    start = text.index('cyclesDefines = [')
    end = text.index('\n]',start)+2
    text = text[:end]+'''\n
# C API proof-of-concept: native baseline uses identical build inputs.
import json
with open(os.environ["CYCLES_MATCHED_CONFIG"]) as f:
    matchedCycles = json.load(f)
cyclesDefines = [tuple(d) if isinstance(d, list) else d for d in matchedCycles["defines"]]
'''+text[end:]
    anchor = 'for libraryName, libraryDef in libraries.items() :'
    text = text.replace(anchor,'''# Keep host libraries; derive native libraries from the qualified CMake link.
libraries["GafferCycles"]["envAppends"]["LIBS"] = [
    "IECoreScene", "IECoreImage", "IECoreVDB", "Gaffer", "GafferScene", "GafferDispatch", "GafferOSL"
] + matchedCycles["libs"]
libraries["GafferCycles"]["envAppends"].setdefault("LINKFLAGS", []).extend([
    "/WHOLEARCHIVE:" + p for p in matchedCycles["libs"]
    if p.endswith(("cycles_scene.lib", "cycles_session.lib"))
])
'''+anchor)
standard_marker = '# Cycles 5.3 headers require C++20 only in the native adapter.'
if standard_marker not in text:
    anchor = 'for libraryName, libraryDef in libraries.items() :'
    text = text.replace(anchor, standard_marker + '''
for section in ("envAppends", "pythonEnvAppends"):
    libraries["GafferCycles"][section].setdefault("CXXFLAGS", []).append(
        "/std:c++20" if sys.platform == "win32" else "-std=c++20")
''' + anchor)
script.write_text(text)
print('Prepared matched native baseline without changing Cycles')
