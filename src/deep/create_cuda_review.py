# SPDX-License-Identifier: Apache-2.0
"""Render the existing three-mesh fixture on CPU/CUDA and save a Gaffer review.

Run in Gaffer Python: EXE SCENE_XML OUTPUT_DIR [REVIEW_FILENAME], with CUDA compiler environment.
"""
import json
import math
from pathlib import Path
import subprocess
import sys
import time
import CyclesDeep
import Gaffer
import GafferImage
import imath

exe, source, out = (Path(p).resolve() for p in sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)
review_filename = sys.argv[4] if len(sys.argv) > 4 else 'm6_review.gfr'
script = Gaffer.ScriptNode()
report = {'resolution': [640, 480], 'samples': 16, 'renders': {}}

def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node

for index, device in enumerate(('CPU', 'CUDA')):
    deep = out / (device + '.deep.exr')
    beauty = out / (device + '.beauty.exr')
    command = [str(exe), '--background', '--quiet', '--device', device,
        '--shadingsys', 'svm', '--samples', '16', '--threads', '12',
        '--width', '640', '--height', '480', '--output', str(beauty),
        '--deep-output', str(deep), '--deep-transparent', '--deep-max-events', '8',
        '--deep-memory-mb', '32', str(source)]
    start = time.monotonic()
    p = subprocess.run(command, capture_output=True, text=True, timeout=1800)
    (out / (device + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode or 'ERROR:' in p.stderr:
        raise RuntimeError(p.stderr[-4000:])
    report['renders'][device] = {'seconds': time.monotonic() - start,
        'deep_bytes': deep.stat().st_size}
    reader = add(device + '_Deep', GafferImage.ImageReader(), index * 40, 30)
    reader['fileName'].setValue(deep.as_posix())
    points = add(device + '_DeepToPointCloud', CyclesDeep.DeepToPointCloud(), index * 40, 10)
    points['in'].setInput(reader['out'])
    points['verticalFieldOfView'].setValue(math.degrees(.9))
    points['maxPoints'].setValue(1000000)
    Gaffer.Metadata.registerValue(points, 'description',
        'Pixel-centre projection of deep axial depth. With depth of field this displays the depth distribution, not the original lens-ray hit positions.')
    flat = add(device + '_Flat', GafferImage.DeepToFlat(), index * 40, -10)
    flat['in'].setInput(reader['out'])
    cut = add(device + '_DepthCut', GafferImage.DeepSlice(), index * 40, -30)
    cut['in'].setInput(reader['out'])
    cut['farClip']['enabled'].setValue(True)
    cut['farClip']['value'].setValue(5.2)
    cut['flatten'].setValue(True)
    beauty_reader = add(device + '_Beauty', GafferImage.ImageReader(), index * 40, 50)
    beauty_reader['fileName'].setValue(beauty.as_posix())
    cloud = points['out'].object('/deepPoints')
    if not cloud.numPoints or not cloud.arePrimitiveVariablesValid():
        raise RuntimeError('Invalid deep point cloud')
    report['renders'][device]['review_points'] = cloud.numPoints
    print(device, report['renders'][device], flush=True)

report['holdouts'] = {}
tile = GafferImage.ImagePlug.tileSize()
for depth in (3., 4., 5.2, 6.5, 8.8, 10., 12., 14.):
    nodes = [script[d + '_DepthCut'] for d in ('CPU', 'CUDA')]
    for node in nodes:
        node['farClip']['value'].setValue(depth)
    maximum = 0.
    changed = 0
    for y in range(0, 480, tile):
        for x in range(0, 640, tile):
            data = [n['out'].channelData('A', imath.V2i(x, y)) for n in nodes]
            for j in range(min(tile, 480 - y)):
                for i in range(min(tile, 640 - x)):
                    error = abs(data[0][j * tile + i] - data[1][j * tile + i])
                    maximum = max(maximum, error)
                    changed += error > 1e-6
    report['holdouts'][str(depth)] = {'max_alpha_difference': maximum,
        'pixels_over_1e-6': changed}
    print('holdout', depth, report['holdouts'][str(depth)], flush=True)
for device in ('CPU', 'CUDA'):
    script[device + '_DepthCut']['farClip']['value'].setValue(5.2)
script['fileName'].setValue((out / review_filename).as_posix())
script.save()
(out / 'report.json').write_text(json.dumps(report, indent=2))
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
assert loaded['CUDA_DeepToPointCloud']['out'].object('/deepPoints').numPoints > 0
print('Saved and reloaded', script['fileName'].getValue(), flush=True)
