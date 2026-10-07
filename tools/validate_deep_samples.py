# SPDX-License-Identifier: Apache-2.0
"""gaffer env python SCRIPT CYCLES_EXE OUTPUT: standalone sample-prefix checks."""
import csv
import subprocess
import sys
from pathlib import Path
import GafferImage

exe, out = (Path(v).resolve() for v in sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
fixture = Path(__file__).resolve().parents[1] / 'src/app/deep_output_driver_volume_test.xml'
for limit in (0, 1, 2, 64, -1):
    target = out / ('samples_' + str(limit) + '.deep.exr')
    ledger = target.with_suffix('.csv')
    if limit < 0:
        target.write_bytes(b'previous-complete-frame')
    command = [str(exe), '--background', '--quiet', '--device', 'CPU', '--shadingsys', 'svm',
               '--width', '8', '--height', '6', '--samples', '5', '--threads', '2',
               '--output', str(target.with_suffix('.beauty.exr')), '--deep-output', str(target),
               '--deep-records', str(ledger), '--deep-volume', '--deep-error', 'strict',
               '--deep-memory-mb', '64', '--deep-samples', str(limit), str(fixture)]
    result = subprocess.run(command, capture_output=True, text=True)
    target.with_suffix('.log').write_text(result.stdout + result.stderr)
    if limit < 0:
        assert result.returncode != 0 and target.read_bytes() == b'previous-complete-frame'
        assert 'sample limit must be nonnegative' in result.stdout + result.stderr
        continue
    assert result.returncode == 0, result.stdout + result.stderr
    reader = GafferImage.ImageReader(); reader['fileName'].setValue(target.as_posix())
    attribute = reader['out']['metadata'].getValue().get('cycles:deepSamples')
    count = min(5, limit) if limit else 5
    assert (attribute.value == count) if limit else attribute is None
    populations = {}
    with ledger.open() as stream:
        for row in csv.DictReader(stream):
            key = (int(row['file_x']), int(row['file_y']))
            populations.setdefault(key, set()).add(int(row['sample']))
    assert len(populations) == 48 and all(v == set(range(count)) for v in populations.values())
print('PASS standalone limits 0/1/2/64, exact prefix/misses, header and negative atomic rejection')
