"""Run with Python; independently exercise FLOAT/count/pass-vector gates."""
import math
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from cuda_beauty_gate import (float32_ulp, raw_pass_gate, reference_pool,
                             completed_sample_count, monte_carlo_gate, bias_gate)

assert completed_sample_count('Rendered 8 samples in 0.2 seconds (0.025 seconds per sample)\n'
                              '| Rendered 16 samples in 0.4 seconds\n', 64) == 16
assert completed_sample_count('| Rendered 128 samples in 1.4 seconds\n', 128) == 128
for log in ('', 'Rendered 0 samples in 1 seconds\n', 'Rendered 65 samples in 1 seconds\n',
            'Rendered 16 samples in 1 seconds\nRendered 16 samples in 2 seconds\n'):
    try:
        completed_sample_count(log, 64)
    except ValueError:
        pass
    else:
        raise AssertionError('Ambiguous/invalid sample-count normalization accepted')

assert float32_ulp(0) == 2**-149
assert float32_ulp(-1) == 2**-23
assert float32_ulp(2) == 2**-22
assert float32_ulp(2**-140) == 2**-149
refs = [[1.0]] * 5
assert raw_pass_gate([1 + 4*2**-23], 16, refs, [16]*5)['passed']
assert not raw_pass_gate([1 + 5*2**-23], 16, refs, [16]*5)['passed']
assert not raw_pass_gate([1], 8, refs, [16]*5)['passed']
assert raw_pass_gate([1.25], 16, [[1],[1.5],[1],[1],[1]], [16]*5)['passed']
gate = raw_pass_gate([1], 8, [[1],[2],[2],[2],[2]], [16,8,8,8,8])
assert gate['matched'] == [1,2,3,4]
assert not raw_pass_gate([-1], 8, [[1],[2],[2],[2],[2]], [16,8,8,8,8])['passed']
assert not raw_pass_gate([.5,2.5],16,[[1,1],[2,2],[1,1],[2,2],[1,1]],[16]*5)['passed']
assert raw_pass_gate([1e-8],16,[[0]]*5,[16]*5,envelope=1e-7)['passed']
assert not raw_pass_gate([3e-7],16,[[0]]*5,[16]*5,envelope=1e-7)['passed']
# A reproduced state must match one render across all flattened input passes.
assert raw_pass_gate([.74,891.6,2.59],4,[[.74,891.6,2.59]],[4],envelope=0)['passed']
assert not raw_pass_gate([.74,891.6,2.59],4,[[.74,891.7,2.59],[.73,891.6,2.59]],
                         [4,4],envelope=0)['passed']
with TemporaryDirectory() as temporary:
    root = Path(temporary)
    builds = root / 'builds.json'
    builds.write_text(json.dumps({'builds': {'a': {'beauty_source_sha256': 'same'},
                                           'b': {'beauty_source_sha256': 'same'},
                                           'c': {'beauty_source_sha256': 'changed'}}}))
    for name, sha, deep, samples in [('deep','b',True,4),('old','a',False,4),
                                    ('new','b',False,4),('changed','c',False,4),
                                    ('settings','a',False,8)]:
        directory = root / name
        directory.mkdir()
        (directory / 'render.json').write_text(json.dumps(dict(renderer_sha256=sha,
                                                             deep=deep, samples=samples)))
        (directory / 'render-passes.exr').touch()
    assert reference_pool(root / 'deep', root, builds) == [root / 'new', root / 'old']
for invalid in (math.nan, math.inf):
    try:
        raw_pass_gate([invalid], 16, refs, [16]*5)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid denoiser input accepted')
# Pixel noise does not waive counts, zero variance, image prevalence, or bias.
g = monte_carlo_gate(0.000269, [0.0], [-0.02, 0.0, 0.01, 0.02])
assert g['passed'] and g['ratio'] < 0.1
assert not monte_carlo_gate(0.1, [0.0], [-0.02, 0.0, 0.01, 0.02])['passed']
assert monte_carlo_gate(1, [1], [1]*4)['passed']
assert not monte_carlo_gate(1.00000001, [1], [1]*4)['passed']
assert not monte_carlo_gate(1, [], [1]*4)['passed']
assert 5 <= 0.001*5850 and not 6 <= 0.001*5850
assert bias_gate([0]*100)['passed']
assert bias_gate([-1, 1]*50)['passed']
assert not bias_gate([0.001]*100)['passed']
assert not bias_gate([0.1 + v for v in [-0.01, 0.01]*50])['passed']
for seeds in ([1]*3, [1, 2, 3, math.inf]):
    try:
        monte_carlo_gate(1, [1], seeds)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid Monte Carlo controls accepted')
print('PASS FLOAT ULP, count/reproduced states, four-seed SE, zero variance and bias rejection')
