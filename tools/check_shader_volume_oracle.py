"""Compare selected captured rays with uncapped, actual-shader CPU samples.

The renderer's diagnostic CPU evaluator only supplies point densities. This
independent NumPy integral has no renderer events, reduction or fitting. Oracle
CSV names use render coordinates; capture CSV and EXRs use file coordinates.
"""
import argparse
import csv
import itertools
import json
from pathlib import Path
import sys
import numpy as np
from deep_exr import read, bound, pixel

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'src/deep'))
from validate_volume_camera_curves import curve
from sample_csv import open_samples


def shader_reference(directory, height):
    result = {}
    for path in sorted(directory.glob('*.csv')):
        x, y, sample = map(int, path.stem.split('-'))
        data = np.loadtxt(path, delimiter=',', skiprows=1, ndmin=2)
        if len(data) < 1 or not np.all(np.isfinite(data)) or np.any(data[:, 2] < 0):
            raise ValueError('Invalid CPU shader samples: '+str(path))
        at, ends, _ = curve([(a, b, s*(b-a)) for a, b, s in data], [])
        key = x, height-1-y
        result.setdefault(key, {})[sample] = (at, ends)
    if not result:
        raise ValueError('No independent CPU shader rays')
    return result


def compare(directory, oracle):
    image = read(directory/'scene.deep.exr')
    height, _ = image.channels()['A'].pixels.shape
    reference = shader_reference(oracle, height)
    captured = {}
    with open_samples(directory/'scene.deep.exr.samples.csv') as stream:
        rows = csv.DictReader(stream)
        for key, group in itertools.groupby(rows, lambda r: (int(r['file_x']), int(r['file_y']))):
            if key not in reference:
                continue
            rays = {}
            for sample, records in itertools.groupby(group, lambda r: int(r['sample'])):
                intervals, surfaces = [], []
                for r in records:
                    if r['kind'] == 'volume':
                        intervals.append(tuple(float(r[k]) for k in ('front', 'back', 'value')))
                    elif r['kind'] == 'surface':
                        surfaces.append((float(r['front']), float(r['value'])))
                    elif r['kind'] != 'miss':
                        raise ValueError('Unexpected capture kind')
                rays[sample] = curve(intervals, surfaces)
            captured[key] = rays
    maximum_capture = maximum_exr = 0.
    pixels = []
    for key, refs in reference.items():
        rays = captured[key]
        if set(refs) != set(rays) or set(refs) != set(range(len(refs))):
            raise ValueError('Accepted populations differ: '+str(key))
        output = pixel(image, *key)
        published = curve([(a, b, -np.log1p(-v)) for a, b, v in output if a < b],
                          [(a, v) for a, b, v in output if a == b])
        depths = np.unique(np.concatenate([v[1] for v in refs.values()] +
                          [v[1] for v in rays.values()] + [published[1], published[2]]))
        depths = np.sort(np.concatenate((depths, (depths[:-1]+depths[1:])*.5)))
        raw_error = exr_error = 0.
        for start in range(0, len(depths), 4096):
            z = depths[start:start+4096]
            expected = sum(v[0](z) for v in refs.values())/len(refs)
            actual = sum(v[0](z) for v in rays.values())/len(rays)
            raw_error = max(raw_error, float(np.max(np.abs(actual-expected))))
            exr_error = max(exr_error, float(np.max(np.abs(published[0](z)-expected))))
        pixels.append(dict(x=key[0], y=key[1], cameras=len(refs),
                           capture_error=raw_error, exr_error=exr_error))
        maximum_capture = max(maximum_capture, raw_error)
        maximum_exr = max(maximum_exr, exr_error)
    return dict(passed=maximum_exr <= bound(image), header_bound=bound(image),
        max_capture_error=maximum_capture, max_exr_error=maximum_exr, pixels=pixels,
        oracle='Independent NumPy midpoint integral of actual CPU shader samples; no event cap')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('render', type=Path)
    parser.add_argument('oracle', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    result = compare(args.render, args.oracle)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
