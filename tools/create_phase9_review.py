# SPDX-License-Identifier: Apache-2.0
"""gaffer env python tools/create_phase9_review.py QUALIFICATION.json OUTPUT.gfr"""
import json
import math
from pathlib import Path
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

qualification = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
assert qualification['passed'] and qualification['phase9_accepted']
out = Path(sys.argv[2]).resolve()
s = Gaffer.ScriptNode()


def add(name, node, x, y):
    s[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


deep = add('ProductionDeep', Gaffer.Switch(), 30, 40)
beauty = add('ProductionBeauty', Gaffer.Switch(), -30, 40)
deep.setup(GafferImage.ImagePlug())
beauty.setup(GafferImage.ImagePlug())
beauty['index'].setInput(deep['index'])
for index, name in enumerate(('run1', 'run2')):
    directory = Path(qualification['runs'][name]['directory'])
    for kind, filename, switch in (('Deep', 'scene.deep.exr', deep),
                                   ('Beauty', 'beauty.exr', beauty)):
        path = directory / filename
        assert path.is_file()
        reader = add(name.title() + kind, GafferImage.ImageReader(),
                     -50 if kind == 'Beauty' else 50, 120 - index * 30)
        reader['fileName'].setValue(path.as_posix())
        switch['in'][index].setInput(reader['out'])
deep['index'].setValue(1)
Gaffer.Metadata.registerValue(deep['index'], 'description',
    '0 = run1, all accepted camera samples; 1 = run2, first 64 per pixel. '
    'Beauty and every deep branch follow this selection.')

flat = add('FullDeepAlpha', GafferImage.DeepToFlat(), 0, 10)
flat['in'].setInput(deep['out'])
alpha = add('AlphaPreview', GafferImage.Shuffle(), -30, -15)
alpha['in'].setInput(flat['out'])
for channel in ('R', 'G', 'B'):
    alpha['shuffles'].addChild(Gaffer.ShufflePlug('A', channel))
alpha['shuffles'].addChild(Gaffer.ShufflePlug('__white', 'A'))
cut = add('DepthCut', GafferImage.DeepSlice(), 35, 10)
cut['in'].setInput(deep['out'])
cut['farClip']['enabled'].setValue(True)
cut['farClip']['value'].setValue(759.322265625)
cut['flatten'].setValue(True)
cut_alpha = add('CutAlphaPreview', GafferImage.Shuffle(), 30, -15)
cut_alpha['in'].setInput(cut['out'])
for channel in ('R', 'G', 'B'):
    cut_alpha['shuffles'].addChild(Gaffer.ShufflePlug('A', channel))
cut_alpha['shuffles'].addChild(Gaffer.ShufflePlug('__white', 'A'))
cloud_cut = add('PointDepthCut', GafferImage.DeepSlice(), 85, 10)
cloud_cut['in'].setInput(deep['out'])
cloud_cut['farClip'].setInput(cut['farClip'])
cloud_cut['flatten'].setValue(False)

settings = json.loads((Path(qualification['runs']['run1']['directory']) / 'render.json').read_text())
camera = add('BlenderCamera', GafferScene.Camera(), 125, 40)
camera['name'].setValue('camera')
camera['perspectiveMode'].setValue(GafferScene.Camera.PerspectiveMode.ApertureFocalLength)
frame = settings['camera_frame']
left, right = min(v[0] / -v[2] for v in frame), max(v[0] / -v[2] for v in frame)
bottom, top = min(v[1] / -v[2] for v in frame), max(v[1] / -v[2] for v in frame)
assert settings['camera_type'] == 'PERSP'
camera['focalLength'].setValue(1)
camera['aperture'].setValue(imath.V2f(right - left, top - bottom))
camera['apertureOffset'].setValue(imath.V2f((right + left) / 2, (top + bottom) / 2))
for name, key in (('translate', 'camera_translation'), ('rotate', 'camera_rotation_degrees'),
                  ('scale', 'camera_scale')):
    camera['transform'][name].setValue(imath.V3f(*settings[key]))
matrix = camera['out'].fullTransform('/camera')
assert max(abs(matrix[c][r] - settings['camera_world_matrix'][r][c])
           for r in range(4) for c in range(4)) < 1e-4
points = add('DeepPoints', CyclesDeep.DeepToPointCloud(), 85, -15)
points['in'].setInput(cloud_cut['out'])
points['camera'].setInput(camera['out'])
points['maxPoints'].setValue(100000)
points['pixelStride'].setValue(32)
points['useImageColor'].setValue(False)
points['color'].setValue(imath.Color3f(.15, .7, 1))
points['pointWidth'].setValue(2)
note = add('ReviewInstructions', Gaffer.Backdrop(), 30, 170)
note['title'].setValue('Phase 9 accepted: full landscape deep alpha')
note['description'].setValue(
    'ProductionDeep index: 0 all samples / 1 first 64. Beauty follows automatically.\n'
    'Select ProductionBeauty, AlphaPreview, CutAlphaPreview or DeepPoints.\n'
    'DepthCut farClip slices both image and points; disable it for the full depth range.\n'
    'Validated cuts: 490.448, 624.885, 759.322, 893.759, 1028.196.\n'
    'Points come from the actual production deep EXRs and matching Blender camera.\n'
    'Preview: every 32nd pixel, at most 100,000 points; full deep output is unchanged.\n'
    'Z shows interval fronts; DeepPoints depthChannel=ZBack shows backs.\n'
    'Both runs: OptiX 1175x500, original max1024 adaptive, GPU OIDN, IDs, error1e-3.\n'
    'Native analytic VDB output. All qualification gates pass, with the recorded\n'
    'snapshot diagnostic resolving run1 (992,78); awaiting final visual review.')
s['fileName'].setValue(out.as_posix())
s.setFocus(beauty)
s.selection().add(beauty)
s.save()
with out.open('a') as stream:
    stream.write('\nparent.selection().clear()\nparent.selection().add(parent["ProductionBeauty"])\n'
                 'parent.setFocus(parent["ProductionBeauty"])\n')

# Check the saved graph itself, including both real deep inputs and bounded clouds.
t = Gaffer.ScriptNode()
t['fileName'].setValue(out.as_posix())
t.load()
checks = []
for index, name in enumerate(('run1', 'run2')):
    t['ProductionDeep']['index'].setValue(index)
    assert t['ProductionBeauty']['index'].getValue() == index
    assert t['ProductionDeep']['out']['deep'].getValue()
    fmt = t['ProductionDeep']['out']['format'].getValue()
    assert (fmt.width(), fmt.height()) == (1175, 500)
    assert not t['FullDeepAlpha']['out']['deep'].getValue()
    assert not t['DepthCut']['out']['deep'].getValue()
    assert t['PointDepthCut']['out']['deep'].getValue()
    p = t['DeepPoints']['out'].object('/deepPoints')
    assert 0 < p.numPoints <= 100000 and p.arePrimitiveVariablesValid()
    assert all(math.isfinite(v) for point in p['P'].data for v in point)
    checks.append(dict(run=name, points=p.numPoints, deep_path=t[name.title()+'Deep']['fileName'].getValue(),
                       camera_transform_checked=True, connected=True))
report = dict(passed=True, path=str(out), checks=checks, point_stride=32, max_points=100000,
              initial_focus='ProductionBeauty', initial_run='run2', qualification=sys.argv[1])
out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
