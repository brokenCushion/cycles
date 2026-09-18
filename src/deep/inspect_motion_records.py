# SPDX-License-Identifier: Apache-2.0
"""Inspect the known moving-primitives discrepancies without loading full ledgers.

Usage: python inspect_motion_records.py REVIEW_DIRECTORY
Requires CPU-records.csv and CUDA-records.csv captured at 640x480, 16 samples.
Coordinates below use the EXR file convention (top-left origin).
"""
import json
from pathlib import Path
import sys

directory = Path(sys.argv[1])
pixels = ((43, 294), (179, 315), (166, 305), (167, 165), (185, 137))
prefixes = tuple(f'{x},{y},' for x, y in pixels)
records = {}
for device in ('CPU', 'CUDA'):
    samples = {}
    with (directory / (device + '-records.csv')).open() as stream:
        for line in stream:
            if not line.startswith(prefixes):
                continue
            x, y, sample, depth, alpha, event = line.strip().split(',')
            events = samples.setdefault((int(x), int(y), int(sample)), [])
            if int(event) >= 0:
                assert int(event) == len(events), 'Incomplete event chain'
                events.append([float(depth), float(alpha)])
    assert len(samples) == len(pixels) * 16, 'Incomplete selected sample identities'
    records[device] = samples

differences = []
for key, cpu in sorted(records['CPU'].items()):
    cuda = records['CUDA'][key]
    if len(cpu) != len(cuda) or any(abs(a-b)>2e-5 or abs(c-d)>1e-6
                                   for (a,c),(b,d) in zip(cpu,cuda)):
        differences.append({'file_pixel':list(key[:2]), 'sample':key[2],
                            'CPU':cpu, 'CUDA':cuda})
report = {'sample_chain_differences':differences,
          'acceptance_passed':not differences}
(directory / 'target_record_comparison.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
sys.exit(1 if differences else 0)
