# SPDX-License-Identifier: Apache-2.0
"""Report pixel differences for linear beauty EXRs using Gaffer's reader.

gaffer env python SCRIPT REFERENCE CANDIDATE [CANDIDATE...]
This reports measurements; it does not choose an acceptance tolerance.
"""
import json
import math
from pathlib import Path
import sys
import GafferImage
import imath

def read(path):
    node = GafferImage.ImageReader()
    node['fileName'].setValue(Path(path).resolve().as_posix())
    assert not node['out']['deep'].getValue(), 'Expected flat image'
    return node

reference = read(sys.argv[1])
window = reference['out']['dataWindow'].getValue()
tile_size = GafferImage.ImagePlug.tileSize()
report = {'reference': str(Path(sys.argv[1]).resolve()), 'comparisons': {}}
for path in sys.argv[2:]:
    candidate = read(path)
    assert candidate['out']['dataWindow'].getValue() == window
    channels = {}
    for name in ('R','G','B','A'):
        maximum, changed, nonfinite = 0, 0, 0
        for y in range(window.min().y//tile_size*tile_size, window.max().y, tile_size):
            for x in range(window.min().x//tile_size*tile_size, window.max().x, tile_size):
                origin = imath.V2i(x,y)
                a = reference['out'].channelData(name,origin)
                b = candidate['out'].channelData(name,origin)
                for py in range(max(y,window.min().y),min(y+tile_size,window.max().y)):
                    for px in range(max(x,window.min().x),min(x+tile_size,window.max().x)):
                        i = (py-y)*tile_size+px-x
                        va,vb = float(a[i]),float(b[i])
                        nonfinite += not (math.isfinite(va) and math.isfinite(vb))
                        changed += va != vb
                        maximum = max(maximum,abs(va-vb))
        channels[name] = {'max_absolute_error':maximum, 'changed_pixels':changed,
                          'nonfinite_pixels':nonfinite}
    report['comparisons'][str(Path(path).resolve())] = channels
print(json.dumps(report,indent=2))
