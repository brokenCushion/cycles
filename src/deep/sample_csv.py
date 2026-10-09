# SPDX-License-Identifier: Apache-2.0
"""Stream raw or archived diagnostics without expanding them onto disk."""
from contextlib import contextmanager
import io
from pathlib import Path
import zipfile


def samples_exist(path):
    path = Path(path)
    return path.is_file() or Path(str(path) + '.zip').is_file()


@contextmanager
def open_samples(path):
    path = Path(path)
    if path.is_file():
        with path.open(encoding='utf-8', newline='') as stream:
            yield stream
    else:
        with zipfile.ZipFile(str(path) + '.zip') as archive:
            if archive.namelist() != [path.name]:
                raise ValueError('Unexpected diagnostic archive entries')
            with archive.open(path.name) as binary, io.TextIOWrapper(binary, encoding='utf-8', newline='') as stream:
                yield stream
