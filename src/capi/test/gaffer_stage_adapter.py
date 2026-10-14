"""Stage the C-only host prototype in the guarded local Gaffer checkout."""
import shutil
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, REPO, disk, self_test
self_test()
disk()
source = output('D:/temp/cycles_c_api/phase9-20261013/gaffer')
directory = output(source/'src/GafferCyclesC')
directory.mkdir(exist_ok=True)
for original in (REPO/'src/capi/gaffer/Renderer.cpp', REPO/'src/capi/cycles_c.h'):
    shutil.copy2(original, output(directory/original.name))
script = output(source/'SConstruct')
text = script.read_text()
marker = '# C-only dynamically loaded Cycles proof-of-concept.'
if marker not in text:
    anchor = 'for libraryName, libraryDef in libraries.items() :'
    text = text.replace(anchor, marker + '''
libraries["GafferCyclesC"] = {
    "envAppends": {
        "LIBS": ["Gaffer", "GafferScene", "IECoreScene", "OpenImageIO", "OpenImageIO_Util"],
    },
}
''' + anchor)
    script.write_text(text)
print('Staged public-C-only adapter; no Cycles static libraries linked')
