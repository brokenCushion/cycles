"""Independent, pixel-streamed NumPy oracle for large accepted-camera CSVs.

Run with a Python environment that already provides NumPy. Gaffer exports the
stored EXR curves; this process checks raw extinction without renderer fitting.
"""
import argparse
import csv
import itertools
import json
import math
from pathlib import Path

import numpy as np


def curve(intervals, surfaces):
    # Independent sweep: accumulate all start/end rate changes, then integrate
    # the resulting piecewise constant extinction. No renderer run index/cache.
    if intervals:
        values = np.asarray(intervals, dtype=np.float64)
        rates = values[:, 2] / (values[:, 1] - values[:, 0])
        endpoints = np.concatenate((values[:, 0], values[:, 1]))
        changes = np.concatenate((rates, -rates))
        order = np.argsort(endpoints, kind='stable')
        depths, starts = np.unique(endpoints[order], return_index=True)
        slopes = np.cumsum(np.add.reduceat(changes[order], starts))
        tau = np.concatenate(([0.0], np.cumsum(slopes[:-1] * np.diff(depths))))
    else:
        depths, slopes, tau = (np.empty(0) for _ in range(3))
    surfaces = sorted(surfaces)
    steps = np.asarray([s[0] for s in surfaces])
    transmission = np.concatenate(([1.0], np.cumprod([1-s[1] for s in surfaces])))

    def at(queries, before=False):
        z = np.asarray(queries, dtype=np.float64)
        result = np.ones(z.shape)
        if len(depths):
            indices = np.searchsorted(depths, z, side='right') - 1
            active = indices >= 0
            i = indices[active]
            result[active] = np.exp(-(tau[i] + slopes[i] * (z[active] - depths[i])))
        return result * transmission[np.searchsorted(steps, z, side='left' if before else 'right')]

    return at, depths, steps


def self_test():
    intervals = [(1, 4, .9), (2, 5, .6), (6, 7, .2)]
    surfaces = [(3, .25), (8, 1)]
    at, _, _ = curve(intervals, surfaces)
    probes = np.array([0, 1, 2, 2.5, 3, 4, 5, 6, 6.5, 7, 8, 9])
    for before in (False, True):
        expected = []
        for z in probes:
            t = math.exp(-math.fsum(v * max(0, min(1, (z-a)/(b-a))) for a, b, v in intervals))
            for depth, alpha in surfaces:
                if depth < z or (depth == z and not before):
                    t *= 1-alpha
            expected.append(t)
        assert np.max(np.abs(at(probes, before) - expected)) < 1e-14
    empty, _, _ = curve([], [])
    assert np.all(empty(probes) == 1)


def validate(source, stored_path, report_path):
    stored = json.loads(stored_path.read_text())
    tolerance = stored['deep_error']
    if not (math.isfinite(tolerance) and 0 < tolerance <= 1e-2):
        raise RuntimeError('Invalid EXR-derived error bound')
    outputs = {(p['x'], p['y']): p['samples'] for p in stored['pixels']}
    seen, populations = set(), []
    maximum, probes = 0.0, 0
    with source.open() as stream:
        rows = csv.DictReader(stream)
        for pixel, group in itertools.groupby(rows, lambda row: (int(row['file_x']), int(row['file_y']))):
            if pixel not in outputs or pixel in seen:
                raise RuntimeError('Unexpected or repeated diagnostic pixel: ' + str(pixel))
            seen.add(pixel)
            functions, boundaries = [], set()
            for sample, camera_rows in itertools.groupby(group, lambda row: int(row['sample'])):
                if sample != len(functions) or sample >= stored['samples']:
                    raise RuntimeError('Missing, repeated or out-of-range camera sample')
                intervals, surfaces = [], []
                previous = -1
                for row in camera_rows:
                    event = int(row['event'])
                    a, b, v = (float(row[key]) for key in ('front', 'back', 'value'))
                    if not all(math.isfinite(value) for value in (a, b, v)):
                        raise RuntimeError('Nonfinite diagnostic value')
                    if row['kind'] == 'miss':
                        if previous != -1 or event != -1 or a != 0 or b != 0 or v != 0:
                            raise RuntimeError('Invalid diagnostic miss')
                        previous = -2
                        continue
                    if event != previous + 1:
                        raise RuntimeError('Repeated or missing diagnostic event')
                    previous = event
                    if row['kind'] == 'volume' and 0 < a < b and v >= 0:
                        intervals.append((a, b, v))
                    elif row['kind'] == 'surface' and 0 < a == b and 0 <= v <= 1:
                        surfaces.append((a, v))
                    else:
                        raise RuntimeError('Invalid diagnostic interval/surface')
                at, ends, steps = curve(intervals, surfaces)
                functions.append(at)
                boundaries.update(ends)
                boundaries.update(steps)
            if not functions or (not stored['adaptive'] and len(functions) != stored['samples']):
                raise RuntimeError('Incomplete camera population')
            populations.append(len(functions))
            output = outputs[pixel]
            actual, ends, steps = curve([(a, b, -math.log1p(-v)) for a, b, v in output if a < b],
                                        [(a, v) for a, b, v in output if a == b])
            boundaries.update(ends)
            boundaries.update(steps)
            ordered = sorted(boundaries)
            queries = np.asarray(sorted(boundaries | {(a+b)/2 for a, b in zip(ordered, ordered[1:])}))
            for start in range(0, len(queries), 4096):
                chunk = queries[start:start+4096]
                for before in (True, False):
                    expected = np.zeros(len(chunk))
                    for function in functions:
                        expected += function(chunk, before)
                    expected /= len(functions)
                    observed = actual(chunk, before)
                    if not np.all(np.isfinite(expected)) or not np.all(np.isfinite(observed)):
                        raise RuntimeError('Nonfinite accepted-camera comparison')
                    maximum = max(maximum, float(np.max(np.abs(expected-observed))))
                    probes += len(chunk)
            print('Checked pixel', pixel, 'cameras', len(functions), 'error', maximum, flush=True)
    if seen != set(outputs):
        raise RuntimeError('Missing diagnostic pixels')
    report = dict(passed=maximum <= tolerance, deep_error=tolerance, max_accepted_camera_error=maximum,
                  accepted_camera_probes=probes, accepted_camera_pixels=len(seen),
                  accepted_populations=populations, oracle='Independent NumPy extinction sweep; one pixel at a time')
    report_path.write_text(json.dumps(report, indent=2))
    if not report['passed']:
        raise RuntimeError('Accepted-camera curve error exceeds EXR bound')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('paths', nargs='*', type=Path)
    args = parser.parse_args()
    self_test()
    if not args.self_test:
        if len(args.paths) != 3:
            parser.error('Expected camera CSV, stored curves JSON and report JSON')
        validate(*args.paths)
