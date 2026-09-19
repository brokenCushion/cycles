# SPDX-License-Identifier: Apache-2.0
"""Validate a native Blender deep render and prepare its live Gaffer review.

gaffer env python SCRIPT RENDER_DIRECTORY
Requires render.json, beauty.exr, scene.deep.exr and its raw .samples.csv grid.
"""
import csv
import json
import math
from pathlib import Path
import struct
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_gaffer import check, deep_pixel, tile_index

directory = Path(sys.argv[1]).resolve()
settings = json.loads((directory/'render.json').read_text())
check(settings['deep'], 'Expected a native Blender deep render')
script = Gaffer.ScriptNode()

def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x,y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node

reader = add('BlenderDeep', GafferImage.ImageReader(), 0, 40)
reader['fileName'].setValue((directory/'scene.deep.exr').as_posix())
check(reader['out']['deep'].getValue(), 'Output is not deep')
fmt = reader['out']['format'].getValue()
width, height = fmt.width(), fmt.height()
check([width,height] == [v*settings['percentage']//100 for v in settings['resolution']],
      'Deep output resolution mismatch')
raw = {}
for row in csv.DictReader((directory/'scene.deep.exr.samples.csv').open(newline='')):
    x,y,s,event = (int(row[k]) for k in ('file_x','file_y','sample','event'))
    key = (x,y,s)
    z,a = (struct.unpack('f',struct.pack('f',float(row[k])))[0] for k in ('depth','alpha'))
    if event == -1:
        check(key not in raw and z==a==0, 'Invalid/duplicate camera miss')
        raw[key] = []
    else:
        events = raw.setdefault(key,[])
        check(event == len(events) and math.isfinite(z) and z>0 and 0<=a<=1,
              'Invalid raw camera event')
        events.append((z,a))
expected_grid = {(x,y) for y in range(height) for x in range(width)
                 if (x%max(1,width//8)==0 or x==width-1) and
                    (y%max(1,height//8)==0 or y==height-1)}
check({(x,y) for x,y,s in raw} == expected_grid, 'Incomplete diagnostic grid')
maximum, checked, depths = 0, 0, []
pixels = {}
for x,y in sorted(expected_grid):
    ids = sorted(s for px,py,s in raw if (px,py)==(x,y))
    check(ids == list(range(len(ids))) and 0<len(ids)<=settings['samples'],
          'Incomplete camera population')
    cameras = [raw[x,y,s] for s in ids]
    actual = deep_pixel(reader['out'],fmt.fromEXRSpace(imath.V2i(x,y)))
    check(all(math.isfinite(z) and z>0 and z==back and 0<a<=1 for z,back,a in actual),
          'Invalid deep surface sample')
    check([v[0] for v in actual] == sorted(v[0] for v in actual), 'Unsorted deep depths')
    boundaries = sorted({z for events in cameras for z,a in events} | {z for z,b,a in actual})
    depths.extend(boundaries)
    for z in boundaries:
        for inclusive in (False,True):
            expected = sum(math.prod(1-a for d,a in events
                if d<z or (inclusive and d==z)) for events in cameras)/len(cameras)
            observed = math.prod(1-a for d,b,a in actual if d<z or (inclusive and d==z))
            maximum = max(maximum,abs(expected-observed))
            checked += 1
    pixels[x,y] = cameras
check(maximum<=1e-6 and checked>0, 'Deep reconstruction exceeds curve tolerance')

cut = add('SceneDepthCut', GafferImage.DeepSlice(), 0, 20)
cut['in'].setInput(reader['out'])
cut['nearClip']['enabled'].setValue(False)
cut['farClip']['enabled'].setValue(True)
cut['flatten'].setValue(True)
depths.sort()
cuts = [depths[int((len(depths)-1)*q)] for q in (.1,.25,.5,.75,.9)] + [depths[-1]+1]
slice_error = 0
for z in cuts:
    cut['farClip']['value'].setValue(z)
    z = cut['farClip']['value'].getValue()
    tiles = {}
    for (x,y),cameras in pixels.items():
        point = fmt.fromEXRSpace(imath.V2i(x,y))
        origin,index = tile_index(point)
        key = (origin.x,origin.y)
        if key not in tiles:
            tiles[key] = cut['out'].channelData('A',origin)
        expected = 1-sum(math.prod(1-a for d,a in events if d<z)
                         for events in cameras)/len(cameras)
        slice_error = max(slice_error,abs(float(tiles[key][index])-expected))
check(slice_error<=1e-6, 'Gaffer DeepSlice exceeds analytic raw-curve tolerance')
cut['farClip']['value'].setValue(cuts[-1])
cut['farClip']['enabled'].setValue(False)
cloud_cut = add('CloudDepthCut', GafferImage.DeepSlice(), 30, 20)
cloud_cut['in'].setInput(reader['out'])
cloud_cut['nearClip']['enabled'].setValue(False)
cloud_cut['farClip'].setInput(cut['farClip'])
cloud_cut['flatten'].setValue(False)
camera = add('BlenderCamera', GafferScene.Camera(), 60, 40)
camera['name'].setValue('camera')
camera['perspectiveMode'].setValue(GafferScene.Camera.PerspectiveMode.ApertureFocalLength)
frame = settings['camera_frame']
left,right = min(v[0]/-v[2] for v in frame),max(v[0]/-v[2] for v in frame)
bottom,top = min(v[1]/-v[2] for v in frame),max(v[1]/-v[2] for v in frame)
camera['focalLength'].setValue(1)
check(settings['camera_type'] in ('PERSP', 'ORTHO'), 'Unsupported Blender camera projection')
if settings['camera_type'] == 'ORTHO':
    camera['projection'].setValue('orthographic')
    left,right = min(v[0] for v in frame),max(v[0] for v in frame)
    bottom,top = min(v[1] for v in frame),max(v[1] for v in frame)
    camera['orthographicAperture'].setValue(imath.V2f(right-left,top-bottom))
else:
    camera['aperture'].setValue(imath.V2f(right-left,top-bottom))
camera['apertureOffset'].setValue(imath.V2f((right+left)/2,(top+bottom)/2))
for name,value in (('translate','camera_translation'),('rotate','camera_rotation_degrees'),
                   ('scale','camera_scale')):
    camera['transform'][name].setValue(imath.V3f(*settings[value]))
matrix = camera['out'].fullTransform('/camera')
check(max(abs(matrix[c][r]-settings['camera_world_matrix'][r][c])
          for r in range(4) for c in range(4))<1e-4, 'Camera transform mismatch')
cloud = add('SceneDeepPoints', CyclesDeep.DeepToPointCloud(), 30, 0)
cloud['in'].setInput(cloud_cut['out'])
cloud['camera'].setInput(camera['out'])
cloud['maxPoints'].setValue(1000000)
cloud['useImageColor'].setValue(False)
cloud['color'].setValue(imath.Color3f(.15,.7,1))
points = cloud['out'].object('/deepPoints')
check(points.numPoints>0 and points.arePrimitiveVariablesValid(), 'Invalid deep point cloud')
beauty = add('BlenderBeauty', GafferImage.ImageReader(), -35, 40)
beauty['fileName'].setValue((directory/'beauty.exr').as_posix())
flat = add('FullDeepAlpha', GafferImage.DeepToFlat(), -20, 20)
flat['in'].setInput(reader['out'])
note = add('ReviewInstructions', Gaffer.Backdrop(), -25, 70)
note['title'].setValue('Native Blender scene: deep camera visibility')
note['description'].setValue('View SceneDeepPoints for points read from the rendered deep EXR.\n'
    'Enable SceneDepthCut farClip and adjust its value to slice the scene.\n'
    'BlenderBeauty retains native materials; deep stores Z/ZBack/A only.\n'
    'With DOF these points show camera-relative depth samples, not original lens-ray hit positions.\n'
    'No mesh-to-point conversion. Display is limited to 1,000,000 distributed samples.')
script['fileName'].setValue((directory/'blender_deep_review.gfr').as_posix())
script.save()
# Focus is session state, not part of normal node serialisation. Restore it
# explicitly so opening this review sends the actual deep cloud to the Viewer.
with Path(script['fileName'].getValue()).open('a') as review_file:
    review_file.write('\nparent.selection().add(parent["SceneDeepPoints"])\n'
                      'parent.setFocus(parent["SceneDeepPoints"])\n')
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(script['fileName'].getValue())
loaded.load()
check(loaded.getFocus().isSame(loaded['SceneDeepPoints']), 'Review did not restore cloud focus')
check(loaded['SceneDeepPoints']['out'].object('/deepPoints').numPoints==points.numPoints,
      'Reloaded point cloud differs')
report = {'passed':True, 'scope':'native Blender deep visibility, diagnostic camera grid',
          'resolution':[width,height], 'grid_pixels':len(pixels), 'boundary_checks':checked,
          'max_curve_error':maximum, 'max_slice_error':slice_error,
          'displayed_points':points.numPoints, 'source_sha256':settings['source_sha256']}
(directory/'gaffer_validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
