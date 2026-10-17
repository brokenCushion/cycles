"""gaffer env python SCRIPT JUNKSHOP_OUTPUT (render.json from the smoke run).

The documented legacy Blender review requires strict surface CSVs. This review
uses the same reader/slice/point-cloud nodes for numeric surface/volume output,
and adds a DeepMerge for each frame. It checks reader interoperability, not
the accuracy of procedural-volume integration against a finer-step reference.
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

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/deep'))
from validate_gaffer import check, deep_error, deep_pixel, flat_alpha
from compare_deep_ids import flattened_transmittance

root = Path(sys.argv[1]).resolve()
settings = json.loads((root / 'render.json').read_text())
check(settings['passed'], 'Production render did not pass')
script = Gaffer.ScriptNode()
reports = {}


def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


for frame in (1, 2):
    merge = add(f'Frame{frame}DeepMerge', GafferImage.DeepMerge(), frame * 150, -80)
    flat = add(f'Frame{frame}MergedAlpha', GafferImage.DeepToFlat(), frame * 150, -105)
    flat['in'].setInput(merge['out'])
    camera = add(f'Frame{frame}Camera', GafferScene.Camera(), frame * 150 + 95, 70)
    camera['name'].setValue('camera')
    camera['perspectiveMode'].setValue(GafferScene.Camera.PerspectiveMode.ApertureFocalLength)
    info = settings['frames'][str(frame)]
    check(info['camera_type'] == 'PERSP', 'Expected the production perspective camera')
    corners = info['camera_frame']
    left, right = min(v[0] / -v[2] for v in corners), max(v[0] / -v[2] for v in corners)
    bottom, top = min(v[1] / -v[2] for v in corners), max(v[1] / -v[2] for v in corners)
    camera['focalLength'].setValue(1)
    camera['aperture'].setValue(imath.V2f(right - left, top - bottom))
    camera['apertureOffset'].setValue(imath.V2f((right + left) / 2, (top + bottom) / 2))
    camera['transform']['translate'].setValue(imath.V3f(*info['camera_translation']))
    camera['transform']['rotate'].setValue(imath.V3f(*info['camera_rotation_degrees']))
    for index, (filename, record) in enumerate((f, r) for f, r in settings['files'].items()
                                               if r['frame'] == frame):
        name = f'{record["layer"]}{frame}'
        x = frame * 150 + index * 35
        reader = add(name + 'Deep', GafferImage.ImageReader(), x, 70)
        reader['fileName'].setValue((root / filename).as_posix())
        check(reader['out']['deep'].getValue(), 'Expected deep EXR: ' + filename)
        merge['in'][index].setInput(reader['out'])
        fmt = reader['out']['format'].getValue()
        width, height = fmt.width(), fmt.height()
        tolerance = deep_error(reader['out'])
        flat = add(name + 'Alpha', GafferImage.DeepToFlat(), x, 40)
        flat['in'].setInput(reader['out'])
        cut = add(name + 'DepthCut', GafferImage.DeepSlice(), x, 10)
        cut['in'].setInput(reader['out'])
        cut['nearClip']['enabled'].setValue(False)
        cut['farClip']['enabled'].setValue(True)
        cut['flatten'].setValue(True)
        pixels = {}
        flatten_error = 0
        for y in range(0, height, max(1, height // 8)):
            for px in range(0, width, max(1, width // 8)):
                point = imath.V2i(px, y)
                samples = deep_pixel(reader['out'], point)
                check(all(math.isfinite(z) and math.isfinite(back) and math.isfinite(a) and
                          0 < z <= back and 0 < a <= 1 for z, back, a in samples),
                      'Invalid Z/ZBack/A in ' + filename)
                pixels[px, y] = samples
                flatten_error = max(flatten_error, abs(flat_alpha(flat['out'], point) -
                                                      (1 - flattened_transmittance(samples))))
        depths = [z for samples in pixels.values() for z, _, _ in samples]
        check(depths, 'No occupied review pixels: ' + filename)
        low, high = min(depths), max(depths)
        slice_error = 0
        for fraction in (.1, .3, .5, .7, .9):
            cut['farClip']['value'].setValue(low + (high - low) * fraction)
            depth = cut['farClip']['value'].getValue()
            for (px, y), samples in pixels.items():
                transmittance = 1.
                for front, back, alpha in samples:
                    if front == back:
                        transmittance *= 1 - alpha if front < depth else 1
                    else:
                        portion = min(1, max(0, (depth - front) / (back - front)))
                        transmittance *= (1 - alpha) ** portion
                slice_error = max(slice_error, abs(flat_alpha(cut['out'], imath.V2i(px, y)) -
                                                    (1 - transmittance)))
        check(max(flatten_error, slice_error) <= tolerance, 'Reader alpha/depth-cut mismatch')
        cut['flatten'].setValue(False)
        cut['farClip']['enabled'].setValue(False)
        points = add(name + 'DeepPoints', CyclesDeep.DeepToPointCloud(), x, -20)
        points['in'].setInput(cut['out'])
        points['camera'].setInput(camera['out'])
        points['maxPoints'].setValue(100000)
        points['useImageColor'].setValue(False)
        points['color'].setValue(imath.Color3f(.15, .7, 1))
        cloud = points['out'].object('/deepPoints')
        check(cloud.numPoints > 0 and cloud.arePrimitiveVariablesValid(), 'Invalid deep point cloud')
        reports[filename] = dict(passed=True, resolution=[width, height],
                                 grid_pixels=len(pixels), max_flatten_error=flatten_error,
                                 max_slice_error=slice_error, displayed_points=cloud.numPoints)
    # DeepMerge must contain the samples from all three layer readers.
    for y in range(0, height, max(1, height // 8)):
        for x in range(0, width, max(1, width // 8)):
            point = imath.V2i(x, y)
            samples = [sample for i in range(3) for sample in deep_pixel(merge['in'][i], point)]
            actual = flat_alpha(script[f'Frame{frame}MergedAlpha']['out'], point)
            check(abs(actual - (1 - flattened_transmittance(samples))) <= tolerance,
                  'DeepMerge alpha mismatch')

review = root / 'blender_deep_multilayer_review.gfr'
script['fileName'].setValue(review.as_posix())
script.save()
loaded = Gaffer.ScriptNode()
loaded['fileName'].setValue(review.as_posix())
loaded.load()
check(loaded['Frame1DeepMerge']['out']['deep'].getValue(), 'Reloaded review is not deep')
(root / 'gaffer_validation.json').write_text(json.dumps(dict(passed=True, files=reports,
    deep_merge_frames=[1, 2], review=str(review), nuke_tested=False), indent=2))
print('MULTILAYER_GAFFER_REVIEW_PASS', len(reports), 'files; DeepMerge frames 1-2', flush=True)
