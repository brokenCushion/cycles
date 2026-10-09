"""Read saved Cycles batch logs; never render or change sampling settings."""
import re
from pathlib import Path


def parse_scheduler(text, adaptive, expected):
    batches, filters, end = [], [], 0
    for line in text.splitlines():
        if not re.match(r'^\d+:\d+\.\d+\s+cycles\s+\|', line):
            continue
        match = re.search(r'Rendered (\d+) samples in', line)
        if match:
            count = int(match[1])
            end += count
            batches.append(dict(samples=count, end_sample=end))
        if 'Will filter adaptive stopping buffer' in line:
            filters.append(dict(end_sample=end, log=line.strip()))
    assert end == expected, (end, expected)
    step_match = re.search(r'Step:\s*(\d+)', text)
    if step_match is None:
        assert not adaptive and re.search(r'Use:\s*False', text), 'Missing adaptive Step without adaptive OFF'
        step = minimum = threshold = None
        checkpoints = []
        assert not filters, 'Adaptive filter invocation in fixed-sample run'
    else:
        assert adaptive, 'Adaptive Step in fixed-sample run'
        step = int(step_match[1])
        minimum = int(re.search(r'Min Samples:\s*(\d+)', text)[1])
        threshold = float(re.search(r'Threshold:\s*([\d.]+)', text)[1])
        checkpoints = [b['end_sample'] for b in batches
                       if b['end_sample'] - 1 > minimum
                       and (b['end_sample'] - 1) & (step - 1) == step - 1]
        if filters:
            assert [f['end_sample'] for f in filters] == checkpoints
    return dict(batch_sizes=[b['samples'] for b in batches], batches=batches,
                adaptive=adaptive, threshold=threshold, step=step, min_samples=minimum,
                fixed_sample_count=None if adaptive else expected,
                adaptive_filter_samples=checkpoints, direct_filter_logs=filters,
                filter_evidence='direct debug invocation log' if filters else (
                    'reconstructed from batch ends and AdaptiveSampling::need_filter; INFO omits invocations'
                    if adaptive else 'disabled; no debug invocation'))


def scheduler(path, adaptive, expected):
    return dict(path=str(path), **parse_scheduler(Path(path).read_text(errors='replace'), adaptive, expected))


def self_check():
    fixed = '00:01.000 cycles | Rendered 336 samples in 1 seconds\nAdaptive sampling:\nUse: False\n'
    result = parse_scheduler(fixed, False, 336)
    assert result['fixed_sample_count'] == 336 and result['step'] is None
    assert result['adaptive_filter_samples'] == []
    adaptive = ('00:01.000 cycles | Rendered 16 samples in 1 seconds\n'
                '00:01.001 cycles | Will filter adaptive stopping buffer, threshold 0.15\n'
                'Use: True\nStep: 16\nMin Samples: 8\nThreshold: 0.15\n')
    assert parse_scheduler(adaptive, True, 16)['adaptive_filter_samples'] == [16]
    try:
        parse_scheduler(fixed.replace('Use: False', 'Use: True'), True, 336)
    except AssertionError:
        pass
    else:
        raise AssertionError('Truncated adaptive summary must fail')


if __name__ == '__main__':
    self_check()
    print('PASS fixed-sample missing-Step and adaptive checkpoint parser checks')
