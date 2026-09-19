# SPDX-License-Identifier: Apache-2.0
"""Gaffer reader/slice/beauty checks and review for a native Blender VDB render.

gaffer env python SCRIPT DEEP_RENDER_DIRECTORY BEAUTY_OFF_DIRECTORY
This checks EXR interoperability, not an independent physical grid integral.
"""
import json
import math
from pathlib import Path
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_gaffer import check, deep_pixel, tile_index

directory, baseline = (Path(p).resolve() for p in sys.argv[1:3])
settings = json.loads((directory / 'render.json').read_text())
check(settings['deep_volume'] and settings['samples'] == 1, 'Expected native volume fixture')
script = Gaffer.ScriptNode()


def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


reader = add('NativeVDBDeep', GafferImage.ImageReader(), 0, 40)
reader['fileName'].setValue((directory / 'scene.deep.exr').as_posix())
check(reader['out']['deep'].getValue(), 'Output is not deep')
fmt = reader['out']['format'].getValue()
width, height = fmt.width(), fmt.height()
check([width, height] == [v * settings['percentage'] // 100 for v in settings['resolution']],
      'Unexpected image size')
beauty = add('VDBBeauty', GafferImage.ImageReader(), -35, 40)
beauty['fileName'].setValue((directory / 'beauty.exr').as_posix())
off = GafferImage.ImageReader()
off['fileName'].setValue((baseline / 'beauty.exr').as_posix())
check(off['out']['format'].getValue() == fmt, 'Beauty baseline format mismatch')
tile = GafferImage.ImagePlug.tileSize()
beauty_error = 0.0
for y in range(0, height, tile):
    for x in range(0, width, tile):
        for channel in ('R', 'G', 'B', 'A'):
            a = beauty['out'].channelData(channel, imath.V2i(x, y))
            b = off['out'].channelData(channel, imath.V2i(x, y))
            for j in range(min(tile, height-y)):
                for i in range(min(tile, width-x)):
                    av, bv = float(a[j*tile+i]), float(b[j*tile+i])
                    check(math.isfinite(av) and math.isfinite(bv), 'Nonfinite beauty')
                    beauty_error = max(beauty_error, abs(av-bv))
check(beauty_error == 0, 'Deep capture changed beauty')

pixels = {}
low, high = math.inf, 0
for y in sorted(set([i*(height-1)//8 for i in range(9)])):
    for x in sorted(set([i*(width-1)//8 for i in range(9)])):
        samples = deep_pixel(reader['out'], imath.V2i(x, y))
        previous = 0
        for front, back, alpha in samples:
            check(all(math.isfinite(v) for v in (front, back, alpha)) and
                  front > 0 and back >= front and front >= previous and 0 < alpha <= 1,
                  'Invalid or unordered deep interval')
            check(back == front or alpha < 1, 'Opaque extended interval')
            previous = back
            low, high = min(low, front), max(high, back)
        pixels[x, y] = samples
check(high > low, 'No sampled VDB volume intervals')


def transmittance(samples, depth):
    tau, surface = 0.0, 1.0
    for front, back, alpha in samples:
        if front == back:
            if front < depth:
                surface *= 1-alpha
        else:
            fraction = min(1, max(0, (depth-front)/(back-front)))
            tau -= math.log1p(-alpha)*fraction
    return surface*math.exp(-tau)


flat = add('FullDeepAlpha', GafferImage.DeepToFlat(), -15, 20)
flat['in'].setInput(reader['out'])
cut = add('VolumeDepthCut', GafferImage.DeepSlice(), 15, 20)
cut['in'].setInput(reader['out'])
cut['nearClip']['enabled'].setValue(False)
cut['farClip']['enabled'].setValue(True)
cut['flatten'].setValue(True)
slice_error = 0.0
cuts = [low+(high-low)*v for v in (.1, .3, .5, .7, .9)]
for depth in cuts:
    cut['farClip']['value'].setValue(depth)
    actual_depth = cut['farClip']['value'].getValue()
    for (x, y), samples in pixels.items():
        origin, index = tile_index(imath.V2i(x, y))
        alpha = float(cut['out'].channelData('A', origin)[index])
        slice_error = max(slice_error, abs(alpha - (1-transmittance(samples, actual_depth))))
check(slice_error <= 1e-6, 'Gaffer depth cuts differ from stored exponential curve')
cut['farClip']['enabled'].setValue(False)
cloud_cut = add('CloudDepthCut', GafferImage.DeepSlice(), 40, 20)
cloud_cut['in'].setInput(reader['out'])
cloud_cut['nearClip']['enabled'].setValue(False)
cloud_cut['farClip'].setInput(cut['farClip'])
cloud_cut['flatten'].setValue(False)
camera = add('VDBCamera', GafferScene.Camera(), 65, 40)
camera['name'].setValue('camera')
camera['perspectiveMode'].setValue(GafferScene.Camera.PerspectiveMode.ApertureFocalLength)
frame = settings['camera_frame']
left, right = min(v[0]/-v[2] for v in frame), max(v[0]/-v[2] for v in frame)
bottom, top = min(v[1]/-v[2] for v in frame), max(v[1]/-v[2] for v in frame)
camera['focalLength'].setValue(1)
camera['aperture'].setValue(imath.V2f(right-left, top-bottom))
camera['apertureOffset'].setValue(imath.V2f((right+left)/2, (top+bottom)/2))
for name, key in (('translate', 'camera_translation'), ('rotate', 'camera_rotation_degrees'),
                  ('scale', 'camera_scale')):
    camera['transform'][name].setValue(imath.V3f(*settings[key]))
cloud = add('VDBDeepPoints', CyclesDeep.DeepToPointCloud(), 40, 0)
cloud['in'].setInput(cloud_cut['out'])
cloud['camera'].setInput(camera['out'])
cloud['maxPoints'].setValue(1000000)
cloud['useImageColor'].setValue(False)
cloud['color'].setValue(imath.Color3f(.3, .7, 1))
points = cloud['out'].object('/deepPoints')
check(points.numPoints > 0 and points.arePrimitiveVariablesValid(), 'Invalid VDB cloud')
note = add('ReviewInstructions', Gaffer.Backdrop(), -15, 70)
note['title'].setValue('Native VDB deep visibility')
note['description'].setValue('Actual firePlume density grid rendered by custom Cycles.\n'
    'Select VDBDeepPoints to view points read from the deep EXR.\n'
    'Enable VolumeDepthCut farClip to slice the stored volume.\n'
    'Scalar absorption Z/ZBack/A; emission/scattering are not reconstructed.\n'
    '256x256, one camera sample; point display limited to 1,000,000 samples.')
review = directory / 'native_vdb_review.gfr'
script['fileName'].setValue(review.as_posix())
script.save()
with review.open('a') as stream:
    stream.write('\nparent.selection().add(parent["VDBDeepPoints"])\n'
                 'parent.setFocus(parent["VDBDeepPoints"])\n')
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(review.as_posix())
loaded.load()
check(loaded.getFocus().isSame(loaded['VDBDeepPoints']), 'Review focus did not reload')
report = {'passed': True, 'scope': 'Gaffer EXR interoperability and beauty isolation',
          'resolution': [width, height], 'diagnostic_pixels': len(pixels),
          'max_beauty_error': beauty_error, 'max_slice_error': slice_error,
          'depth_cuts': cuts, 'depth_range': [low, high], 'displayed_points': points.numPoints,
          'physical_grid_oracle': 'Separate CPU/CUDA grid qualification; not this reader check'}
(directory / 'gaffer_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
