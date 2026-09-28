# SPDX-License-Identifier: Apache-2.0
"""Measure one render process: Python SCRIPT OUTPUT_DIRECTORY -- COMMAND ...

Requires psutil. Run renders sequentially without other render/compile jobs.
I/O counters include all process I/O, not just spill; GPU memory is device-wide.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

import psutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ['--'] else args.command
if not command:
    parser.error('A render command is required')
args.directory.mkdir(parents=True, exist_ok=True)
report_path = args.directory / 'measurement.json'
log_path = args.directory / 'process.log'
if report_path.exists() or log_path.exists():
    parser.error('Use a fresh measurement directory')
report = {'command': command, 'sample_period_seconds': 1,
          'scope': 'One process; sampled I/O includes all files; GPU memory is device-wide',
          'max_sampled_rss': 0, 'peak_working_set': 0, 'max_sampled_private': 0,
          'max_sampled_read_bytes': 0, 'max_sampled_write_bytes': 0,
          'max_device_memory_mib': None, 'monitor_errors': []}
smi = shutil.which('nvidia-smi')
start = time.monotonic()
with log_path.open('w') as log:
    child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    process = psutil.Process(child.pid)
    while child.poll() is None:
        try:
            memory, io = process.memory_info(), process.io_counters()
            for key, value in (
                ('max_sampled_rss', memory.rss),
                ('peak_working_set', getattr(memory, 'peak_wset', memory.rss)),
                ('max_sampled_private', getattr(memory, 'private', memory.vms)),
                ('max_sampled_read_bytes', io.read_bytes),
                ('max_sampled_write_bytes', io.write_bytes)):
                report[key] = max(report[key], value)
        except psutil.NoSuchProcess:
            break
        except psutil.AccessDenied as error:
            if str(error) not in report['monitor_errors']:
                report['monitor_errors'].append(str(error))
        if smi:
            try:
                query = subprocess.run(
                    [smi, '--query-gpu=memory.used', '--format=csv,noheader,nounits'],
                    capture_output=True, text=True, timeout=5, check=True)
                used = sum(int(line.strip()) for line in query.stdout.splitlines())
                report['max_device_memory_mib'] = max(report['max_device_memory_mib'] or 0, used)
            except (ValueError, subprocess.SubprocessError) as error:
                report['monitor_errors'].append(str(error))
                smi = None
        try:
            child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            pass
    report['exit_code'] = child.wait()
report['wall_seconds'] = time.monotonic() - start
report['passed_process'] = report['exit_code'] == 0
executable = Path(command[0])
if executable.is_file():
    report['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
report_path.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
raise SystemExit(report['exit_code'])
