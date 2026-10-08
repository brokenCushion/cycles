"""Phase 8c shader volumes: finer-step, identity, beauty and analytic-cost checks."""
import argparse
from contextlib import redirect_stdout
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import numpy as np
from compare_deep_identity import compare
from compare_deep_ids import curve_error, flattened_transmittance
from deep_exr import read, bound, pixel, exact_flat
from validate_deep_render import validate
from check_shader_volume_oracle import compare as shader_oracle

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO/'src/deep'))
from cuda_beauty_gate import validate_cuda_beauty


def metrics(directory):
    text = (directory/'process.log').read_text(errors='replace')
    counters = {}
    for line in text.splitlines():
        if any(tag in line for tag in ('Deep CUDA capture:', 'Deep output:', 'Deep export timing:')):
            for name, value in re.findall(r'\b(\w+)=([0-9.eE+-]+)', line):
                counters[name] = counters.get(name, 0) + float(value)
    times = re.findall(r'Rendered \d+ samples in ([0-9.eE+-]+) seconds\s*\n', text)
    return dict(render_capture_seconds=float(times[-1]), counters=counters,
                exr_bytes=(directory/'scene.deep.exr').stat().st_size)


def difference(left, right):
    a, b = read(left/'scene.deep.exr'), read(right/'scene.deep.exr')
    h, w = a.channels()['A'].pixels.shape
    maximum = flat = 0.
    for y in range(h):
        for x in range(w):
            p, q = pixel(a, x, y), pixel(b, x, y)
            maximum = max(maximum, curve_error(p, q))
            flat = max(flat, abs(flattened_transmittance(p)-flattened_transmittance(q)))
    return dict(max_curve_error=float(maximum), max_flattened_alpha_error=float(flat),
                bound=bound(a), passed=bool(maximum <= bound(a)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    if root.drive.upper() != 'D:':
        parser.error('Large validation outputs require D:')
    root.mkdir(parents=True, exist_ok=True)
    (root/'temp').mkdir(exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(root/'temp'), TMP=str(root/'temp'),
               BLENDER_USER_RESOURCES='D:/CyclesDeepScratch/regression-cache')
    for name in ('OCIO', 'CYCLES_KERNEL_PATH', 'CYCLES_DEEP_Z_BASELINE',
                 'CYCLES_DEEP_VALIDATE_CAPTURE_ONLY', 'CYCLES_DEEP_HOST_ONLY_BEAUTY_PROOF',
                 'CYCLES_DEEP_VOLUME_ORACLE_DIR', 'CYCLES_DEEP_VOLUME_ORACLE_PIXELS',
                 'CYCLES_DEEP_VOLUME_ORACLE_STEP', 'CYCLES_DEEP_VOLUME_FIXED_STEP'):
        env.pop(name, None)
    digest = hashlib.sha256(args.blender.read_bytes()).hexdigest()
    result = dict(passed=False, smoke=args.smoke, renderer_sha256=digest, cases={}, rejections={})
    start = time.monotonic()
    def save():
        args.report.write_text(json.dumps(result, indent=2)+'\n')
    def render(case, device, label, mode=None, step=0, seed=0, analytic=False, extra=(),
               rejection=None, fixed=False, oracle=None):
        directory = root/device/case['name']/label
        directory.mkdir(parents=True, exist_ok=True)
        scene = Path(case['scenes']['analytic' if analytic else 'eval'])
        expected = dict(renderer_sha256=digest, source_sha256=hashlib.sha256(scene.read_bytes()).hexdigest(),
                        device=device, deep=bool(mode), seed=seed, samples=4, percentage=100)
        if mode:
            expected.update(deep_error=0 if mode == 'strict' else float(mode),
                            deep_volume_shader_eval=case['optin'] and not analytic, deep_volume_step=step,
                            deep_volume_fixed_step=fixed)
        if (directory/'render.json').exists():
            old = json.loads((directory/'render.json').read_text())
            if any(old.get(k) != v for k, v in expected.items()):
                raise ValueError('Foreign completed render: '+str(directory))
            return directory
        sentinel = b'previous publication must survive rejection\n'
        if rejection:
            (directory/'scene.deep.exr').write_bytes(sentinel)
        command = [args.blender, '--factory-startup', '--background', '--disable-autoexec',
                   '--log', 'cycles', '--log-level', 'info', scene, '--python-exit-code', '1',
                   '--python', REPO/'tools/render_blender_deep_scene.py', '--', '--output', directory,
                   '--device', device, '--samples', '4', '--percentage', '100', '--threads', '24',
                   '--fixed-sampling', '--seed', str(seed), '--save-render-passes', '--diagnostic-sample-count']
        if mode:
            capacity = 16 if analytic and case['name'] == 'constant' else 8192
            command += ['--deep', '--deep-volume', '--deep-error', mode, '--deep-z-tolerance', '0',
                        '--deep-volume-step', str(step), '--deep-max-events', str(capacity), '--deep-memory-mb', '512']
            if case['optin'] and not analytic:
                command += ['--deep-volume-shader-eval']
        command += list(extra)
        with (directory/'process.log').open('w') as log:
            render_env = env.copy()
            render_env['CYCLES_DEEP_VOLUME_FIXED_STEP'] = '1' if fixed else '0'
            if oracle:
                oracle.mkdir(parents=True, exist_ok=True)
                render_env.update(CYCLES_DEEP_VOLUME_ORACLE_DIR=str(oracle),
                    CYCLES_DEEP_VOLUME_ORACLE_PIXELS=';2,1;;6,2;;8,4;;10,5;;14,7;',
                    CYCLES_DEEP_VOLUME_ORACLE_STEP=str(step/64))
            status = subprocess.run(list(map(str, command)), cwd=REPO, env=render_env, stdout=log, stderr=subprocess.STDOUT)
        if rejection:
            text = (directory/'process.log').read_text(errors='replace')
            if (not status.returncode or rejection.lower() not in text.lower()
                    or (directory/'scene.deep.exr').read_bytes() != sentinel):
                raise ValueError('Missing explicit/atomic rejection: '+str(directory))
            result['rejections'][device+'/'+case['name']+'/'+label] = dict(passed=True, reason=rejection)
            save()
        elif status.returncode or not (directory/'render.json').exists():
            raise RuntimeError('Render failed: '+str(directory/'process.log'))
        return directory
    try:
        for case in json.loads(args.cases.read_text()):
            if args.smoke and case['name'] != 'constant':
                continue
            if case.get('rejection'):
                for device in ('CPU', 'OPTIX'):
                    render(case, device, 'reject', '1e-3', step=.005, rejection=case['rejection'])
                continue
            devices = ('CPU', 'CUDA', 'OPTIX') if case['optin'] else ('CPU', 'OPTIX')
            for device in devices:
                controls = [render(case, device, 'off-'+str(i))
                            for i in range(1, 2 if device == 'CPU' or args.smoke else 21)]
                seeds = [] if device == 'CPU' or args.smoke else [
                    render(case, device, 'seed-'+str(i), seed=100+i) for i in range(1, 5)]
                for mode in (('1e-3',) if args.smoke else ('1e-4', '1e-3')):
                    directory = render(case, device, mode, mode, step=case['step'])
                    step = float(np.asarray(read(directory/'scene.deep.exr').header()['cycles:deepVolumeStepMin']).item())
                    fine = render(case, device, mode+'-fine', mode, step=step/4, fixed=True)
                    fixed = render(case, device, mode+'-fixed', mode, step=step, fixed=True)
                    with (directory/'checks.log').open('w') as log, redirect_stdout(log):
                        value = validate(directory)
                        value['fine_oracle'] = validate(fine)
                    if not value['total_deep_samples'] or not value['fine_oracle']['total_deep_samples']:
                        raise ValueError('Nonzero-volume fixture produced empty deep output: '+str(directory))
                    value.update(metrics=metrics(directory), fine_metrics=metrics(fine),
                                 fixed_metrics=metrics(fixed), fixed_comparison=difference(directory, fixed),
                                 finer_reference=difference(directory, fine), step=step,
                                 fine_step=float(np.asarray(read(fine/'scene.deep.exr').header()['cycles:deepVolumeStep']).item()))
                    result['cases'][device+'/'+case['name']+'/'+mode] = value
                    save()
                    if not value['finer_reference']['passed'] or value['fine_step'] != step/4:
                        raise ValueError('4x-finer reference gate failed: '+str(directory))
                    oracle = root/'cpu-shader-oracle'/case['name']
                    if device == 'CPU' and not oracle.exists():
                        diagnostic = render(case, device, 'oracle-h64', '1e-4', step=step, oracle=oracle)
                        value['oracle_capture_identity'] = compare(
                            directory/'scene.deep.exr', diagnostic/'scene.deep.exr') if mode == '1e-4' else None
                        if value['oracle_capture_identity'] and not value['oracle_capture_identity']['passed']:
                            raise ValueError('Oracle observation changed deep output: '+str(directory))
                    value['independent_cpu_shader_oracle'] = shader_oracle(directory, oracle)
                    if not value['independent_cpu_shader_oracle']['passed']:
                        save()
                        raise ValueError('Uncapped CPU shader oracle failed: '+str(directory))
                    if case['name'] == 'texture' and mode == '1e-4':
                        coarse = render(case, device, 'adaptive-coarse-start', mode, step=.32)
                        with (coarse/'checks.log').open('w') as log, redirect_stdout(log):
                            value['coarse_start_validation'] = validate(coarse)
                        value['coarse_start_oracle'] = shader_oracle(coarse, oracle)
                        value['coarse_start_metrics'] = metrics(coarse)
                        if not value['coarse_start_oracle']['passed']:
                            save()
                            raise ValueError('Adaptive coarse-start refinement failed: '+str(coarse))
                    if device == 'CPU':
                        value['beauty'] = exact_flat(directory/'beauty.exr', controls[0]/'beauty.exr')
                        value['raw_beauty'] = exact_flat(directory/'render-passes.exr', controls[0]/'render-passes.exr')
                    elif not args.smoke:
                        value['beauty'] = validate_cuda_beauty(directory, controls[:5], pool=controls,
                            seed_references=seeds, builds=REPO/'builds/validation/beauty-builds.json', reader_backend='openexr')
                        if not value['beauty']['passed']:
                            save()
                            raise ValueError('GPU beauty gate failed: '+str(directory))
                    rerun = render(case, device, mode+'-rerun', mode, step=case['step'])
                    value['identity'] = compare(directory/'scene.deep.exr', rerun/'scene.deep.exr')
                    if not value['identity']['passed']:
                        raise ValueError('Same-build identity failed: '+str(directory))
                    analytic = render(case, device, mode+'-analytic', mode, analytic=True)
                    with (analytic/'checks.log').open('w') as log, redirect_stdout(log):
                        value['analytic_oracle'] = validate(analytic)
                    value['analytic_metrics'] = metrics(analytic)
                    value['analytic_comparison'] = difference(directory, analytic)
                    value['analytic_equivalence'] = case['analytic_equivalence']
                    if device != 'CPU':
                        value['cpu_comparison'] = difference(directory, root/'CPU'/case['name']/mode)
                        value['cpu_comparison'].update(
                            passed=value['cpu_comparison']['max_flattened_alpha_error'] <= 1e-4,
                            flattened_alpha_limit=1e-4, curve_depth_counts_informational=True)
                        if not value['cpu_comparison']['passed']:
                            raise ValueError('CPU/GPU flattened alpha exceeds cross-backend bound: '+str(directory))
                    save()
                if not args.smoke:
                    if case['optin']:
                        render(dict(case, optin=False), device, 'reject-without-optin', '1e-3',
                               rejection='nonlinear products of density')
                    render(case, device, 'reject-strict', 'strict', step=case['step'], rejection='require numeric')
                    render(case, device, 'reject-step-limit', '1e-3', step=1e-8, rejection='traversal step limit')
                    if case['name'] == 'constant':
                        render(case, device, 'reject-no-step', '1e-3', rejection='requires --deep-volume-step')
                        render(case, device, 'reject-event-limit', '1e-3', step=.00013, rejection='event capacity')
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['seconds'] = time.monotonic()-start
        save()
    print(json.dumps(dict(passed=result['passed'], seconds=result['seconds'], report=str(args.report))))


if __name__ == '__main__':
    main()
