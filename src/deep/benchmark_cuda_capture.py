# SPDX-License-Identifier: Apache-2.0
"""Sequential warm-cache CUDA capture comparison. Python: EXE FIXTURES OUT [--volume].

FIXTURES is an existing validate_cuda_gaffer.py output directory. Run before
and after a change, with no concurrent render/compile jobs. Wall times include
process startup, rendering, spill, reconstruction and output, not only kernels.
With --volume, FIXTURES is a validate_volume_capture_gaffer.py output directory.
"""
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

exe, fixtures, out = (Path(p).resolve() for p in sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)
volume = len(sys.argv) > 4 and sys.argv[4] == '--volume'
report = {'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
          'resolution': [64, 48], 'samples': 16, 'repetitions': 3, 'cases': {}}
cases = (
        ('empty', 'miss_cuda.xml', 2, True),
        ('opaque', 'opaque_cuda.xml', 1, False),
        ('mixed', 'cutout_cuda.xml', 8, True),
        ('layers64', 'layers64_cuda.xml', 64, True))
if volume:
    cases = (('miss', 'volume_miss.xml', 8, False),
             ('homogeneous', 'homogeneous.xml', 8, False),
             ('overlap', 'overlap.xml', 8, False),
             ('surface_in_fog', 'transparent_in_fog.xml', 8, False))
for name, source, capacity, transparent in cases:
    report['cases'][name] = {}
    for enabled in (False, True):
        runs = []
        for iteration in range(4):
            tag = f'{name}_{"on" if enabled else "off"}_{iteration}'
            command = [str(exe), '--background', '--quiet', '--device', 'CUDA',
                       '--shadingsys', 'svm', '--samples', '16', '--threads', '4',
                       '--width', '64', '--height', '48', '--output', str(out / (tag + '.beauty.exr'))]
            if enabled:
                command += ['--deep-output', str(out / (tag + '.deep.exr')),
                            '--deep-max-events', str(capacity)]
                if transparent:
                    command += ['--deep-transparent']
                if volume:
                    command += ['--deep-volume', '--deep-memory-mb', '64']
            command.append(str(fixtures / source))
            start = time.perf_counter()
            p = subprocess.run(command, capture_output=True, text=True, timeout=900)
            elapsed = time.perf_counter() - start
            log = p.stdout + p.stderr
            (out / (tag + '.log')).write_text(log)
            if p.returncode or 'ERROR:' in log:
                raise RuntimeError(tag + ': ' + log[-4000:])
            stages = {}
            for line in log.splitlines():
                if 'Deep CUDA capture:' not in line:
                    continue
                for key, value in re.findall(r'(\w+)=([0-9.eE+-]+)', line):
                    if key in ('buffer_bytes_each', 'medium_bytes_each', 'event_capacity', 'device_bytes_peak'):
                        stages[key] = max(stages.get(key, 0), float(value))
                    else:
                        stages[key] = stages.get(key, 0) + float(value)
            if iteration:  # First run warms the cache and is excluded.
                runs.append({'wall_seconds': elapsed, **stages})
        result = {'runs': runs, 'median_seconds': statistics.median(r['wall_seconds'] for r in runs)}
        report['cases'][name]['on' if enabled else 'off'] = result
        print(name, 'on' if enabled else 'off', result['median_seconds'], flush=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
