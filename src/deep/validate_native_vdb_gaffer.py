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
import subprocess
import os

import CyclesDeep
import Gaffer
import GafferImage
import GafferScene
import imath

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_gaffer import deep_error, check, deep_pixel, tile_index, population_reference_error, beauty_repeat_gate

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('baseline', type=Path)
parser.add_argument('--reader-only', action='store_true')
parser.add_argument('--expect-empty', action='store_true',
                    help='Require zero samples at every pixel for a zero-extinction fixture')
parser.add_argument('--overlap-reference', nargs=2, type=Path,
                    help='Single-grid renders with matching cameras; check combined extinction')
parser.add_argument('--beauty-repeat', type=Path, action='append',
                    help='Supply four times: five independent CUDA deep-off references including baseline')
parser.add_argument('--oracle-python', type=Path,
                    help='Existing NumPy Python environment for bounded, large camera-CSV checks')
parser.add_argument('--beauty-pool', type=Path, action='append', default=[],
                    help='Ordinary control directory with matching settings and unchanged beauty source')
parser.add_argument('--beauty-builds', type=Path,
                    default=os.environ.get('CYCLES_DEEP_BEAUTY_BUILDS'),
                    help='Recorded executable/beauty-source identities for cross-phase pooling')
parser.add_argument('--beauty-pool-root', type=Path,
                    default=Path(__file__).resolve().parents[2] / 'builds/validation',
                    help='Search completed compatible controls across phases')
args = parser.parse_args()
directory, baseline = args.directory.resolve(), args.baseline.resolve()
settings = json.loads((directory / 'render.json').read_text())
check(not settings.get('capture_only'), 'Capture-only diagnostics cannot qualify deep output')
reference = json.loads((baseline / 'render.json').read_text())
check(settings['deep_volume'] and settings['samples'] >= 1, 'Expected native volume fixture')
check(not reference['deep'], 'Beauty baseline must disable deep capture')
for key in ('source_sha256', 'samples', 'resolution', 'percentage', 'device',
            'camera_world_matrix', 'camera_frame', 'frame', 'adaptive', 'denoising'):
    check(settings[key] == reference[key], 'Beauty baseline differs: ' + key)
for key in ('denoiser', 'denoising_use_gpu', 'save_render_passes'):
    check(settings.get(key) == reference.get(key), 'Beauty baseline differs: ' + key)
if not args.reader_only:
    check(settings.get('renderer_sha256') is not None and
          settings['renderer_sha256'] == reference.get('renderer_sha256'),
          'Beauty baseline must use the same executable')
script = Gaffer.ScriptNode()


def add(name, node, x, y):
    script[name] = node
    node.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(x, y),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    return node


