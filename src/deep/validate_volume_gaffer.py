# SPDX-License-Identifier: Apache-2.0
"""Validate M8 analytic EXRs with Gaffer DeepSlice and save a live cloud review.

Run with gaffer env python SCRIPT FIXTURE_DIRECTORY.
"""
import csv
import json
import math
from pathlib import Path
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

directory = Path(sys.argv[1]).resolve()
script = Gaffer.ScriptNode()
tile_size = GafferImage.ImagePlug.tileSize()
report = {'scope': 'analytic reference; no Cycles volume capture', 'fixtures': {},
          'max_alpha_error': 0.0}


def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


def reader_for(name):
    reader = GafferImage.ImageReader()
    reader['fileName'].setValue((directory / (name + '.exr')).as_posix())
    if not reader['out']['deep'].getValue():
        raise RuntimeError('Expected deep EXR: ' + name)
    return reader


def sliced(reader):
    node = GafferImage.DeepSlice()
    node['in'].setInput(reader['out'])
    node['nearClip']['enabled'].setValue(False)
    node['farClip']['enabled'].setValue(True)
    node['flatten'].setValue(True)
    return node


with (directory / 'expected.csv').open(newline='') as stream:
    grouped = {}
    for row in csv.DictReader(stream):
        grouped.setdefault(row['fixture'], []).append(row)
for name, rows in grouped.items():
    reader = reader_for(name)
    cut = sliced(reader)
    maximum = 0
    for row in rows:
        depth = float(row['depth'])
        # DeepSlice uses an exclusive far boundary for point samples.
        # Interior checks here avoid the surface boundary convention.
        if depth == 5 and name in ('crossing_surface', 'opaque_surface', 'mixed_extinction'):
            continue
        cut['farClip']['value'].setValue(depth)
        data = cut['out'].channelData('A', imath.V2i(0))
        expected = float(row['alpha'])
        for y in range(8):
            for x in range(8):
                maximum = max(maximum, abs(data[y * tile_size + x] - expected))
    report['fixtures'][name] = maximum
    print(name, maximum, flush=True)
    report['max_alpha_error'] = max(report['max_alpha_error'], maximum)

reader = add('AnalyticVolumeSpheres', reader_for('analytic_spheres'), 0, 40)
Gaffer.Metadata.registerValue(reader, 'description',
    'Analytic Beer-Lambert reference: two overlapping homogeneous spheres. '
    'This file is not a Cycles render. Scalar extinction only; no RGB.')
cut = add('FogDepthCut', sliced(reader), 0, 20)
rows_by_depth = {}
with (directory / 'spheres_expected.csv').open(newline='') as stream:
    for row in csv.DictReader(stream):
        rows_by_depth.setdefault(float(row['depth']), []).append(row)
maximum = 0
for depth, rows in rows_by_depth.items():
    cut['farClip']['value'].setValue(depth)
    tiles = {}
    for row in rows:
        x, y = int(row['x']), 119 - int(row['file_y'])
        origin = (x // tile_size * tile_size, y // tile_size * tile_size)
        if origin not in tiles:
            tiles[origin] = cut['out'].channelData('A', imath.V2i(*origin))
        actual = tiles[origin][(y % tile_size) * tile_size + x % tile_size]
        maximum = max(maximum, abs(actual - float(row['alpha'])))
report['spheres_max_alpha_error'] = maximum
report['max_alpha_error'] = max(report['max_alpha_error'], maximum)
cut['farClip']['value'].setValue(5.5)
# A deep-preserving slice supplies interval endpoints to the live point clouds.
deep_cut = add('SliceForPointClouds', GafferImage.DeepSlice(), 30, 20)
deep_cut['in'].setInput(reader['out'])
deep_cut['farClip'].setInput(cut['farClip'])
deep_cut['nearClip']['enabled'].setValue(False)
deep_cut['flatten'].setValue(False)
for name, channel, color, x in (
        ('IntervalFrontPoints', 'Z', imath.Color3f(.15, .7, 1), 20),
        ('IntervalBackPoints', 'ZBack', imath.Color3f(1, .4, .1), 50)):
    cloud = add(name, CyclesDeep.DeepToPointCloud(), x, 0)
    cloud['in'].setInput(deep_cut['out'])
    cloud['depthChannel'].setValue(channel)
    cloud['color'].setValue(color)
    cloud['useImageColor'].setValue(False)
    cloud['maxPoints'].setValue(1000000)
    cloud['verticalFieldOfView'].setValue(math.degrees(.9))
    points = cloud['out'].object('/deepPoints')
    if points.numPoints == 0 or not points.arePrimitiveVariablesValid():
        raise RuntimeError('Invalid volume point cloud')
    report[name] = points.numPoints
group = add('VolumeIntervalPoints', GafferScene.Group(), 35, -20)
group['in'][0].setInput(script['IntervalFrontPoints']['out'])
group['in'][1].setInput(script['IntervalBackPoints']['out'])
Gaffer.Metadata.registerValue(group, 'description',
    'Blue = interval fronts; orange = interval backs. These are deep EXR '
    'boundaries, not scattering particles. Adjust FogDepthCut farClip to slice the fog.')
flat = add('FullFogAlpha', GafferImage.DeepToFlat(), -25, 20)
flat['in'].setInput(reader['out'])
note = add('ReviewInstructions', Gaffer.Backdrop(), -20, 70)
note['title'].setValue('M8a: analytic volume intervals')
note['description'].setValue('View VolumeIntervalPoints: blue fronts, orange backs.\n'
    'Adjust FogDepthCut farClip to move the depth cut.\n'
    'View FullFogAlpha or FogDepthCut in alpha mode.\n'
    'Analytic reference only: Cycles volume capture is next.')
script['fileName'].setValue((directory / 'm8_volume_reference.gfr').as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
if not loaded['IntervalFrontPoints']['out'].object('/deepPoints').numPoints:
    raise RuntimeError('Reloaded cloud is empty')
report['passed'] = report['max_alpha_error'] <= 1e-6
(directory / 'gaffer_validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
if not report['passed']:
    raise RuntimeError('Gaffer volume cuts exceed 1e-6')
