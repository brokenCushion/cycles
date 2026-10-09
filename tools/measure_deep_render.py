# SPDX-License-Identifier: Apache-2.0
"""Measure one render process: Python SCRIPT OUTPUT_DIRECTORY -- COMMAND ...

Requires psutil. Run renders sequentially without other render/compile jobs.
I/O counters include all process I/O, not just spill. Device-wide GPU memory is
the primary metric; Windows dedicated per-process counters additionally aid attribution.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
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
progress_path = args.directory / 'measurement-progress.json'
log_path = args.directory / 'process.log'
if report_path.exists() or log_path.exists() or progress_path.exists():
    parser.error('Use a fresh measurement directory')
report = {'command': command, 'sample_period_seconds': 1,
          'scope': 'One process; sampled I/O includes all files; GPU memory is device-wide',
          'max_sampled_rss': 0, 'peak_working_set': 0, 'max_sampled_private': 0,
          'max_sampled_read_bytes': 0, 'max_sampled_write_bytes': 0,
          'max_device_memory_mib': None, 'max_device_memory_at_seconds': None,
          'max_device_memory_at_utc': None, 'max_device_memory_log_tail': None,
          'max_process_gpu_dedicated_bytes': None, 'process_gpu_samples': 0,
          'process_gpu_memory_scope': 'Windows GPU Process Memory dedicated counter, summed across adapters',
          'process_gpu_monitor_errors': [],
          'monitor_errors': []}
smi = shutil.which('nvidia-smi')
gpu_process = None
if os.name == 'nt':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from windows_gpu_memory import WindowsGpuMemory
    try:
        gpu_process = WindowsGpuMemory()
    except OSError as error:
        report['process_gpu_monitor_errors'].append(str(error))


def publish_progress(value):
    temporary = progress_path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    try:
        temporary.replace(progress_path)
    except PermissionError:
        # A Windows reader may temporarily deny replacement. This optional
        # snapshot must not abort a render; the final measurement is authoritative.
        pass


start = time.monotonic()
last_progress = start - 30
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
        if gpu_process:
            try:
                dedicated = gpu_process.sample(child.pid)
                if dedicated is not None:
                    report['process_gpu_samples'] += 1
                    report['max_process_gpu_dedicated_bytes'] = max(
                        report['max_process_gpu_dedicated_bytes'] or 0, dedicated)
            except OSError as error:
                report['process_gpu_monitor_errors'].append(str(error))
                gpu_process.close()
                gpu_process = None
        if smi:
            try:
                query = subprocess.run(
                    [smi, '--query-gpu=memory.used', '--format=csv,noheader,nounits'],
                    capture_output=True, text=True, timeout=5, check=True)
                used = sum(int(line.strip()) for line in query.stdout.splitlines())
                if used > (report['max_device_memory_mib'] or 0):
                    report['max_device_memory_mib'] = used
                    report['max_device_memory_at_seconds'] = time.monotonic() - start
                    report['max_device_memory_at_utc'] = datetime.now(timezone.utc).isoformat()
                    with log_path.open('rb') as snapshot:
                        snapshot.seek(0, 2)
                        snapshot.seek(max(0, snapshot.tell() - 2048))
                        report['max_device_memory_log_tail'] = snapshot.read().decode('utf-8', 'replace')
            except (ValueError, subprocess.SubprocessError) as error:
                report['monitor_errors'].append(str(error))
                smi = None
        if time.monotonic() - last_progress >= 30:
            live = dict(report, running=True, elapsed_seconds=time.monotonic() - start,
                        process_id=child.pid)
            # Replace a small snapshot atomically; readers never see half JSON.
            publish_progress(live)
            last_progress = time.monotonic()
        try:
            child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            pass
    report['exit_code'] = child.wait()
if gpu_process:
    gpu_process.close()
report['wall_seconds'] = time.monotonic() - start
report['passed_process'] = report['exit_code'] == 0
executable = Path(command[0])
if executable.is_file():
    report['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
report_path.write_text(json.dumps(report, indent=2) + '\n')
publish_progress(dict(report, running=False))
print(json.dumps(report, indent=2), flush=True)
raise SystemExit(report['exit_code'])