reader = add('NativeVDBDeep', GafferImage.ImageReader(), 0, 40)
reader['fileName'].setValue((directory / 'scene.deep.exr').as_posix())
check(reader['out']['deep'].getValue(), 'Output is not deep')
deep_tolerance = deep_error(reader['out'])
if settings.get('deep_error', 0):
    check('cycles:deepError' in reader['out']['metadata'].getValue() and
          math.isclose(deep_tolerance, settings['deep_error'], rel_tol=1e-7),
          'EXR bound differs from the requested setting')
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
if settings['device'] == 'CUDA' and not args.reader_only:
    from cuda_beauty_gate import validate_cuda_beauty, reference_pool
    if args.beauty_builds:
        args.beauty_pool += reference_pool(directory, args.beauty_pool_root, args.beauty_builds)
    cuda_report = validate_cuda_beauty(directory, [baseline] + (args.beauty_repeat or []),
                                    pool=args.beauty_pool, builds=args.beauty_builds)
    # Extend only when raw states remain unmatched. Controls preserve the source
    # scene/settings and never enable deep; the checker verifies every setting.
    for attempt in range(1, 21):
        if cuda_report['raw_passed'] or not args.beauty_builds:
            break
        registry = json.loads(args.beauty_builds.read_text())['builds']
        blender = Path(registry[settings['renderer_sha256']]['executable'])
        control = directory / ('beauty-control-%02d' % attempt)
        if not control.exists():
            control.mkdir()
            command = [str(blender), '--factory-startup', '--background', '--disable-autoexec',
                settings['source_file'], '--python-exit-code', '1', '--python',
                str(Path(__file__).resolve().parents[2] / 'tools/render_blender_deep_scene.py'),
                '--', '--output', str(control), '--samples', str(settings['samples']),
                '--percentage', str(settings['percentage']), '--device', 'CUDA',
                '--threads', str(settings['threads']), '--save-render-passes',
                '--diagnostic-sample-count']
            env = dict(os.environ)
            for key in ('OCIO', 'CYCLES_KERNEL_PATH', 'CYCLES_DEEP_VALIDATE_CAPTURE_ONLY'):
                env.pop(key, None)
            with (control / 'process.log').open('w') as log:
                subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        args.beauty_pool.append(control)
        cuda_report = validate_cuda_beauty(directory, [baseline] + (args.beauty_repeat or []),
                                         pool=args.beauty_pool, builds=args.beauty_builds)
    beauty_error = cuda_report['max_deep_on_off']
    repeat_error = cuda_report['max_ordinary_repeat']
    beauty_peak = cuda_report['peak_absolute_value']
    beauty_tolerance = repeat_error
    beauty_passed = cuda_report['passed']
    beauty_report = dict(cuda_report, comparison='K=5 count-matched denoiser inputs; four FLOAT ULP floor',
                         tolerance=beauty_tolerance)
    (directory / 'beauty_validation.json').write_text(json.dumps(beauty_report, indent=2)+'\n')
    print('CUDA beauty isolation:', json.dumps(dict(passed=beauty_passed,
          K=cuda_report['K'], raw_passed=cuda_report['raw_passed'],
          explained_outliers=cuda_report['explained_outlier_pixels'],
          unexplained_outliers=cuda_report['unexplained_outlier_pixels'])), flush=True)
