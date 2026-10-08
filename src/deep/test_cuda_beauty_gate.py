"""Run with Python; independently exercise FLOAT/count/pass-vector gates."""
import math
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from cuda_beauty_gate import (float32_ulp, raw_pass_gate, reference_pool,
                             completed_sample_count, monte_carlo_gate, bias_gate, exact_state_counts)

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
# Root-cause evidence must check auxiliary inputs too, not just identical RGB.
snapshot = [.13, .10, .10, 1, .39, .37, .38, -.006, -.019, -.62, 992.4]
assert raw_pass_gate(snapshot,416,[snapshot],[416],envelope=0)['passed']
for channel in (4,8,10):
    changed = list(snapshot)
    changed[channel] += 8*float32_ulp(snapshot[channel])
    assert not raw_pass_gate(changed,416,[snapshot],[416],envelope=0)['passed']
assert not raw_pass_gate(snapshot,944,[snapshot],[416],envelope=0)['passed']
# Duplicate shortcut is exact across every channel and count, not an ULP guess.
states = [snapshot, list(snapshot), snapshot, [*snapshot[:-1], 993.0]]
populations = [416, 416, 944, 416]
duplicates = exact_state_counts(states, populations)
assert [duplicates[(n,tuple(v))] for v,n in zip(states,populations)] == [2,2,1,1]
for i,(v,n) in enumerate(zip(states,populations)):
    full = raw_pass_gate(v,n,states[:i]+states[i+1:],populations[:i]+populations[i+1:],envelope=0)['passed']
    if duplicates[(n,tuple(v))] > 1:
        assert full
try:
    exact_state_counts([[math.nan],[math.nan]],[416,416])
except ValueError:
    pass
else:
    raise AssertionError('Nonfinite duplicate state accepted')
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
# A measured ordinary maximum accepts its boundary and rejects larger effects.
seeds = [-0.02, 0.0, 0.01, 0.02]
controls = [0.0, 0.1, 0.1]
limit = max(monte_carlo_gate(v, controls[:i]+controls[i+1:], seeds)['ratio']
            for i, v in enumerate(controls))
assert monte_carlo_gate(0.1, [0.0], seeds, ratio_limit=limit)['passed']
assert not monte_carlo_gate(0.11, [0.0], seeds, ratio_limit=limit)['passed']
assert monte_carlo_gate(0.000269, [0.0], seeds, ratio_limit=limit)['passed']
assert monte_carlo_gate(1, [1], [1]*4)['passed']
assert monte_carlo_gate(1 + 4*float32_ulp(1), [1], [1]*4, ratio_limit=0)['passed']
assert not monte_carlo_gate(1 + 5*float32_ulp(1), [1], [1]*4, ratio_limit=0)['passed']
# The floor uses the reference spacing, including at an exponent boundary.
reference = 1 - float32_ulp(.5)
assert not monte_carlo_gate(reference + 5*float32_ulp(reference), [reference],
                            [reference]*4, ratio_limit=0)['passed']
assert not monte_carlo_gate(1, [], [1]*4)['passed']
assert bias_gate([0]*100, 1)['passed']
assert bias_gate([-1, 1]*50, 1)['passed']
assert not bias_gate([0.001]*100, 1)['passed']
assert not bias_gate([0.1 + v for v in [-0.01, 0.01]*50], 1)['passed']
ulp = float32_ulp(1)
assert bias_gate([ulp]*100, 1)['passed']
assert bias_gate([ulp]*100, -1)['passed']
assert not bias_gate([math.nextafter(ulp, math.inf)]*100, 1)['passed']
assert bias_gate([-1, 1]*50, 0)['passed']
assert bias_gate([float32_ulp(0)], 0)['passed']
for value in (math.nan, math.inf):
    try:
        bias_gate([0], value)
    except ValueError:
        pass
    else:
        raise AssertionError('Nonfinite reference mean accepted')
for seeds in ([1]*3, [1, 2, 3, math.inf]):
    try:
        monte_carlo_gate(1, [1], seeds)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid Monte Carlo controls accepted')
print('PASS FLOAT ULP, count/reproduced states, four-seed SE, zero variance and bias rejection')
