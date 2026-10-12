"""Compare every record dumped by the dynamically loaded C API and native C++ reflection."""
import json
import pathlib
import subprocess
import sys


def records(text):
    return [line for line in text.splitlines() if line.startswith(('NODE ', 'SOCKET ', 'DEFAULT ', 'ENUM '))]


def compare(a, b):
    assert a and a == b, next(((i, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y),
                              ('record count', len(a), len(b)))


if __name__ == '__main__':
    # Must reject changed defaults, even if type and socket inventories agree.
    try:
        compare(['DEFAULT 1'], ['DEFAULT 2'])
    except AssertionError:
        pass
    else:
        raise AssertionError('Comparison failed to reject altered default')
    host, native, library, resource, user, output = sys.argv[1:]
    a = subprocess.check_output([host, library, resource, user], text=True)
    b = subprocess.check_output([native], text=True)
    path = pathlib.Path(output)
    path.with_suffix('.c-dump.txt').write_text(a)
    path.with_suffix('.native-dump.txt').write_text(b)
    a, b = records(a), records(b)
    compare(a, b)
    counts = {kind: sum(x.startswith(kind+' ') for x in a) for kind in ['NODE', 'SOCKET', 'DEFAULT', 'ENUM']}
    path.write_text(json.dumps(dict(passed=True, records=counts), indent=2)+'\n')
    print('PASS complete C DLL/native dump parity:', counts)