else:
    repeat = None
    beauty_tolerance = 0.0
    if args.beauty_repeat:
        check(settings['device'] == 'CUDA' and settings['samples'] > 1,
              'Repeat tolerance is only for multisample CUDA fixtures')
        repeat_directory = args.beauty_repeat[0].resolve()
        check(repeat_directory != baseline and repeat_directory != directory,
              'Beauty repeat must be an independent render directory')
        repeated_settings = json.loads((repeat_directory / 'render.json').read_text())
        for key in ('source_sha256', 'samples', 'resolution', 'percentage', 'device',
                    'camera_world_matrix', 'camera_frame', 'frame', 'adaptive', 'denoising', 'deep'):
            check(repeated_settings[key] == reference[key], 'Beauty repeat differs: ' + key)
        for key in ('denoiser', 'denoising_use_gpu', 'save_render_passes'):
            check(repeated_settings.get(key) == reference.get(key), 'Beauty repeat differs: ' + key)
        if not args.reader_only:
            check(repeated_settings.get('renderer_sha256') == settings['renderer_sha256'],
                  'Beauty repeat must use the same executable')
        repeat = GafferImage.ImageReader()
        repeat['fileName'].setValue((repeat_directory / 'beauty.exr').as_posix())
        check(repeat['out']['format'].getValue() == fmt, 'Beauty repeat format mismatch')
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
    raw_error = raw_repeat_error = 0.0
    matched_raw_error = matched_repeat_error = 0.0
    unmatched_population_pixels = native_population_changes = 0
    population_matched = False
    raw_tolerance = beauty_tolerance
    denoised_repeat_gate = settings['denoising'] and repeat is not None
    if denoised_repeat_gate:
        check(settings.get('save_render_passes') and reference.get('save_render_passes') and
              repeated_settings.get('save_render_passes'), 'Denoised CUDA pairs require native noisy passes')
        raw_readers = []
        for source in (directory, baseline, repeat_directory):
            node = GafferImage.ImageReader()
            node['fileName'].setValue((source / 'render-passes.exr').as_posix())
            check(node['out']['format'].getValue() == fmt, 'Noisy pass format mismatch')
            raw_readers.append(node)
        channels = [c for c in raw_readers[0]['out']['channelNames'].getValue() if '.Noisy Image.' in c]
        check(bool(channels), 'Native noisy beauty pass is missing')
        population_matched = all(s.get('diagnostic_sample_count_pass', False)
                                 for s in (settings, reference, repeated_settings))
        count_channel = None
        if population_matched:
            count_names = [[c for c in node['out']['channelNames'].getValue()
                            if 'Debug Sample Count' in c] for node in raw_readers]
            check(all(len(names) == 1 and names == count_names[0] for names in count_names),
                  'Native sample-count pass missing or inconsistent')
            count_channel = count_names[0][0]
        for y in range(0, height, tile):
            for x in range(0, width, tile):
                populations = None
                if population_matched:
                    count_data = [node['out'].channelData(count_channel, imath.V2i(x, y))
                                  for node in raw_readers]
                    populations = []
                    for j in range(min(tile, height-y)):
                        for i in range(min(tile, width-x)):
                            values = [float(d[j*tile+i]) * settings['samples'] for d in count_data]
                            check(all(math.isfinite(v) and 1 <= round(v) <= settings['samples'] and
                                      abs(v-round(v)) <= 1e-4 for v in values), 'Invalid native sample count')
                            counts = [round(v) for v in values]
                            populations.append(counts)
                            unmatched_population_pixels += int(counts[0] not in counts[1:])
                            native_population_changes += int(counts[1] != counts[2])
                for channel in channels:
                    data = [node['out'].channelData(channel, imath.V2i(x, y)) for node in raw_readers]
                    pixel = 0
                    for j in range(min(tile, height-y)):
                        for i in range(min(tile, width-x)):
                            a, b, c = (float(d[j*tile+i]) for d in data)
                            check(all(math.isfinite(v) for v in (a, b, c)), 'Nonfinite noisy beauty')
                            raw_error = max(raw_error, abs(a-b))
                            raw_repeat_error = max(raw_repeat_error, abs(b-c))
                            if populations is not None:
                                counts = populations[pixel]
                                if counts[0] in counts[1:]:
                                    matched_raw_error = max(matched_raw_error,
                                        population_reference_error(a, counts[0], [(b, counts[1]), (c, counts[2])]))
                                if counts[1] == counts[2]:
                                    matched_repeat_error = max(matched_repeat_error, abs(b-c))
                            pixel += 1
        raw_tolerance = raw_repeat_error
    beauty_tolerance = repeat_error if settings['device'] == 'CUDA' else 0.0
    beauty_report = {'max_deep_on_off': beauty_error, 'max_ordinary_repeat': repeat_error,
                     'peak_absolute_value': beauty_peak, 'comparison': 'ordinary-repeat envelope; CPU exact',
                     'tolerance': beauty_tolerance}
    if denoised_repeat_gate:
        beauty_report.update(comparison='independent native raw and denoised repeat envelopes',
                             max_raw_deep_on_off=raw_error, max_raw_ordinary_repeat=raw_repeat_error,
                             raw_tolerance=raw_tolerance)
        if population_matched:
            beauty_report.update(raw_comparison='Ordinary-repeat envelope; population matches are diagnostic only',
                                 max_population_matched_raw_error=matched_raw_error,
                                 max_same_population_native_repeat_error=matched_repeat_error,
                                 unmatched_population_pixels=unmatched_population_pixels,
                                 native_population_changes=native_population_changes)
    (directory / 'beauty_validation.json').write_text(json.dumps(beauty_report, indent=2)+'\n')
    raw_passed = beauty_repeat_gate(settings['device'], raw_error, raw_repeat_error)
    beauty_passed = (beauty_repeat_gate(settings['device'], beauty_error, repeat_error) and
                     (not denoised_repeat_gate or raw_passed))
    print('Beauty isolation:', json.dumps(dict(passed=beauty_passed, **beauty_report)), flush=True)

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
    check({(x, y) for x, y, s in cameras} == expected_pixels, 'Missing diagnostic pixels')
    for x, y in expected_pixels:
        accepted = {s for px, py, s in cameras if (px, py) == (x, y)}
        count = len(accepted) if settings['adaptive'] else settings['samples']
        check(count > 0 and accepted == set(range(count)), 'Missing diagnostic cameras')
    return cameras


