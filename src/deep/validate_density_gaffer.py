# SPDX-License-Identifier: Apache-2.0
"""Validate the known linear-density reference and save its Gaffer review.

Run with gaffer env python SCRIPT FIXTURE_DIRECTORY.
"""
import csv
import json
from pathlib import Path
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

directory = Path(sys.argv[1]).resolve()
script = Gaffer.ScriptNode()


def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


reader = add('AnalyticDensitySphere', GafferImage.ImageReader(), 0, 40)
reader['fileName'].setValue((directory / 'density_sphere.exr').as_posix())
assert reader['out']['deep'].getValue()
cut = add('DensityDepthCut', GafferImage.DeepSlice(), 0, 20)
cut['in'].setInput(reader['out'])
cut['nearClip']['enabled'].setValue(False)
cut['farClip']['enabled'].setValue(True)
cut['flatten'].setValue(True)
rows_by_depth = {}
with (directory / 'density_expected.csv').open(newline='') as stream:
    for row in csv.DictReader(stream):
        rows_by_depth.setdefault(float(row['depth']), []).append(row)
maximum = 0
size = GafferImage.ImagePlug.tileSize()
for depth, rows in rows_by_depth.items():
    cut['farClip']['value'].setValue(depth)
    data = cut['out'].channelData('A', imath.V2i(0))
    for row in rows:
        x, y = int(row['x']), 23-int(row['file_y'])
        maximum = max(maximum, abs(data[y*size+x]-float(row['alpha'])))
cut['farClip']['value'].setValue(5.7)
deep_cut = add('CloudDepthCut', GafferImage.DeepSlice(), 30, 20)
deep_cut['in'].setInput(reader['out'])
deep_cut['nearClip']['enabled'].setValue(False)
deep_cut['farClip'].setInput(cut['farClip'])
deep_cut['flatten'].setValue(False)
camera = add('ReferenceCamera', GafferScene.Camera(), 65, 40)
camera['name'].setValue('camera')
camera['projection'].setValue('orthographic')
camera['orthographicAperture'].setValue(imath.V2f(32/6, 24/6))
report = {'scope': 'M8d1 known linear-density reference; not a Cycles/VDB render',
          'max_alpha_error': maximum, 'cuts': len(rows_by_depth),
          'passed': maximum <= 1e-6}
for name, channel, color, x in (
        ('IntervalFrontPoints', 'Z', imath.Color3f(.15, .7, 1), 20),
        ('IntervalBackPoints', 'ZBack', imath.Color3f(1, .4, .1), 50)):
    cloud = add(name, CyclesDeep.DeepToPointCloud(), x, 0)
    cloud['in'].setInput(deep_cut['out'])
    cloud['camera'].setInput(camera['out'])
    cloud['depthChannel'].setValue(channel)
    cloud['color'].setValue(color)
    cloud['useImageColor'].setValue(False)
    cloud['pointWidth'].setValue(2)
    points = cloud['out'].object('/deepPoints')
    assert points.numPoints and points.arePrimitiveVariablesValid()
    # Verify reconstruction is actually orthographic and has the fixture scale.
    for p in points['P'].data:
        assert abs(p.x) < 1.8 and abs(p.y) < 1.8 and -5.701 <= p.z < -3.19
    report[name] = points.numPoints
group = add('DensityIntervalPoints', GafferScene.Group(), 35, -20)
group['in'][0].setInput(script['IntervalFrontPoints']['out'])
group['in'][1].setInput(script['IntervalBackPoints']['out'])
flat = add('FullDensityAlpha', GafferImage.DeepToFlat(), -25, 20)
flat['in'].setInput(reader['out'])
note = add('ReviewInstructions', Gaffer.Backdrop(), -20, 70)
note['title'].setValue('M8d1: known linear-density reference')
note['description'].setValue('Analytic fixture, NOT a Cycles or VDB render.\n'
    'View DensityIntervalPoints: blue interval fronts, orange backs.\n'
    'Adjust DensityDepthCut farClip to slice the sphere.\n'
    'Points are EXR interval boundaries, not scattering particles.\n'
    'View FullDensityAlpha or DensityDepthCut in alpha mode.\n'
    'Native heterogeneous capture remains future work.')
script['fileName'].setValue((directory / 'm8d1_density_reference.gfr').as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
assert loaded['IntervalFrontPoints']['out'].object('/deepPoints').numPoints == report['IntervalFrontPoints']
(directory / 'gaffer_validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2), flush=True)
assert report['passed'], 'Gaffer density cuts exceed 1e-6'
