"""Run with Python; independently exercise FLOAT/count/pass-vector gates."""
import math
from cuda_beauty_gate import float32_ulp, raw_pass_gate

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
for invalid in (math.nan, math.inf):
    try:
        raw_pass_gate([invalid], 16, refs, [16]*5)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid denoiser input accepted')
print('PASS FLOAT ULP, five-reference envelope, count matching and leak rejection')
