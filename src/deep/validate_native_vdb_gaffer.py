# SPDX-License-Identifier: Apache-2.0
"""Gaffer reader/slice/beauty checks and review for a native Blender VDB render.

gaffer env python SCRIPT DEEP_RENDER_DIRECTORY BEAUTY_OFF_DIRECTORY [--reader-only]
  [--beauty-repeat INDEPENDENT_CUDA_BEAUTY_DIRECTORY]
This checks EXR interoperability, not an independent physical grid integral.
Reader-only permits historical outputs without accepted-camera diagnostics.
"""
import argparse
import json
import math
import bisect
import csv
from pathlib import Path
import sys

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_gaffer import check, deep_pixel, tile_index

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('baseline', type=Path)
parser.add_argument('--reader-only', action='store_true')
parser.add_argument('--expect-empty', action='store_true',
                    help='Require zero samples at every pixel for a zero-extinction fixture')
parser.add_argument('--overlap-reference', nargs=2, type=Path,
                    help='Single-grid renders with matching cameras; check combined extinction')
parser.add_argument('--beauty-repeat', type=Path,
                    help='Independent CUDA beauty repeat; fixed absolute regression gate of samples * 2^-23')
args = parser.parse_args()
directory, baseline = args.directory.resolve(), args.baseline.resolve()
settings = json.loads((directory / 'render.json').read_text())
reference = json.loads((baseline / 'render.json').read_text())
check(settings['deep_volume'] and settings['samples'] >= 1, 'Expected native volume fixture')
check(not reference['deep'], 'Beauty baseline must disable deep capture')
for key in ('source_sha256', 'samples', 'resolution', 'percentage', 'device',
            'camera_world_matrix', 'camera_frame', 'frame', 'adaptive', 'denoising'):
    check(settings[key] == reference[key], 'Beauty baseline differs: ' + key)
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
repeat = None
beauty_tolerance = 0.0
if args.beauty_repeat:
    check(settings['device'] == 'CUDA' and settings['samples'] > 1,
          'Repeat tolerance is only for multisample CUDA fixtures')
    repeat_directory = args.beauty_repeat.resolve()
    check(repeat_directory != baseline and repeat_directory != directory,
          'Beauty repeat must be an independent render directory')
    repeated_settings = json.loads((repeat_directory / 'render.json').read_text())
    for key in ('source_sha256', 'samples', 'resolution', 'percentage', 'device',
                'camera_world_matrix', 'camera_frame', 'frame', 'adaptive', 'denoising', 'deep'):
        check(repeated_settings[key] == reference[key], 'Beauty repeat differs: ' + key)
    repeat = GafferImage.ImageReader()
    repeat['fileName'].setValue((repeat_directory / 'beauty.exr').as_posix())
    check(repeat['out']['format'].getValue() == fmt, 'Beauty repeat format mismatch')
    # Fixed absolute regression gate, including HDR. Do not scale the tolerance
    # with brightness or the observed repeat error. This is a fixture acceptance
    # criterion, not a universal floating-point bound for arbitrary HDR renders.
    beauty_tolerance = settings['samples'] * 2**-23
tile = GafferImage.ImagePlug.tileSize()
total_deep_samples, max_pixel_samples = 0, 0
for y in range(0, height, tile):
    for x in range(0, width, tile):
        offsets = reader['out'].sampleOffsets(imath.V2i(x, y))
        previous = 0
        for offset in offsets:
            check(offset >= previous, 'Invalid deep sample offsets')
            max_pixel_samples = max(max_pixel_samples, offset-previous)
            previous = offset
        total_deep_samples += previous
