# SPDX-License-Identifier: Apache-2.0
"""Archive approved historical sample CSVs; verify bytes before replacing originals."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import ctypes
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import threading
import zipfile


def allocated(path):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCompressedFileSizeW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_uint32)]
    kernel.GetCompressedFileSizeW.restype=ctypes.c_uint32
    ctypes.set_last_error(0)
    high=ctypes.c_uint32();low=kernel.GetCompressedFileSizeW(str(path),ctypes.byref(high))
    if low==0xffffffff and ctypes.get_last_error():raise ctypes.WinError(ctypes.get_last_error())
    return (high.value<<32)|low


def physical_source(row):
    # Use the resolved physical path, never unlink through a junction folder.
    source=Path(row['physical_path'])
    allowed=[Path('D:/CyclesDeepArchive').resolve(),Path('D:/CyclesDeepScratch').resolve(),
             (Path(__file__).resolve().parents[1]/'builds').resolve()]
    if source.resolve()!=source or not any(source.is_relative_to(root) for root in allowed):
        raise ValueError('Archive source outside approved storage: '+str(source))
    if row['required_uncompressed'] or not source.name.endswith('.samples.csv'):
        raise ValueError('Archive source must be an unneeded sample CSV')
    return source


def archive(row,publish):
    source=physical_source(row);target=Path(str(source)+'.zip')
    if target.exists():raise FileExistsError('Existing archive needs manual verification: '+str(target))
    before=source.stat();before_allocated=allocated(source);digest=hashlib.sha256()
    if 'mtime_ns' in row and (before.st_size,before.st_mtime_ns)!=(row['bytes'],row['mtime_ns']):
        raise ValueError('Source changed since inventory: '+str(source))
    with tempfile.NamedTemporaryFile(dir=source.parent,suffix='.zip.tmp',delete=False) as staging:
        temporary=Path(staging.name)
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True) as zipped:
            with source.open('rb') as original,zipped.open(source.name,'w',force_zip64=True) as output:
                for chunk in iter(lambda:original.read(1024*1024),b''):
                    digest.update(chunk);output.write(chunk)
        restored=hashlib.sha256();size=0
        with zipfile.ZipFile(temporary) as zipped:
            if zipped.namelist()!=[source.name]:raise ValueError('Archive contains unexpected entries')
            with zipped.open(source.name) as stream:
                for chunk in iter(lambda:stream.read(1024*1024),b''):
                    restored.update(chunk);size+=len(chunk)
        current=source.stat()
        if (size!=before.st_size or restored.digest()!=digest.digest() or
            (current.st_size,current.st_mtime_ns)!=(before.st_size,before.st_mtime_ns)):
            raise ValueError('Archive verification failed or source changed: '+str(source))
        os.replace(temporary,target)
        record=dict(path=row['path'],physical_path=str(source),archive=str(target),passed=True,
                    sha256=digest.hexdigest(),original_bytes=size,archive_bytes=target.stat().st_size,
                    original_allocated=before_allocated,archive_allocated=allocated(target))
        # Durable verification record precedes replacement, so a crash can resume.
        publish(record)
        # User approved replacement after full decompression verification.
        source.unlink()
        return record
    finally:
        if temporary.exists():temporary.unlink()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory',type=Path)
    parser.add_argument('--replace-verified',action='store_true',required=True,
                        help='Explicit authorization to replace originals after byte verification')
    args=parser.parse_args();start=time.monotonic();inventory=json.loads(args.inventory.read_text())
    rows=[r for r in inventory['files'] if 'alias_of' not in r and not r['required_uncompressed']]
    events=args.inventory.with_name('samples-archives.jsonl');done=[]
    if events.exists():done=[json.loads(line) for line in events.read_text().splitlines()]
    approved={row['physical_path']:row for row in rows}
    for record in done:
        source=physical_source(approved[record['physical_path']]);target=Path(str(source)+'.zip')
        if target.resolve()!=target:raise ValueError('Published archive became a reparse point')
        restored=hashlib.sha256()
        with zipfile.ZipFile(target) as zipped,zipped.open(source.name) as stream:
            for chunk in iter(lambda:stream.read(1024*1024),b''):restored.update(chunk)
        if restored.hexdigest()!=record['sha256']:raise ValueError('Published archive changed: '+str(target))
        if source.exists():
            with source.open('rb') as stream:
                original=hashlib.file_digest(stream,'sha256').hexdigest()
            if original!=record['sha256']:raise ValueError('Source changed after archive publication')
            source.unlink()
    completed={r['physical_path'] for r in done};rows=[r for r in rows if r['physical_path'] not in completed]
    lock=threading.Lock();errors=[]
    with ThreadPoolExecutor(max_workers=2) as workers,events.open('a') as log:
        def publish(record):
            with lock:
                log.write(json.dumps(record)+'\n');log.flush();os.fsync(log.fileno());done.append(record)
        pending=[workers.submit(archive,row,publish) for row in sorted(rows,key=lambda r:-r['bytes'])]
        for future in as_completed(pending):
            try:future.result()
            except Exception as error:errors.append(str(error))
    report=dict(passed=not errors,errors=errors,files=len(done),original_bytes=sum(r['original_bytes'] for r in done),
                archive_bytes=sum(r['archive_bytes'] for r in done),
                allocated_bytes_saved=sum(r['original_allocated']-r['archive_allocated'] for r in done),
                sha256_verified=True,seconds=time.monotonic()-start,entries=done)
    args.inventory.with_name('samples-archives.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='entries'},indent=2))
    if errors:raise SystemExit(1)


if __name__=='__main__':main()
