"""Run with Python; independently exercise FLOAT/count/pass-vector gates."""
import math
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from cuda_beauty_gate import float32_ulp, raw_pass_gate, reference_pool

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
print('PASS FLOAT ULP, five-reference envelope, count matching and leak rejection')
