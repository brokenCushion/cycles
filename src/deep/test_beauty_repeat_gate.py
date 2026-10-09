"""Run with gaffer env python: enforce the Phase 0 beauty isolation contract."""
from validate_gaffer import beauty_repeat_gate

assert beauty_repeat_gate('CPU', 0, 1)
assert not beauty_repeat_gate('CPU', 1e-12, 1)
assert beauty_repeat_gate('CUDA', 0.1, 0.1)
assert not beauty_repeat_gate('CUDA', 0.1000001, 0.1)
assert not beauty_repeat_gate('CUDA', 1e-12, 0)
assert not beauty_repeat_gate('CUDA', float('nan'), 1)
assert not beauty_repeat_gate('CUDA', 0, float('inf'))
print('PASS CPU exact / CUDA measured repeat envelope, including invalid evidence')
