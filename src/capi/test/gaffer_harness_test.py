"""Guarded regression checks for FLOAT-pass capture and comparison adapters."""
import os,sys
sys.dont_write_bytecode=True
from workspace_guard import output,environment,self_test
self_test()
root=output(sys.argv[1]);root.mkdir(exist_ok=False)
os.environ.update(environment(root/'temp'))
import OpenEXR,numpy as np
from gaffer_passes import read,compare
expected={}
for name,keys in [('rgba','RGBA'),('normal',['normal.R','normal.G','normal.B']),
                  ('depth',['depth']),('sample_count',['sample_count'])]:
    arrays={k:np.full((48,64),np.float32(1 if name=='sample_count' else .125*(i+1)),dtype=np.float32)
            for i,k in enumerate(keys)}
    OpenEXR.File({},dict(arrays)).write(str(output(root/(name+'.exr'))))
a=read(root);assert len(a)==9
assert all(v['bit_identical'] for v in compare(a,a).values())
b={k:v.copy() for k,v in a.items()};b['rgba.R'][0,0]=np.nextafter(b['rgba.R'][0,0],np.float32(1))
assert not compare(a,b)['rgba.R']['bit_identical']
assert compare(a,b)['rgba.R']['different_pixels']==1
assert (a['sample_count.X']*16==16).all()
# Negative zero must not slip through the exact byte criterion.
b={k:v.copy() for k,v in a.items()};a['depth.Z'][0,0]=0;b['depth.Z'][0,0]=-0.0
assert not compare(a,b)['depth.Z']['bit_identical']
print('PASS guarded FLOAT roundtrip, prefixed/scalar channels, one-ULP and signed-zero rejection, count normalization')
