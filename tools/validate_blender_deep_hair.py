"""Independent raw camera-chain oracle for all pixels and all depth cuts."""
from collections import defaultdict
import csv
import math
import json


def ledger(path, width, height, samples):
    cameras = defaultdict(lambda: dict(raw=[], events=[], terminal=[]))
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            key = tuple(int(row[k]) for k in ('file_x', 'file_y', 'sample'))
            assert 0 <= key[0] < width and 0 <= key[1] < height and 0 <= key[2] < samples
            stage = int(row['stage'])
            assert stage in (0, 1, 2), row
            cameras[key][('raw', 'events', 'terminal')[stage]].append(row)
    assert set(cameras) == {(x, y, s) for y in range(height) for x in range(width)
                            for s in range(samples)}, 'Missing accepted camera samples'
    for key, camera in cameras.items():
        assert len(camera['terminal']) == 1, key
        terminal = camera['terminal'][0]
        if int(terminal['type']) == 2:  # DEEP_COMPLETE
            assert int(terminal['segment']) == 0
            assert int(terminal['primitive']) == len(camera['events'])
        for hit in camera['raw'] + camera['events']:
            assert all(math.isfinite(float(hit[k])) for k in ('u', 'v', 't', 'facing', 'depth', 'alpha'))
            assert float(hit['t']) >= 0 and float(hit['facing']) in (-1, 1)
        assert len(camera['raw']) >= len(camera['events'])
        fields = ('type', 'primitive', 'segment', 'object', 'u', 'v', 't', 'facing')
        raw = {tuple(hit[k] for k in fields) for hit in camera['raw']}
        assert all(tuple(hit[k] for k in fields) in raw for hit in camera['events']), key
    return cameras


def transmittance(events, depth):
    return math.prod(1 - alpha for z, alpha in events if z <= depth)


def depth_error(chains, output):
    cuts = sorted({z for chain in [*chains, output] for z, _ in chain})
    # Before and after every discontinuity covers the complete step functions.
    cuts = [point for z in cuts for point in (math.nextafter(z, -math.inf), z)]
    return max((abs(sum(transmittance(c, z) for c in chains) / len(chains)
                    - transmittance(output, z)) for z in cuts), default=0)


def known_strand(cameras, name):
    """Orthographic interior rays are inside radius .25, away from spline tips.

    Camera at z=8 and a straight strand at z=2 imply camera-axis depth 6.
    Pixel jitter stays within each .125-wide pixel; these pixels are wholly inside.
    """
    if not name.startswith('single_'): return 0
    selected = [c for (x, y, _), c in cameras.items() if x in (15, 16) and 12 <= y <= 19]
    assert selected
    for camera in selected:
        assert camera['events'], name
        first = camera['events'][0]
        assert abs(float(first['depth']) - 6) <= 1e-5, (name, first)
        alpha = 1 - float(__import__('numpy').float32(.9)) if '_constant_' in name else 1
        assert abs(float(first['alpha']) - alpha) <= 1e-7, (name, first)
        assert len(camera['events']) == (2 if '_constant_' in name and name.endswith('_plane') else 1)
    return len(selected)


def validate(path, width, height, samples):
    import numpy as np
    from deep_exr import read, check_deep
    cameras = ledger(path.with_suffix(path.suffix + '.hits.csv'), width, height, samples)
    deep = read(path); checked = check_deep(deep)
    channels = deep.channels(); maximum = 0
    if 'id' in channels:
        from compare_deep_ids import name_hash
        manifest = json.loads(deep.header()['cycles:deepIDManifest'])
        assert all(int(key, 16) == name_hash(name) for key, name in manifest.items())
    counts = []; raw_counts = []; overflow_pixels = set()
    for (x, y, _), camera in cameras.items():
        counts.append(len(camera['events'])); raw_counts.append(len(camera['raw']))
        terminal = camera['terminal'][0]
        if int(terminal['segment']) == 4: overflow_pixels.add((x, y))
        assert int(terminal['type']) == 2 and int(terminal['segment']) == 0, terminal
    for y in range(height):
        for x in range(width):
            chains = [[(float(np.float32(r['depth'])), float(np.float32(r['alpha'])))
                       for r in cameras[x, y, s]['events']]
                      for s in range(samples)]
            z = channels['Z'].pixels[y, x]; a = channels['A'].pixels[y, x]
            output = [] if z is None else [(float(zv), float(av)) for zv, av in zip(z, a)]
            if 'id' in channels and output:
                owners = {int(r['object_id']) for s in range(samples)
                          for r in cameras[x, y, s]['events'] if float(r['alpha']) > 0}
                assert set(map(int, channels['id'].pixels[y, x])) <= owners, (x, y, owners)
            error = depth_error(chains, output)
            assert error <= checked['deep_error'], (x, y, error, checked['deep_error'])
            maximum = max(maximum, error)
    return dict(pixels=width * height, camera_samples=len(cameras), maximum_all_depth_error=maximum,
                deep_error=checked['deep_error'], median_events=float(np.median(counts)),
                p95_events=float(np.percentile(counts, 95)), maximum_events=max(counts),
                maximum_raw_hits=max(raw_counts), overflow_pixels=len(overflow_pixels))


if __name__ == '__main__':
    assert depth_error([[(1, .5)], []], [(1, .25)]) == 0
    assert depth_error([[(1, .5)], []], [(2, .25)]) == .25
    assert depth_error([[], []], []) == 0
    assert depth_error([[(1, 1)], [(2, 1)]], [(1, .5), (2, 1)]) == 0
    print('HAIR_ORACLE_SELF_TEST_PASS')
