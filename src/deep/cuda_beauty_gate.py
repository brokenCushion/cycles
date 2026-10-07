# SPDX-License-Identifier: Apache-2.0
"""CUDA beauty isolation: reproduced states, bounded Monte Carlo noise, and bias."""
import csv
import json
import math
import re
from pathlib import Path
import struct
import statistics

SETTINGS_KEYS = ('source_sha256', 'samples', 'resolution', 'percentage', 'device',
    'camera_world_matrix', 'camera_frame', 'frame', 'adaptive', 'adaptive_threshold',
    'adaptive_min_samples', 'seed', 'use_animated_seed', 'denoising', 'denoiser',
    'denoising_use_gpu', 'pixel_filter', 'threads', 'save_render_passes',
    'diagnostic_sample_count_pass')


def completed_sample_count(log, maximum):
    """Single-tile PassAccessor normalization, from final scheduler statistics."""
    counts = re.findall(r'Rendered (\d+) samples in [0-9.e+-]+ seconds\s*\n', log)
    if len(counts) != 1 or not 0 < int(counts[0]) <= maximum:
        raise ValueError('Need one valid completed tile for sample-count normalization')
    return int(counts[0])


def reference_pool(directory, root, builds):
    """All completed deep-off controls with verified matching beauty/settings."""
    settings = json.loads((Path(directory) / 'render.json').read_text())
    registry = json.loads(Path(builds).read_text())['builds']
    digest = settings['renderer_sha256']
    result = []
    for path in sorted(Path(root).rglob('render.json')):
        other = json.loads(path.read_text())
        sha = other.get('renderer_sha256')
        same = sha == digest or (sha in registry and digest in registry and
            registry[sha]['beauty_source_sha256'] == registry[digest]['beauty_source_sha256'])
        if (same and not other.get('deep', True) and
                all(other.get(k) == settings.get(k) for k in SETTINGS_KEYS) and
                (path.parent / 'render-passes.exr').is_file()):
            result.append(path.parent)
    return result


def float32_ulp(value):
    """FLOAT spacing at |value|, including zero, subnormals and negative values."""
    if not math.isfinite(value):
        raise ValueError('Nonfinite FLOAT')
    bits = struct.unpack('<I', struct.pack('<f', abs(value)))[0]
    exponent = (bits >> 23) & 255
    if exponent == 255:
        raise ValueError('Outside finite FLOAT range')
    return math.ldexp(1.0, -149 if exponent == 0 else exponent - 150)


def raw_pass_gate(values, population, references, populations, envelope=None):
    """One matching-count reference must satisfy all components of this pass."""
    if (not references or len(references) != len(populations) or
            any(len(r) != len(values) for r in references)):
        raise ValueError('Invalid reference shape')
    if not all(math.isfinite(v) for row in [values] + references for v in row):
        raise ValueError('Nonfinite denoiser input')
    if envelope is None:
        envelope = max(max(r[i] for r in references) - min(r[i] for r in references)
                       for i in range(len(values)))
    if not math.isfinite(envelope) or envelope < 0:
        raise ValueError('Invalid ordinary-repeat envelope')
    limits = [max(envelope, 4 * float32_ulp(v)) for v in values]
    matched = [i for i, n in enumerate(populations) if n == population]
    passing = [i for i in matched if all(abs(a-b) <= limit
               for a, b, limit in zip(values, references[i], limits))]
    return dict(passed=bool(passing), matched=matched, passing=passing,
                envelope=[envelope]*len(values), limits=limits)


def monte_carlo_gate(value, references, seeds):
    """Four independent pixel estimates; SE = sample SD / sqrt(4)."""
    if len(seeds) != 4 or not all(math.isfinite(v) for v in [value] + references + seeds):
        raise ValueError('Need four finite seed-varied pixel estimates')
    sigma = statistics.stdev(seeds) / 2
    if not references:
        return dict(passed=False, difference=None, sigma=sigma, ratio=None)
    nearest = min(range(len(references)), key=lambda i: abs(value-references[i]))
    delta = value-references[nearest]
    return dict(passed=abs(delta) <= 0.1*sigma, difference=delta, sigma=sigma,
                ratio=abs(delta)/sigma if sigma else (0.0 if delta == 0 else None),
                nearest=nearest)


