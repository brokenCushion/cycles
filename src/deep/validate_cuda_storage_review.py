# SPDX-License-Identifier: Apache-2.0
"""Render CUDA primitives and compare deep pixels to a saved CUDA baseline.

Gaffer Python: EXE SCENE_XML BASELINE_DIRECTORY OUTPUT_DIRECTORY.
"""
import json
import math
from pathlib import Path
import subprocess
import sys
import CyclesDeep
import Gaffer
import GafferImage
import imath

exe, source, baseline, out = (Path(p).resolve() for p in sys.argv[1:5])
out.mkdir(parents=True, exist_ok=True)
command = [str(exe), '--background', '--quiet', '--device', 'CUDA',
           '--shadingsys', 'svm', '--samples', '16', '--threads', '4',
           '--width', '640', '--height', '480', '--output', str(out / 'CUDA.beauty.exr'),
           '--deep-output', str(out / 'CUDA.deep.exr'), '--deep-transparent',
           '--deep-max-events', '8', '--deep-memory-mb', '32', str(source)]
p = subprocess.run(command, capture_output=True, text=True, timeout=1800)
(out / 'CUDA.log').write_text(p.stdout + p.stderr)
if p.returncode or 'ERROR:' in p.stderr:
    raise RuntimeError(p.stderr[-4000:])
script = Gaffer.ScriptNode()

def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node

readers = []
for i, (label, directory) in enumerate((('Before', baseline), ('After', out))):
    reader = add(label + 'Deep', GafferImage.ImageReader(), i * 45, 35)
    reader['fileName'].setValue((directory / 'CUDA.deep.exr').as_posix())
    readers.append(reader)
    cut = add(label + 'DepthCut', GafferImage.DeepSlice(), i * 45, 15)
    cut['in'].setInput(reader['out'])
    cut['flatten'].setValue(False)
    cut['farClip']['enabled'].setValue(True)
    cut['farClip']['value'].setValue(14)
    points = add(label + 'Points', CyclesDeep.DeepToPointCloud(), i * 45, -5)
    points['in'].setInput(cut['out'])
    points['verticalFieldOfView'].setValue(math.degrees(.9))
    points['maxPoints'].setValue(1000000)
    if points['out'].object('/deepPoints').numPoints <= 0:
        raise RuntimeError('Empty review point cloud')
    beauty = add(label + 'Beauty', GafferImage.ImageReader(), i * 45 - 20, 55)
    beauty['fileName'].setValue((directory / 'CUDA.beauty.exr').as_posix())

script['BeforeDepthCut']['farClip'].setInput(script['AfterDepthCut']['farClip'])
a, b = (r['out'] for r in readers)
if a['format'].getValue() != b['format'].getValue() or a['dataWindow'].getValue() != b['dataWindow'].getValue():
    raise RuntimeError('Image windows changed')
maximum = {c: 0. for c in ('Z', 'ZBack', 'A')}
ts = GafferImage.ImagePlug.tileSize()
for y in range(0, 480, ts):
    for x in range(0, 640, ts):
        origin = imath.V2i(x, y)
        if a.sampleOffsets(origin) != b.sampleOffsets(origin):
            raise RuntimeError('Deep sample counts changed')
        for channel in maximum:
            av, bv = a.channelData(channel, origin), b.channelData(channel, origin)
            if len(av) != len(bv):
                raise RuntimeError('Deep channel lengths changed')
            maximum[channel] = max(maximum[channel], max((abs(v-w) for v, w in zip(av, bv)), default=0))
report = {'maximum_channel_difference': maximum,
          'sample_counts_identical': True, 'resolution': [640, 480],
          'passed': all(value < 1e-6 for value in maximum.values())}
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
script['fileName'].setValue((out / 'cuda_storage_review.gfr').as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
if loaded['AfterPoints']['out'].object('/deepPoints').numPoints <= 0 or not report['passed']:
    raise RuntimeError('CUDA baseline comparison or saved review failed: ' + str(report))
print(json.dumps(report), flush=True)
print(script['fileName'].getValue(), flush=True)
