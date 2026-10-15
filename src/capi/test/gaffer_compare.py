"""Exact CPU RGBA acceptance, independent of EXR run-specific metadata."""
import json
import os
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment
report = output(sys.argv[3])
os.environ.update(environment(report.parent/'compare-temp'))
import numpy as np
import OpenEXR

def read(path):
    with OpenEXR.File(str(output(path)), separate_channels=True) as image:
        pixels = np.stack([image.channels()[c].pixels for c in ('R','G','B','A')],axis=-1)
    assert pixels.dtype == np.float32 and np.isfinite(pixels).all()
    return pixels

left,right = read(sys.argv[1]),read(sys.argv[2])
assert left.shape == right.shape
diff = np.abs(left.astype(np.float64)-right.astype(np.float64))
result = dict(native=sys.argv[1],api=sys.argv[2],shape=list(left.shape),
    bit_identical=bool(np.array_equal(left.view(np.uint32),right.view(np.uint32))),
    mismatched_pixels=int(np.any(left != right,axis=2).sum()),
    max_absolute_by_channel=dict(zip(('R','G','B','A'),diff.max(axis=(0,1)).tolist())))
report.write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
assert result['bit_identical'], 'CPU exact render acceptance failed'
