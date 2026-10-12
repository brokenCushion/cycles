"""Check the *actual* front imports and private backend exports on Windows."""
import json
import re
import subprocess
import sys
from pathlib import Path
from check_exports import check, exported


def imports(text):
    names = set(re.findall(r'^\s+([\w.-]+\.dll)\s*$', text, re.M | re.I))
    assert names, 'No imports parsed'
    return {n.lower() for n in names}


def os_only(names):
    allowed = {'kernel32.dll', 'version.dll', 'advapi32.dll', 'user32.dll',
               'shell32.dll', 'ole32.dll', 'ntdll.dll', 'bcrypt.dll', 'ucrtbase.dll'}
    assert names <= allowed, ('Non-OS front imports', sorted(names - allowed))


def main():
    tool, install, output = sys.argv[1:]
    install = Path(install)
    def dump(option, dll):
        return subprocess.check_output([tool, option, str(install / dll)], text=True)
    front = exported(dump('/exports', 'cycles_c.dll'), 'win32')
    check(front)
    private = exported(dump('/exports', 'cycles_c_backend_private.dll'), 'win32')
    assert private == ['cycles_c_private_get_table'], private
    dependencies = imports(dump('/dependents', 'cycles_c.dll'))
    os_only(dependencies)
    try:
        os_only(dependencies | {'OpenImageIO.dll'})
    except AssertionError:
        pass
    else:
        raise AssertionError('Import checker accepted a renderer dependency')
    Path(output).write_text(json.dumps(dict(public_exports=len(front), private_exports=private,
                                          front_imports=sorted(dependencies), passed=True), indent=2)+'\n')
    print('PASS front OS-only imports and 98 exports; backend one private export')


if __name__ == '__main__':
    main()
