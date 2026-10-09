# SPDX-License-Identifier: Apache-2.0
"""Separate stored-curve, sliced-sample and FLOAT flattening errors in Gaffer.

gaffer env python SCRIPT RENDER_DIRECTORY
Reads the validator's worst_slice; does not change any acceptance tolerance.
"""
import json
import math
from pathlib import Path
import struct
import sys

import GafferImage
import imath

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'src/deep'))
from validate_gaffer import deep_pixel, flat_alpha

directory = Path(sys.argv[1]).resolve()
worst = json.loads((directory / 'gaffer_validation.json').read_text())['worst_slice']
reader = GafferImage.ImageReader()
reader['fileName'].setValue((directory / 'scene.deep.exr').as_posix())
point = imath.V2i(*worst['pixel'])
cut = GafferImage.DeepSlice()
cut['in'].setInput(reader['out'])
cut['nearClip']['enabled'].setValue(False)
cut['farClip']['enabled'].setValue(True)
cut['farClip']['value'].setValue(worst['depth'])
cut['flatten'].setValue(False)
sliced = deep_pixel(cut['out'], point)
tau, remaining = 0.0, 1.0
for a, b, alpha in sliced:
    if a == b:
        remaining *= 1-alpha
    else:
        tau -= math.log1p(-alpha)
double_alpha = 1-remaining*math.exp(-tau)


def f32(v):
    return struct.unpack('f', struct.pack('f', v))[0]


float_alpha = 0.0
for _, _, alpha in sliced:
    float_alpha = f32(float_alpha + f32(f32(1-float_alpha)*alpha))
cut['flatten'].setValue(True)
actual = flat_alpha(cut['out'], point)
report = {'pixel': list(point), 'depth': worst['depth'], 'sliced_samples': len(sliced),
          'stored_curve_alpha': worst['expected'], 'sliced_double_alpha': double_alpha,
          'float_accumulated_alpha': float_alpha, 'gaffer_flat_alpha': actual,
          'slice_sample_error': abs(double_alpha-worst['expected']),
          'flatten_accumulation_error': abs(actual-double_alpha)}
(directory / 'slice_precision.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
