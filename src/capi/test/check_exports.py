"""Check the actual dynamic symbol table, including accidental C++/data exports."""
import re
import subprocess
import sys

EXPECTED = {
    'ccl_api_version', 'ccl_has_feature', 'ccl_init', 'ccl_last_error',
    'ccl_set_log_callback', 'ccl_device_count', 'ccl_device_info',
    'ccl_node_type_count', 'ccl_node_type_info', 'ccl_socket_info',
    'ccl_socket_default_value', 'ccl_socket_enum_value',
}


def exported(text, platform):
    if platform == 'win32':
        return re.findall(r'^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)', text, re.M)
    return [line.split()[-1].removeprefix('_') if platform == 'darwin'
            else line.split()[-1] for line in text.splitlines() if line.strip()]


def check(names):
    assert len(names) == len(EXPECTED) and set(names) == EXPECTED, (
        'Unexpected/missing exports', sorted(set(names) - EXPECTED), sorted(EXPECTED - set(names)))


def self_check():
    sample = '\n'.join(f' {i} {i:X} 00001000 {n}' for i, n in enumerate(sorted(EXPECTED)))
    check(exported(sample, 'win32'))
    for names in [list(EXPECTED) + ['?native@@'], list(EXPECTED)[:-1], list(EXPECTED) + ['ccl_data']]:
        try:
            check(names)
        except AssertionError:
            continue
        raise AssertionError('Export checker accepted a broken table')


if __name__ == '__main__':
    self_check()
    tool, library = sys.argv[1:]
    args = ['/exports'] if sys.platform == 'win32' else (['-gUj'] if sys.platform == 'darwin' else ['-D', '--defined-only', '-j'])
    text = subprocess.check_output([tool, *args, library], text=True)
    names = exported(text, sys.platform)
    check(names)
    print('PASS: exactly twelve public C function exports:', ', '.join(sorted(names)))
