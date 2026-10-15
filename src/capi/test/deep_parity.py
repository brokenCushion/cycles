"""Byte-for-byte host-C-API versus native standalone deep EXR qualification."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk, REPO


def main():
    root = output(sys.argv[1])
    install = output(root / 'install')
    api_outputs = output(sys.argv[2] if len(sys.argv) > 2 else root / 'build/src/capi/deep-tests')
    dest = output(sys.argv[3] if len(sys.argv) > 3 else root / 'standalone-comparison')
    dest.mkdir(exist_ok=False)
    env = environment(root / 'temp')
    results = []
    for transparent in (False, True):
        for ids in (False, True):
            disk()
            label = ('transparent' if transparent else 'opaque') + ('-ids' if ids else '')
            fixture = REPO / 'src/app' / ('deep_output_driver_transparent_test.xml' if transparent
                                         else 'deep_output_driver_test.xml')
            target = output(dest / (label + '-native.exr'))
            args = [str(install / 'cycles.exe'), '--background', '--quiet', '--device', 'CPU',
                    '--threads', '4', '--samples', '16', '--width', '47', '--height', '20',
                    '--deep-output', str(target), '--deep-error', '1e-3' if ids else 'strict',
                    '--deep-memory-mb', '64', '--deep-max-events', '16',
                    '--deep-z-tolerance', '1e-4', '--deep-samples', '0']
            if ids:
                args.append('--deep-ids')
            if transparent:
                args.append('--deep-transparent')
            args.append(str(fixture))
            output(dest / (label + '-command.json')).write_text(json.dumps(args, indent=2))
            with output(dest / (label + '.log')).open('w') as log:
                subprocess.run(args, cwd=dest, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True)
            native = target.read_bytes()
            subject = output(api_outputs / (label + '-api.exr')).read_bytes()
            row = dict(case=label, byte_identical=native == subject,
                       native_bytes=len(native), api_bytes=len(subject),
                       native_sha256=hashlib.sha256(native).hexdigest(),
                       api_sha256=hashlib.sha256(subject).hexdigest())
            results.append(row)
            output(dest / 'deep-parity.json').write_text(json.dumps(results, indent=2) + '\n')
            assert row['byte_identical'], row
            print('PASS byte identity:', label, flush=True)


if __name__ == '__main__':
    main()
