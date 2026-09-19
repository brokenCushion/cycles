# SPDX-License-Identifier: Apache-2.0
"""Compare CPU/CUDA native VDB curves at diagnostic boundaries and midpoints.

gaffer env python SCRIPT CPU.exr CUDA.exr REPORT.json
Backend rays may differ in floating-point arithmetic; interval counts need not match.
"""
import bisect
import json
import math
from pathlib import Path
import sys

import GafferImage
import imath

from validate_gaffer import check, deep_pixel


def curve(samples):
    ends, cumulative = [], [0.0]
    for front, back, alpha in samples:
        check(back >= front and 0 < alpha < 1, 'Invalid absorption interval')
        ends.append(back)
        cumulative.append(cumulative[-1] - math.log1p(-alpha))

    def evaluate(depth, before=False):
        i = bisect.bisect_left(ends, depth) if before else bisect.bisect_right(ends, depth)
        tau = cumulative[i]
        if i < len(samples):
            front, back, alpha = samples[i]
            if back > front and depth > front:
                tau -= math.log1p(-alpha) * min(1.0, (depth-front)/(back-front))
        return math.exp(-tau)
    return evaluate


readers = []
for path in sys.argv[1:3]:
    reader = GafferImage.ImageReader()
    reader['fileName'].setValue(Path(path).resolve().as_posix())
    check(reader['out']['deep'].getValue(), 'Expected deep output')
    readers.append(reader)
fmt = readers[0]['out']['format'].getValue()
check(fmt == readers[1]['out']['format'].getValue(), 'Backend formats differ')
maximum, checks, pixels = 0.0, 0, 0
for y in sorted(set(i*(fmt.height()-1)//8 for i in range(9))):
    for x in sorted(set(i*(fmt.width()-1)//8 for i in range(9))):
        samples = [deep_pixel(reader['out'], imath.V2i(x, y)) for reader in readers]
        functions = [curve(s) for s in samples]
        depths = sorted(set(d for s in samples for front, back, _ in s for d in (front, back)))
        probes = depths + [(a+b)/2 for a, b in zip(depths, depths[1:])]
        for depth in probes:
            for before in (False, True):
                maximum = max(maximum, abs(functions[0](depth, before)-functions[1](depth, before)))
                checks += 1
        pixels += 1
report = {'scope': 'CPU/CUDA diagnostic deep-curve boundaries and midpoints',
          'diagnostic_pixels': pixels, 'comparisons': checks,
          'max_transmittance_error': maximum, 'tolerance': 1e-6,
          'passed': checks > 0 and maximum <= 1e-6}
Path(sys.argv[3]).write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
check(report['passed'], 'Backend diagnostic curves differ beyond tolerance')