diagnostic = directory / 'scene.deep.exr.samples.csv'
check(diagnostic.exists() or settings['samples'] == 1 or args.reader_only,
      'Multi-sample qualification requires accepted-camera diagnostics')
check(not args.overlap_reference or diagnostic.exists(), 'Overlap check requires diagnostics')
raw_error, raw_probes, raw_pixels = 0.0, 0, 0
accepted_populations = []
if diagnostic.exists() and args.oracle_python:
    check(not args.overlap_reference and not args.expect_empty,
          'Large-scene oracle mode is separate from named overlap/empty fixtures')
    stored_path = directory / 'stored_diagnostic_curves.json'
    stored_path.write_text(json.dumps(dict(samples=settings['samples'], adaptive=settings['adaptive'], deep_error=deep_tolerance,
        pixels=[dict(x=x, y=y, samples=deep_pixel(reader['out'], imath.V2i(x, height-1-y)))
                for x, y in sorted({(i*(width-1)//8, j*(height-1)//8)
                                   for i in range(9) for j in range(9)})])))
    oracle_report = directory / 'accepted_camera_oracle.json'
    subprocess.run([str(args.oracle_python.resolve()), '-I',
                    str(Path(__file__).with_name('validate_volume_camera_curves.py')),
                    str(diagnostic), str(stored_path), str(oracle_report)], check=True)
    evidence = json.loads(oracle_report.read_text())
    check(evidence['passed'], 'Independent accepted-camera oracle failed')
    raw_error = evidence['max_accepted_camera_error']
    raw_probes = evidence['accepted_camera_probes']
    raw_pixels = evidence['accepted_camera_pixels']
    accepted_populations = evidence['accepted_populations']
elif diagnostic.exists():
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
        overlap_report = {'passed': overlap_error <= deep_tolerance, 'max_error': overlap_error,
                          'probes': overlap_probes,
                          'worst': worst_overlap,
                          'references': [str(p.resolve()) for p in args.overlap_reference],
                          'scope': 'Separate single-grid capture product at boundaries and midpoints'}
        (directory / 'overlap_validation.json').write_text(json.dumps(overlap_report, indent=2)+'\n')
        check(overlap_report['passed'], 'Combined extinction differs from single-grid product: ' + str(overlap_report))
    expected_pixels = {(i*(width-1)//8, j*(height-1)//8) for i in range(9) for j in range(9)}
    for x, file_y in sorted(expected_pixels):
        accepted = [cameras[key] for key in sorted(cameras) if key[:2] == (x, file_y)]
        accepted_populations.append(len(accepted))
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
    check(raw_error <= deep_tolerance, 'EXR differs from accepted camera transmittance')


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
for name, key in (('translate', 'camera_translation'), ('rotate', 'camera_rotation_degrees')):
    camera['transform'][name].setValue(imath.V3f(*settings[key]))
# Blender's blender_camera_matrix() strips object scale before Cycles renders.
# Applying the source object's scale here would stretch the world-space cloud.
camera['transform']['scale'].setValue(imath.V3f(1))
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
note['title'].setValue('Native scene deep alpha')
note['description'].setValue(f'Source: {Path(settings["source_file"]).name}\n'
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
report = {'deep_error': deep_tolerance, 'passed': beauty_passed and slice_error <= deep_tolerance,
          'beauty_passed': beauty_passed, 'depth_cuts_passed': slice_error <= deep_tolerance,
          'scope': 'Gaffer EXR interoperability and beauty isolation',
          'expected_empty': args.expect_empty,
          'total_deep_samples': total_deep_samples, 'max_pixel_samples': max_pixel_samples,
          'camera_samples': settings['samples'], 'device': settings['device'],
          'min_accepted_population': min(accepted_populations, default=0),
          'max_accepted_population': max(accepted_populations, default=0),
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
check(report['passed'], 'Beauty or Gaffer depth-cut gate failed: ' + str(report))