def bias_gate(differences):
    """Paired-pixel signed mean against mean same-count controls; no ULP floor."""
    if not differences or not all(math.isfinite(v) for v in differences):
        raise ValueError('Need finite paired-pixel differences')
    mean = statistics.mean(differences)
    se = statistics.stdev(differences)/math.sqrt(len(differences)) if len(differences) > 1 else 0.0
    return dict(passed=abs(mean) <= 3*se, mean_signed_difference=mean,
                standard_error=se, limit=3*se, pixels=len(differences))


def validate_cuda_beauty(directory, references, qualification=True, output=None, pool=(), builds=None,
                         seed_references=()):
    """Check all stored denoiser inputs; trace every denoised outlier at its pixel."""
    import GafferImage
    import imath

    directory = Path(directory).resolve()
    references = [Path(p).resolve() for p in references]
    if qualification and len(references) != 5:
        raise ValueError('CUDA beauty qualification requires exactly five deep-off runs')
    if len(set(references + [directory])) != len(references) + 1:
        raise ValueError('Reference renders must be independent directories')
    paths = [directory] + references
    settings = [json.loads((p / 'render.json').read_text()) for p in paths]
    if settings[0]['device'] != 'CUDA' or not settings[0]['deep'] or any(s['deep'] for s in settings[1:]):
        raise ValueError('Expected one CUDA deep render and independent deep-off controls')
    keys = SETTINGS_KEYS
    def executable(p, s):
        return s.get('renderer_sha256') or json.loads((p / 'measurement.json').read_text())['executable_sha256']
    digest = executable(directory, settings[0])
    for p, s in zip(paths, settings):
        if executable(p, s) != digest or any(s.get(k) != settings[0].get(k) for k in keys):
            raise ValueError('Reference executable/settings mismatch: ' + str(p))
        if not s.get('save_render_passes') or not s.get('diagnostic_sample_count_pass'):
            raise ValueError('CUDA gate needs saved denoiser inputs and accepted sample counts')
    pool = list(dict.fromkeys(Path(p).resolve() for p in list(references) + list(pool)))
    identities = json.loads(Path(builds).read_text())['builds'] if builds else {}
    for p in pool:
        s = json.loads((p / 'render.json').read_text())
        other = executable(p, s)
        same_beauty = (other == digest or
            (digest in identities and other in identities and
             identities[digest]['beauty_source_sha256'] == identities[other]['beauty_source_sha256']))
        if not same_beauty or s['deep'] or any(s.get(k) != settings[0].get(k) for k in keys):
            raise ValueError('Pool beauty source/settings mismatch: ' + str(p))

    seed_references = [Path(p).resolve() for p in seed_references]
    if seed_references and (len(seed_references) != 4 or len(set(seed_references)) != 4):
        raise ValueError('Need exactly four independent seed-varied deep-off renders')
    seed_settings = []
    for p in seed_references:
        s = json.loads((p / 'render.json').read_text())
        other = executable(p, s)
        same_beauty = (other == digest or
            (digest in identities and other in identities and
             identities[digest]['beauty_source_sha256'] == identities[other]['beauty_source_sha256']))
        if (not same_beauty or s['deep'] or
                any(s.get(k) != settings[0].get(k) for k in keys if k != 'seed')):
            raise ValueError('Seed control beauty source/settings mismatch: ' + str(p))
        seed_settings.append(s)
    if len({s['seed'] for s in seed_settings}) != len(seed_settings):
        raise ValueError('Seed controls must use four distinct seeds')
    # Keep old K=3 diagnostic evidence reproducible. Qualification uses the final
    # user rule; seed controls are needed only when a state is not reproduced.
    final_raw_rule = qualification or bool(seed_references)

    def readers(filename):
        result = []
        for p in paths:
            node = GafferImage.ImageReader()
            node['fileName'].setValue((p / filename).as_posix())
            result.append(node)
        return result
    raw, beauty = readers('render-passes.exr'), readers('beauty.exr')
    # Retain qualified K-run denoised envelopes across unchanged beauty builds.
    # Raw gates still use ONLY the current K references and reproduced-state rule.
    historical_groups = {}
    for p in pool:
        if (p / 'beauty.exr').is_file():
            historical_groups.setdefault(p.parent, []).append(p)
    historical = []
    for parent, paths in historical_groups.items():
        if len(paths) >= 5:
            nodes = []
            for p in paths:
                node = GafferImage.ImageReader()
                node['fileName'].setValue((p / 'beauty.exr').as_posix())
                nodes.append(node)
            historical.append((parent, nodes))
    pool_raw = []
    for p in pool:
        node = GafferImage.ImageReader()
        node['fileName'].setValue((p / 'render-passes.exr').as_posix())
        pool_raw.append(node)
    seed_raw = []
    for p in seed_references:
        node = GafferImage.ImageReader()
        node['fileName'].setValue((p / 'render-passes.exr').as_posix())
        seed_raw.append(node)
    fmt = raw[0]['out']['format'].getValue()
    if any(n['out']['format'].getValue() != fmt for n in raw + beauty):
        raise ValueError('Reference image size mismatch')
    names = list(raw[0]['out']['channelNames'].getValue())
    if any(list(n['out']['channelNames'].getValue()) != names for n in raw):
        raise ValueError('Denoiser input channel mismatch')
    if any(n['out']['format'].getValue() != fmt or
           list(n['out']['channelNames'].getValue()) != names for n in pool_raw + seed_raw):
        raise ValueError('Pool image/channel mismatch')
    counts = [c for c in names if 'Debug Sample Count' in c]
    if len(counts) != 1:
        raise ValueError('Missing accepted sample-count channel')
    groups = {}
    for c in names:
        if any('.'+p+'.' in c for p in ('Noisy Image', 'Denoising Albedo',
               'Denoising Normal', 'Denoising Depth')) or c in counts:
            groups.setdefault(c.rsplit('.', 1)[0], []).append(c)
    if settings[0]['denoising']:
        for required in ('Noisy Image', 'Denoising Albedo', 'Denoising Normal', 'Denoising Depth'):
            if not any(required in g for g in groups):
                raise ValueError('Missing denoiser input: ' + required)
    elif not any('Noisy Image' in g for g in groups):
        combined = [c for c in names if '.Combined.' in c]
        if not combined:
            raise ValueError('Missing raw combined pass')
        groups[combined[0].rsplit('.', 1)[0]] = combined
    stats = {g: dict(channels=cs, violations=0, k5_violations=0, max_envelope=0.0, max_matching_error=0.0,
                     max_four_ulp=0.0, worst=None, worst_violation=None) for g, cs in groups.items()}
    report = dict(passed=False, qualification=qualification, K=len(references),
                  renderer_sha256=digest, references=[str(p) for p in references],
                  envelope_scope='one maximum per pass over all pixels/components and every ordinary pair',
                  raw_rule='max(envelope, 4 FLOAT ULP); one count-matched reference per pass',
                  input_passes=stats, unmatched_population_pixels=0,
                  max_deep_on_off=0.0, max_ordinary_repeat=0.0, peak_absolute_value=0.0,
                  denoised_outlier_pixels=0, explained_outlier_pixels=0,
                  unexplained_outlier_pixels=0, worst_denoised_pixels=[])
    report['pool'] = [str(p) for p in pool]
    report['reproduced_pixels'] = []
    report['seed_references'] = [str(p) for p in seed_references]
    report['statistical_pixels'] = []
    report['statistical_fraction_limit'] = 0.001
    if final_raw_rule:
        report['raw_rule'] = 'one reproduced state within 4 ULP; otherwise same-count nearest <= 0.1 four-seed pixel SE; image bias <= 3 SE; statistical pixels <= 0.1%'
    checked_channels = [c for cs in groups.values() for c in cs]
    bias_differences = {c: [] for c in checked_channels}
    output = Path(output) if output else directory / 'cuda_beauty_validation.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    trace = output.with_suffix('.pixels.csv')
    tile_size = GafferImage.ImagePlug.tileSize()
    width, height = fmt.width(), fmt.height()
    channels = ('R', 'G', 'B', 'A')
    maximum_samples = settings[0]['samples']
    # range(K values) equals max over all K*(K-1)/2 pairs at a pixel.
    # The plan's per-pass envelope is the maximum of those ranges over the image.
    envelopes = {group:0.0 for group in groups}
    denoised_envelope = 0.0
    historical_envelopes = {str(parent):0.0 for parent, _ in historical}
    for y in range(0, height, tile_size):
        for x in range(0, width, tile_size):
            origin = imath.V2i(x,y)
            valid = [j*tile_size+i for j in range(min(tile_size,height-y))
                     for i in range(min(tile_size,width-x))]
            for group, cs in groups.items():
                for c in cs:
                    data = [r['out'].channelData(c,origin) for r in raw[1:]]
                    for index in valid:
                        values = [float(d[index]) for d in data]
                        if not all(math.isfinite(v) for v in values):
                            raise ValueError('Nonfinite reference denoiser input')
                        envelopes[group] = max(envelopes[group],max(values)-min(values))
            for c in channels:
                data = [r['out'].channelData(c,origin) for r in beauty[1:]]
                for index in valid:
                    values = [float(d[index]) for d in data]
                    if not all(math.isfinite(v) for v in values):
                        raise ValueError('Nonfinite reference denoised beauty')
                    denoised_envelope = max(denoised_envelope,max(values)-min(values))
                for parent, nodes in historical:
                    data = [n['out'].channelData(c,origin) for n in nodes]
                    for index in valid:
                        values = [float(d[index]) for d in data]
                        if not all(math.isfinite(v) for v in values):
                            raise ValueError('Nonfinite historical denoised beauty')
                        key = str(parent)
                        historical_envelopes[key] = max(historical_envelopes[key], max(values)-min(values))
    report['current_K_denoised_envelope'] = denoised_envelope
    report['historical_denoised_envelopes'] = historical_envelopes
    denoised_envelope = max([denoised_envelope] + list(historical_envelopes.values()))
    report['max_ordinary_repeat'] = denoised_envelope
    with trace.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=('file_x', 'file_y', 'reference_index', 'sample_count',
            'denoised_channel', 'difference', 'envelope', 'input_channel', 'deep_input',
            'ordinary_input', 'input_difference', 'input_limit', 'explained'))
        writer.writeheader()
        for y in range(0, height, tile_size):
            for x in range(0, width, tile_size):
                origin = imath.V2i(x, y)
                count_data = [r['out'].channelData(counts[0], origin) for r in raw]
                raw_data = {c: [r['out'].channelData(c, origin) for r in raw]
                            for cs in groups.values() for c in cs}
                color_data = {c: [r['out'].channelData(c, origin) for r in beauty] for c in channels}
                if final_raw_rule:
                    pool_data = {c: [r['out'].channelData(c, origin) for r in pool_raw]
                                 for c in checked_channels}
                    seed_data = {c: [r['out'].channelData(c, origin) for r in seed_raw]
                                 for c in checked_channels}
                for j in range(min(tile_size, height-y)):
                    for i in range(min(tile_size, width-x)):
                        index = j * tile_size + i
                        values = [float(d[index]) * maximum_samples for d in count_data]
                        if not all(math.isfinite(v) and 1 <= round(v) <= maximum_samples and
                                   abs(v-round(v)) <= 1e-4 for v in values):
                            raise ValueError('Invalid accepted sample count')
                        populations = [round(v) for v in values]
                        matched = [r for r in range(len(references)) if populations[r+1] == populations[0]]
                        report['unmatched_population_pixels'] += int(not matched)
                        actual = [float(color_data[c][0][index]) for c in channels]
                        native = [[float(color_data[c][r+1][index]) for c in channels]
                                  for r in range(len(references))]
                        if not all(math.isfinite(v) for row in [actual]+native for v in row):
                            raise ValueError('Nonfinite denoised beauty')
                        envelope = [denoised_envelope]*4
                        report['max_deep_on_off'] = max(report['max_deep_on_off'],
                                                      max(abs(v-n) for v, n in zip(actual, native[0])))
                        report['max_ordinary_repeat'] = max(report['max_ordinary_repeat'], max(envelope))
                        report['peak_absolute_value'] = max(report['peak_absolute_value'],
                            max(abs(v) for row in [actual]+native for v in row))
                        inside = [r for r in matched if all(abs(v-n) <= e for v,n,e in zip(actual,native[r],envelope))]
                        candidates = inside or matched
                        closest = min(candidates, key=lambda r: max(abs(v-n) for v, n in zip(actual, native[r]))) if candidates else 0
                        differences = [abs(v-n) for v, n in zip(actual, native[closest])]
                        outlier = bool(matched) and not inside
                        changes, evidence, failed_groups = [], {}, []
                        for group, cs in groups.items():
                            v = [float(raw_data[c][0][index]) for c in cs]
                            refs = [[float(raw_data[c][r+1][index]) for c in cs] for r in range(len(references))]
                            gate = raw_pass_gate(v, populations[0], refs, populations[1:], envelopes[group])
                            s = stats[group]
                            s['violations'] += int(not gate['passed'])
                            s['k5_violations'] += int(not gate['passed'])
                            if not gate['passed']:
                                failed_groups.append(group)
                            s['max_envelope'] = max(s['max_envelope'], max(gate['envelope']))
                            s['max_four_ulp'] = max(s['max_four_ulp'], max(4*float32_ulp(a) for a in v))
                            error = min((max(abs(a-b) for a,b in zip(v, refs[r])) for r in gate['matched']), default=0)
                            if not gate['passed'] and s['worst_violation'] is None:
                                s['worst_violation'] = dict(file_pixel=[x+i,height-1-(y+j)], values=v,
                                    references=refs, sample_counts=populations, limits=gate['limits'])
                            if error >= s['max_matching_error']:
                                s['max_matching_error'] = error
                                s['worst'] = dict(file_pixel=[x+i, height-1-(y+j)], values=v, references=refs,
                                    sample_counts=populations, envelopes=gate['envelope'], limits=gate['limits'],
                                    passing_references=gate['passing'])
                            if outlier:
                                changed = []
                                for k, c in enumerate(cs):
                                    delta = abs(v[k]-refs[closest][k])
                                    if delta:
                                        detail = dict(channel=c, deep=v[k], reference=refs[closest][k],
                                                      difference=delta, limit=gate['limits'][k])
                                        changed.append(detail)
                                        changes.append(detail)
                                if changed:
                                    evidence[group] = changed
                        if final_raw_rule:
                            same_count = []
                            for r in range(len(pool)):
                                n = float(pool_data[counts[0]][r][index])*maximum_samples
                                if not math.isfinite(n) or abs(n-round(n)) > 1e-4:
                                    raise ValueError('Invalid pool accepted sample count')
                                if round(n) == populations[0]:
                                    same_count.append(r)
                            v = [float(raw_data[c][0][index]) for c in checked_channels]
                            refs = [[float(pool_data[c][r][index]) for c in checked_channels]
                                    for r in same_count]
                            reproduced = raw_pass_gate(v, populations[0], refs,
                                [populations[0]]*len(refs), envelope=0) if refs else dict(passed=False)
                            final_failed = []
                            if reproduced['passed']:
                                if failed_groups or not matched:
                                    r = same_count[reproduced['passing'][0]]
                                    report['reproduced_pixels'].append(dict(file_pixel=[x+i,height-1-(y+j)],
                                        sample_count=populations[0], checked_passes=list(groups),
                                        k5_failed_passes=failed_groups, matching_pool_run=str(pool[r])))
                            else:
                                details = []
                                for group, cs in groups.items():
                                    for c in cs:
                                        a = float(raw_data[c][0][index])
                                        ref = [float(pool_data[c][r][index]) for r in same_count]
                                        seeds = [float(d[index]) for d in seed_data[c]]
                                        gate = monte_carlo_gate(a, ref, seeds) if seeds else dict(
                                            passed=False, difference=None, sigma=None, ratio=None,
                                            reason='Missing four seed-varied controls')
                                        details.append(dict(channel=c, **gate,
                                            nearest_reference=str(pool[same_count[gate['nearest']]])
                                            if 'nearest' in gate else None))
                                        if not gate['passed'] and group not in final_failed:
                                            final_failed.append(group)
                                report['statistical_pixels'].append(dict(file_pixel=[x+i,height-1-(y+j)],
                                    sample_count=populations[0], passed=not final_failed,
                                    failed_passes=final_failed, channels=details))
                            for group in groups:
                                stats[group]['violations'] += int(group in final_failed)-int(group in failed_groups)
                            # Counts remain exact, even when the statistical rule is used.
                            report['unmatched_population_pixels'] -= int(not matched)
                            report['unmatched_population_pixels'] += int(not same_count)
                            if same_count:
                                for c in checked_channels:
                                    ref_mean = statistics.mean(float(pool_data[c][r][index]) for r in same_count)
                                    bias_differences[c].append(float(raw_data[c][0][index])-ref_mean)
                        elif failed_groups or not matched:
                            cs = [c for group in groups for c in groups[group]]
                            v = [float(raw_data[c][0][index]) for c in cs]
                            for p, node in zip(pool, pool_raw):
                                n = float(node['out'].channelData(counts[0], origin)[index]) * maximum_samples
                                if not math.isfinite(n) or abs(n-round(n)) > 1e-4 or round(n) != populations[0]:
                                    continue
                                ref = [float(node['out'].channelData(c, origin)[index]) for c in cs]
                                gate = raw_pass_gate(v, populations[0], [ref], [round(n)], envelope=0)
                                if gate['passed']:
                                    for group in failed_groups:
                                        stats[group]['violations'] -= 1
                                    report['unmatched_population_pixels'] -= int(not matched)
                                    report['reproduced_pixels'].append(dict(file_pixel=[x+i,height-1-(y+j)],
                                        sample_count=populations[0], checked_passes=list(groups),
                                        k5_failed_passes=failed_groups, matching_pool_run=str(p),
                                        channels=cs, values=v, reference=ref, limits=gate['limits']))
                                    break
                        if outlier:
                            report['denoised_outlier_pixels'] += 1
                            explained = bool(changes)
                            report['explained_outlier_pixels'] += int(explained)
                            report['unexplained_outlier_pixels'] += int(not explained)
                            k = max(range(4), key=lambda k: differences[k]-envelope[k])
                            strongest = max(changes, key=lambda c: c['difference']/c['limit']) if changes else {}
                            writer.writerow(dict(file_x=x+i, file_y=height-1-(y+j), reference_index=closest,
                                sample_count=populations[0], denoised_channel=channels[k], difference=differences[k],
                                envelope=envelope[k], input_channel=strongest.get('channel'),
                                deep_input=strongest.get('deep'), ordinary_input=strongest.get('reference'),
                                input_difference=strongest.get('difference'), input_limit=strongest.get('limit'), explained=explained))
                            detail = dict(file_pixel=[x+i,height-1-(y+j)], reference_index=closest,
                                sample_counts=populations, denoised_values=actual, ordinary_values=native,
                                differences=differences, envelopes=envelope, changed_inputs=evidence,
                                explained_by_input_difference=explained)
                            report['worst_denoised_pixels'].append(detail)
                            report['worst_denoised_pixels'].sort(key=lambda p:max(p['differences']), reverse=True)
                            del report['worst_denoised_pixels'][20:]
    report['raw_passed'] = not report['unmatched_population_pixels'] and all(not s['violations'] for s in stats.values())
    if final_raw_rule:
        report['bias_estimator'] = 'mean paired-pixel residual to mean same-count pool references; sample SD / sqrt(pixel count)'
        report['bias'] = {c: bias_gate(d) for c, d in bias_differences.items() if d}
        report['bias_passed'] = (all(len(d) == width*height for d in bias_differences.values()) and
                                 all(r['passed'] for r in report['bias'].values()))
        report['statistical_pixel_fraction'] = len(report['statistical_pixels'])/(width*height)
        report['statistical_fraction_passed'] = len(report['statistical_pixels']) <= 0.001*width*height
        report['raw_passed'] &= report['bias_passed'] and report['statistical_fraction_passed']
    report['outliers_explained'] = not report['unexplained_outlier_pixels']
    report['passed'] = report['raw_passed'] and report['outliers_explained']
    report['denoised_pixels_csv'] = str(trace)
    output.write_text(json.dumps(report, indent=2)+'\n')
    return report
