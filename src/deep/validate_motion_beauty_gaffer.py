# SPDX-License-Identifier: Apache-2.0
"""Check full-resolution motion beauty isolation. Gaffer Python: REVIEW_DIRECTORY.

Inputs: CPU/CUDA.beauty.exr and CPU/CUDA-off.beauty.exr, rendered with
identical 640x480, 16-sample settings. Also reports the known cube-edge pixel.
"""
import json
from pathlib import Path
import sys
import GafferImage
import imath

directory = Path(sys.argv[1]).resolve()
report = {}
nodes = {}
tile = GafferImage.ImagePlug.tileSize()
for device in ('CPU', 'CUDA'):
    pair = []
    for suffix in ('', '-off'):
        node = GafferImage.ImageReader()
        node['fileName'].setValue((directory / (device+suffix+'.beauty.exr')).as_posix())
        pair.append(node)
    nodes[device] = pair
    maximum = 0.
    for y in range(0,480,tile):
        for x in range(0,640,tile):
            for channel in ('R','G','B','A'):
                data = [n['out'].channelData(channel,imath.V2i(x,y)) for n in pair]
                for j in range(min(tile,480-y)):
                    for i in range(min(tile,640-x)):
                        k=j*tile+i
                        maximum=max(maximum,abs(data[0][k]-data[1][k]))
    report[device]={'max_deep_on_off_error':maximum}
    assert maximum <= (16*2**-23 if device=='CUDA' else 0), 'Deep capture changed beauty'

# Gaffer bottom-left coordinate; EXR file coordinate is (179,315).
x,y=179,164
origin=imath.V2i(x//tile*tile,y//tile*tile)
index=(y%tile)*tile+x%tile
report['cube_edge']={'gaffer_pixel':[x,y]}
for device,pair in nodes.items():
    report['cube_edge'][device]=[n['out'].channelData('A',origin)[index] for n in pair]
(directory/'beauty_isolation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