beauty_error = 0.0
repeat_error = 0.0
beauty_peak = 0.0
for y in range(0, height, tile):
    for x in range(0, width, tile):
        for channel in ('R', 'G', 'B', 'A'):
            a = beauty['out'].channelData(channel, imath.V2i(x, y))
            b = off['out'].channelData(channel, imath.V2i(x, y))
            c = repeat['out'].channelData(channel, imath.V2i(x, y)) if repeat is not None else None
            for j in range(min(tile, height-y)):
                for i in range(min(tile, width-x)):
                    av, bv = float(a[j*tile+i]), float(b[j*tile+i])
                    check(math.isfinite(av) and math.isfinite(bv), 'Nonfinite beauty')
                    beauty_error = max(beauty_error, abs(av-bv))
                    beauty_peak = max(beauty_peak, abs(av), abs(bv))
                    if c is not None:
                        cv = float(c[j*tile+i])
                        check(math.isfinite(cv), 'Nonfinite beauty repeat')
                        beauty_peak = max(beauty_peak, abs(cv))
                        repeat_error = max(repeat_error, abs(bv-cv))
beauty_report = {'max_deep_on_off': beauty_error, 'max_ordinary_repeat': repeat_error,
                 'peak_absolute_value': beauty_peak, 'comparison': 'fixed absolute error',
                 'tolerance': beauty_tolerance}
(directory / 'beauty_validation.json').write_text(json.dumps(beauty_report, indent=2)+'\n')
check(beauty_error <= beauty_tolerance and repeat_error <= beauty_tolerance,
      'Beauty isolation failed: ' + str(beauty_report))

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
if args.expect_empty:
    check(total_deep_samples == 0, 'Zero-extinction fixture contains deep samples')
    low, high = 0, 1
else:
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


def curve(intervals, surfaces):
    """Evaluate the stored optical-depth integral, independent of C++ fitting."""
    intervals = sorted(intervals)
    ordered = all(a[1] <= b[0] for a, b in zip(intervals, intervals[1:]))
    ends = [v[1] for v in intervals]
    prefix = [0.0]
    for _, _, tau in intervals:
        prefix.append(prefix[-1] + tau)
    if not ordered:
        # Independent sweep integral for overlaps. Recompute the active rate
        # with fsum; do not share the renderer's ordered-run cache or fitting.
        events = {}
        for i, (front, back, value) in enumerate(intervals):
            events.setdefault(front, []).append((i, value/(back-front)))
            events.setdefault(back, []).append((i, None))
        depths = sorted(events)
        optical_depth, rates, active = [], [], {}
        tau, correction, previous, rate = 0.0, 0.0, depths[0], 0.0
        for depth in depths:
            increment = (depth-previous)*rate - correction
            updated = tau + increment
            correction = (updated-tau) - increment
            tau = updated
            for i, value in events[depth]:
                if value is None:
                    del active[i]
                else:
                    active[i] = value
            rate = math.fsum(active.values())
            optical_depth.append(tau)
            rates.append(rate)
            previous = depth

    def at(z, before=False):
        if ordered:
            i = bisect.bisect_right(ends, z)
            tau = prefix[i]
            if i < len(intervals):
                front, back, value = intervals[i]
                tau += value * max(0, (z-front)/(back-front))
        else:
            i = bisect.bisect_right(depths, z)-1
            tau = 0.0 if i < 0 else optical_depth[i] + rates[i]*(z-depths[i])
        t = math.exp(-tau)
        for depth, alpha in surfaces:
            if depth < z or (depth == z and not before):
                t *= 1-alpha
        return t
    return at


