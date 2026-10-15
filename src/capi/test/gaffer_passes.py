"""Read saved FLOAT passes without changing their values or precision."""
import OpenEXR
import numpy as np
from workspace_guard import output

def read(root):
    root=output(root)
    result={}
    for name,labels in [('rgba','RGBA'),('normal','XYZ'),('depth','Z'),('sample_count','X')]:
        with OpenEXR.File(str(root/(name+'.exr')),separate_channels=True) as image:
            channels={k:v.pixels for k,v in image.channels().items()}
        # Native display prefixes normal channels; file output does not. Scalar
        # native passes use their pass name, while OIIO defaults to Y in the POC.
        if len(labels)==1:
            assert len(channels)==1, channels.keys()
            values=list(channels.values())
        else:
            keys=list('RGBA'[:len(labels)])
            values=[channels[k if k in channels else name+'.'+k] for k in keys]
        for label,v in zip(labels,values):
            assert v.shape==(48,64) and v.dtype==np.float32 and np.isfinite(v).all()
            result[name+'.'+label]=v
    return result

def compare(a,b):
    assert a.keys()==b.keys()
    return {k:dict(bit_identical=bool(np.array_equal(a[k].view(np.uint32),b[k].view(np.uint32))),
                   different_pixels=int(np.count_nonzero(a[k]!=b[k])),
                   max_absolute=float(np.abs(a[k].astype(np.float64)-b[k]).max())) for k in a}
