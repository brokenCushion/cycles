# SPDX-License-Identifier: Apache-2.0
"""Small storage/reader checks; run with the regression Python environment."""
import hashlib
from pathlib import Path
import tempfile
import zipfile
from types import SimpleNamespace
from archive_deep_samples import archive
from run_deep_regression import SCRATCH, cleanup
from sample_csv import open_samples
from deep_exr import image_tile,image_tile_size,image_channels,check_deep


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
    import OpenEXR
    from compare_deep_identity import toolchain_difference
    with tempfile.TemporaryDirectory(dir=SCRATCH,prefix='selfcheck-') as directory:
        def write(name, samples):
            channels={}
            for index, channel in enumerate(('Z','ZBack','A')):
                values=np.empty((1,1),dtype=object)
                values[0,0]=np.array([s[index] for s in samples],dtype=np.float32)
                channels[channel]=values
            path=Path(directory)/name
            OpenEXR.File({'type':OpenEXR.deepscanline,'compression':OpenEXR.ZIPS_COMPRESSION,
                          'cycles:maxTransmittanceError':1e-6},channels).write(str(path))
            return path
        a=write('a.exr',[(1,3,.75)])
        b=write('b.exr',[(1,2,.5),(2,3,.5)])
        v=toolchain_difference(a,b)
        assert not v['passed'] and v['audit_passed'] and v['count_changed_pixels']==1
        assert v['max_transmittance_difference']<1e-14
        c=write('c.exr',[(1,1,1)])
        d=write('d.exr',[(2,2,1)])
        v=toolchain_difference(c,d)
        assert not v['audit_passed'] and v['max_transmittance_difference']==1
    channels={}
    for name,value,dtype in [('Z',1,np.float32),('ZBack',1,np.float32),('A',1,np.float32),('id',7,np.uint32)]:
        values=np.empty((1,1),dtype=object);values[0,0]=np.array([value],dtype=dtype)
        channels[name]=SimpleNamespace(pixels=values)
    image=SimpleNamespace(channels=lambda:channels,header=lambda:{'type':OpenEXR.deepscanline,
        'cycles:maxTransmittanceError':1e-3,'cycles:deepIDManifest':'{"00000007":"plane"}'})
    assert check_deep(image)['passed']
    channels['id'].pixels[0,0][0]=8
    try:check_deep(image)
    except ValueError:pass
    else:raise AssertionError('An ID missing from the manifest was accepted')
    print('PASS: verified archive, streaming reader, owned cleanup and padded tiles')


if __name__=='__main__':main()