def read_cameras(path):
    cameras, identities = {}, set()
    with path.open() as stream:
        for row in csv.DictReader(stream):
            key = tuple(int(row[k]) for k in ('file_x', 'file_y', 'sample'))
            identity = key + (int(row['event']),)
            check(identity not in identities, 'Duplicate volume diagnostic record')
            identities.add(identity)
            check(0 <= key[0] < width and 0 <= key[1] < height and
                  0 <= key[2] < settings['samples'], 'Invalid diagnostic identity')
            intervals, surfaces = cameras.setdefault(key, ([], []))
            a, b, v = (float(row[k]) for k in ('front', 'back', 'value'))
            check(all(math.isfinite(n) for n in (a, b, v)), 'Nonfinite diagnostic value')
            if row['kind'] == 'volume':
                check(0 < a < b and v >= 0, 'Invalid diagnostic volume interval')
                intervals.append((a, b, v))
            elif row['kind'] == 'surface':
                check(0 < a == b and 0 <= v <= 1, 'Invalid diagnostic surface')
                surfaces.append((a, v))
            else:
                check(row['kind'] == 'miss' and identity[-1] == -1 and a == b == v == 0,
                      'Invalid diagnostic miss')
    expected_pixels = {(i*(width-1)//8, j*(height-1)//8) for i in range(9) for j in range(9)}
    check(set(cameras) == {(x, y, s) for x, y in expected_pixels
                          for s in range(settings['samples'])}, 'Missing diagnostic cameras')
    return cameras


diagnostic = directory / 'scene.deep.exr.samples.csv'
check(diagnostic.exists() or settings['samples'] == 1 or args.reader_only,
      'Multi-sample qualification requires accepted-camera diagnostics')
check(not args.overlap_reference or diagnostic.exists(), 'Overlap check requires diagnostics')
raw_error, raw_probes, raw_pixels = 0.0, 0, 0
if diagnostic.exists():
    cameras = read_cameras(diagnostic)
    if args.expect_empty:
        check(all(not v and not s for v, s in cameras.values()),
              'Zero-extinction camera captured opacity')
    references = []
    for path in args.overlap_reference or []:
        other = json.loads((path / 'render.json').read_text())
        for key in ('samples', 'resolution', 'percentage', 'device', 'camera_world_matrix',
                    'camera_frame', 'frame', 'adaptive', 'dof', 'pixel_filter', 'pixel_aspect'):
            check(settings[key] == other[key], 'Overlap reference differs: ' + key)
        reference_cameras = read_cameras(path / 'scene.deep.exr.samples.csv')
        check(all(not surfaces for _, surfaces in reference_cameras.values()),
              'Overlap references must contain only volumes')
        references.append(reference_cameras)
    if references:
        overlap_error, overlap_probes = 0.0, 0
        worst_overlap = None
        for key, (intervals, surfaces) in cameras.items():
            check(not surfaces, 'Overlap fixture must contain only volumes')
            sources = [ref[key][0] for ref in references]
            actual_curve = curve(intervals, [])
            reference_curves = [curve(v, []) for v in sources]
            boundaries = {z for v in [intervals] + sources for a, b, _ in v for z in (a, b)}
            ordered = sorted(boundaries)
            probes = boundaries | {(a+b)/2 for a, b in zip(ordered, ordered[1:])}
            for z in probes:
                expected = math.prod(f(z) for f in reference_curves)
                actual_value = actual_curve(z)
                error = abs(actual_value-expected)
                if error > overlap_error:
                    overlap_error = error
                    worst_overlap = {'camera': list(key), 'depth': z,
                                     'actual': actual_value, 'expected': expected,
                                     'intervals': [len(intervals)] + [len(v) for v in sources]}
                overlap_probes += 1
        overlap_report = {'passed': overlap_error <= 1e-6, 'max_error': overlap_error,
                          'probes': overlap_probes,
                          'worst': worst_overlap,
                          'references': [str(p.resolve()) for p in args.overlap_reference],
                          'scope': 'Separate single-grid capture product at boundaries and midpoints'}
        (directory / 'overlap_validation.json').write_text(json.dumps(overlap_report, indent=2)+'\n')
        check(overlap_report['passed'], 'Combined extinction differs from single-grid product: ' + str(overlap_report))
    expected_pixels = {(i*(width-1)//8, j*(height-1)//8) for i in range(9) for j in range(9)}
    for x, file_y in sorted(expected_pixels):
        accepted = [cameras[x, file_y, s] for s in range(settings['samples'])]
        functions = [curve(v, s) for v, s in accepted]
        output = deep_pixel(reader['out'], imath.V2i(x, height-1-file_y))
        actual = curve([(a, b, -math.log1p(-v)) for a, b, v in output if a < b],
                       [(a, v) for a, b, v in output if a == b])
        boundaries = {z for v, s in accepted for a, b, _ in v for z in (a, b)}
        boundaries.update(z for v, s in accepted for z, _ in s)
        boundaries.update(z for a, b, _ in output for z in (a, b))
        ordered = sorted(boundaries)
        probes = boundaries | {(a+b)/2 for a, b in zip(ordered, ordered[1:])}
        for z in probes:
            for before in (True, False):
                expected = math.fsum(f(z, before) for f in functions) / len(functions)
                raw_error = max(raw_error, abs(expected-actual(z, before)))
                raw_probes += 1
        raw_pixels += 1
    check(raw_error <= 1e-6, 'EXR differs from accepted camera transmittance')


flat = add('FullDeepAlpha', GafferImage.DeepToFlat(), -15, 20)
flat['in'].setInput(reader['out'])
cut = add('VolumeDepthCut', GafferImage.DeepSlice(), 15, 20)
cut['in'].setInput(reader['out'])
cut['nearClip']['enabled'].setValue(False)
cut['farClip']['enabled'].setValue(True)
cut['flatten'].setValue(True)
slice_error = 0.0
worst_slice = None
cuts = [low+(high-low)*v for v in (.1, .3, .5, .7, .9)]
for depth in cuts:
    cut['farClip']['value'].setValue(depth)
    actual_depth = cut['farClip']['value'].getValue()
    for (x, y), samples in pixels.items():
        origin, index = tile_index(imath.V2i(x, y))
        alpha = float(cut['out'].channelData('A', origin)[index])
        expected = 1-transmittance(samples, actual_depth)
        error = abs(alpha-expected)
        if error > slice_error:
            slice_error = error
            worst_slice = {'pixel': [x,y], 'depth': actual_depth, 'intervals': len(samples),
                           'actual': alpha, 'expected': expected, 'error': error}
(directory / 'curve_validation.json').write_text(json.dumps({
    'accepted_camera_probes': raw_probes, 'max_accepted_camera_error': raw_error,
    'max_slice_error': slice_error, 'worst_slice': worst_slice}, indent=2)+'\n')
# Save a review even when compositor precision fails the gate. The final report
# and exit status remain failing; a viewable graph is not numerical sign-off.
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
preview_stride = 4 if width * height > 256 * 256 else 1
cloud['pixelStride'].setValue(preview_stride)
cloud['useImageColor'].setValue(False)
cloud['color'].setValue(imath.Color3f(.3, .7, 1))
points = cloud['out'].object('/deepPoints')
check((points.numPoints == 0 if args.expect_empty else points.numPoints > 0) and
      points.arePrimitiveVariablesValid(), 'Invalid VDB cloud')
note = add('ReviewInstructions', Gaffer.Backdrop(), -15, 70)
note['title'].setValue('Native VDB deep visibility')
note['description'].setValue('Actual firePlume density grid rendered by custom Cycles.\n'
    'Select VDBDeepPoints to view points read from the deep EXR.\n'
    'Enable VolumeDepthCut farClip to slice the stored volume.\n'
    'Scalar extinction Z/ZBack/A; emission and scattered colour are not stored.\n'
    f'{width}x{height}, {settings["samples"]} camera samples; '
    f'preview pixel stride {preview_stride}, at most 1,000,000 points.')
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
report = {'passed': slice_error <= 1e-6, 'scope': 'Gaffer EXR interoperability and beauty isolation',
          'expected_empty': args.expect_empty,
          'total_deep_samples': total_deep_samples, 'max_pixel_samples': max_pixel_samples,
          'camera_samples': settings['samples'], 'device': settings['device'],
          'accepted_camera_pixels': raw_pixels, 'accepted_camera_probes': raw_probes,
          'max_accepted_camera_error': raw_error if diagnostic.exists() else None,
          'preview_pixel_stride': preview_stride,
          'resolution': [width, height], 'diagnostic_pixels': len(pixels),
          'max_beauty_error': beauty_error, 'max_beauty_repeat_error': repeat_error,
          'beauty_tolerance': beauty_tolerance, 'max_slice_error': slice_error,
          'worst_slice': worst_slice,
          'depth_cuts': cuts, 'depth_range': [low, high], 'displayed_points': points.numPoints,
          'physical_grid_oracle': 'Separate CPU/CUDA grid qualification; not this reader check'}
(directory / 'gaffer_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
check(report['passed'], 'Gaffer depth cuts differ: ' + str(worst_slice))
