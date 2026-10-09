"""Check a saved small-scene deep-off/on pair; never launch a render."""
import argparse
import json
from pathlib import Path
from deep_scheduler_log import scheduler


def same_batches(off, on):
    assert off['batch_sizes'] == on['batch_sizes'], 'Deep changed scheduler batch sizes'
    assert off['adaptive_filter_samples'] == on['adaptive_filter_samples'], 'Deep changed adaptive checkpoints'


def self_check():
    case = dict(batch_sizes=[1, 1, 2, 4, 8, 16], adaptive_filter_samples=[16, 32])
    same_batches(case, case)
    try:
        same_batches(case, dict(case, batch_sizes=[1, 1, 2, 4, 8, 14, 2]))
    except AssertionError:
        pass
    else:
        raise AssertionError('The former 16 versus 14/2 schedule must be rejected')


def compare(off, on):
    a, b = (json.loads((d / 'render.json').read_text()) for d in (off, on))
    assert not a['deep'] and b['deep']
    for key in ('renderer_sha256', 'source_sha256', 'device', 'samples', 'resolution',
                'percentage', 'adaptive', 'adaptive_threshold', 'adaptive_min_samples',
                'seed', 'use_animated_seed', 'frame', 'threads', 'denoiser', 'denoising_use_gpu'):
        assert a[key] == b[key], key
    schedules = {label: scheduler(path / 'process.log', a['adaptive'], a['samples'])
                 for label, path in (('off', off), ('on', on))}
    same_batches(schedules['off'], schedules['on'])
    return dict(passed=True, device=a['device'], renderer_sha256=a['renderer_sha256'],
                source_sha256=a['source_sha256'], scheduler=schedules)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('off', type=Path, nargs='?')
    parser.add_argument('on', type=Path, nargs='?')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    self_check()
    if args.off is None and args.on is None:
        print('PASS batch-sequence regression self-check')
    else:
        assert args.off is not None and args.on is not None
        result = compare(args.off, args.on)
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
