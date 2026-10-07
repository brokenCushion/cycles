# SPDX-License-Identifier: Apache-2.0
"""Small storage/reader checks; run with the regression Python environment."""
import hashlib
from pathlib import Path
import tempfile
import zipfile
from archive_deep_samples import archive
from run_deep_regression import SCRATCH, cleanup
from sample_csv import open_samples
from deep_exr import image_tile,image_tile_size,image_channels


def main():
    SCRATCH.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=SCRATCH,prefix='selfcheck-') as directory:
        root=Path(directory).resolve();source=root/'camera.samples.csv'
        data=b'file_x,file_y,sample\r\n0,0,0\r\n'*4096;source.write_bytes(data)
        records=[]
        def publish(record):
            assert source.exists() and Path(record['archive']).exists()
            records.append(record)
        result=archive(dict(path=str(source),physical_path=str(source),required_uncompressed=False),publish)
        assert records==[result] and not source.exists()
        assert result['sha256']==hashlib.sha256(data).hexdigest()
        with zipfile.ZipFile(result['archive']) as zipped:assert zipped.read(source.name)==data
        with open_samples(source) as stream:assert stream.read()==data.decode()
        try:cleanup(root.parent.parent)
        except ValueError:pass
        else:raise AssertionError('Cleanup accepted a foreign folder')
        child=root/'foreign';child.mkdir()
        try:cleanup(child)
        except ValueError:pass
        else:raise AssertionError('Cleanup accepted a nested foreign folder')
        assert root.exists() and child.exists()
    owned=Path(tempfile.mkdtemp(dir=SCRATCH,prefix='selfcheck-'));(owned/'large.samples.csv').write_text('owned')
    cleanup(owned);assert not owned.exists()
    # SDK Y-up tiles must match the established Gaffer raw-policy coordinates.
    import numpy as np
    tile=image_tile({'channels':{'R':np.arange(6,dtype=np.float32).reshape(2,3)}},'R',(0,0))
    size=image_tile_size()
    assert len(tile)==size*size and list(tile[:3])==[0,1,2] and list(tile[size:size+3])==[3,4,5]
    assert image_channels({'channels':dict.fromkeys(('Layer.B','Layer.A','Layer.R','Layer.G'))})==['Layer.R','Layer.G','Layer.B','Layer.A']
    print('PASS: verified archive, streaming reader, owned cleanup and padded tiles')


if __name__=='__main__':main()
