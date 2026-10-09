# SPDX-License-Identifier: Apache-2.0
"""Check exact uniform-scale equivalence of two rendered native deep files.

gaffer env python SCRIPT BASE.deep.exr SCALED.deep.exr REPORT.json
The fixture doubles world distances and halves extinction, retaining rotation.
"""
import json
from pathlib import Path
import sys
import GafferImage
import imath

readers = []
for path in sys.argv[1:3]:
    node = GafferImage.ImageReader()
    node['fileName'].setValue(Path(path).resolve().as_posix())
    if not node['out']['deep'].getValue():
        raise RuntimeError('Expected a deep render')
    readers.append(node)
a, b = [node['out'] for node in readers]
fmt = a['format'].getValue()
if fmt != b['format'].getValue():
    raise RuntimeError('Mismatched scale fixture formats')
size = GafferImage.ImagePlug.tileSize()
count = 0
alpha_equal = True
offsets_equal = True
maximum_depth_error = 0.0
for y in range(0, fmt.height(), size):
    for x in range(0, fmt.width(), size):
        origin = imath.V2i(x, y)
        offsets_equal &= a.sampleOffsets(origin) == b.sampleOffsets(origin)
        aa, ba = a.channelData('A', origin), b.channelData('A', origin)
        alpha_equal &= aa == ba
        count += len(aa)
        for channel in ('Z', 'ZBack'):
            av, bv = a.channelData(channel, origin), b.channelData(channel, origin)
            if len(av) != len(bv):
                maximum_depth_error = float('inf')
            else:
                maximum_depth_error = max(maximum_depth_error,
                    max((abs(float(v)-float(w)/2) for v, w in zip(av, bv)), default=0))
report = {'scope': 'native VDB render uniform scale/physical extinction invariance',
          'samples_checked': count, 'sample_offsets_identical': offsets_equal,
          'alpha_identical': alpha_equal, 'max_normalized_depth_error': maximum_depth_error,
          'passed': offsets_equal and alpha_equal and maximum_depth_error == 0}
Path(sys.argv[3]).write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
if not report['passed']:
    raise RuntimeError('Rendered deep output changed under the exact scale fixture')
