"""Qualify Phase 8b OSL fixtures using the existing oracle and beauty policy.

Large outputs stay in the supplied D: directory; resumable successful renders
must match the executable and source hashes. This is separate from SVM replay.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
from compare_deep_identity import compare
from compare_deep_ids import flattened_transmittance
from deep_exr import read, bound, pixel, exact_flat
from validate_deep_render import validate

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO/'src/deep'))
from cuda_beauty_gate import validate_cuda_beauty


def native_alpha_waiver(cross, pristine, errors, bounds):
    """Same native difference, with each deep side inside its existing bound."""
    return bool(all(math.isfinite(v) for v in (cross, pristine, *errors, *bounds))
            and pristine > 1e-4 and all(0 <= e <= b for e, b in zip(errors, bounds))
            and abs(cross-pristine) <= sum(bounds))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--device', choices=('CPU', 'OPTIX'), nargs='+', default=('CPU', 'OPTIX'))
    parser.add_argument('--smoke', action='store_true', help='One CPU/OptiX constant pair only')
    parser.add_argument('--pristine', type=Path,
                        help='JSON: pristine executable and per-case CPU/OptiX deep-off directories')
    args = parser.parse_args()
    root = args.root.resolve()
    if root.drive.upper() != 'D:':
        parser.error('Large validation outputs require D:')
    root.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(root/'temp'), TMP=str(root/'temp'),
               BLENDER_USER_RESOURCES='D:/CyclesDeepScratch/regression-cache')
    (root/'temp').mkdir(exist_ok=True)
    for name in ('OCIO', 'CYCLES_KERNEL_PATH', 'CYCLES_DEEP_Z_BASELINE',
                 'CYCLES_DEEP_VALIDATE_CAPTURE_ONLY', 'CYCLES_DEEP_HOST_ONLY_BEAUTY_PROOF'):
        env.pop(name, None)
    digest = hashlib.sha256(args.blender.read_bytes()).hexdigest()
    cases = json.loads(args.cases.read_text())
    pristine = json.loads(args.pristine.read_text()) if args.pristine else None
    result = dict(passed=False, renderer_sha256=digest, cases={}, rejections={},
                  scope='smoke' if args.smoke else 'full', devices=args.device)
    start = time.monotonic()

    def save():
        args.report.write_text(json.dumps(result, indent=2)+'\n')

    def render(case, device, label, mode=None, seed=None, rejection=None):
        directory = root/device/case['name']/label
        directory.mkdir(parents=True, exist_ok=True)
        source_hash = hashlib.sha256(Path(case['scene']).read_bytes()).hexdigest()
        if (directory/'render.json').exists():
            old = json.loads((directory/'render.json').read_text())
            if (old['renderer_sha256'] != digest or old['source_sha256'] != source_hash
                    or old['device'] != device or old['deep'] != bool(mode)
                    or old['seed'] != (seed or 0)
                    or (mode and old['deep_error'] != (0 if mode == 'strict' else float(mode)))):
                raise ValueError('Foreign completed render: '+str(directory))
            return directory
        sentinel = b'OSL rejection must preserve the previous publication\n'
        if rejection:
            (directory/'scene.deep.exr').write_bytes(sentinel)
        command = [str(args.blender), '--factory-startup', '--background', '--disable-autoexec',
                   '--log', 'cycles', '--log-level', 'info', '--python-exit-code', '1', case['scene'],
                   '--python', str(REPO/'tools/render_blender_deep_scene.py'), '--',
                   '--output', str(directory), '--device', device, '--samples', '4',
                   '--percentage', '100', '--threads', '24', '--fixed-sampling',
                   '--save-render-passes', '--diagnostic-sample-count']
        if mode:
            command += ['--deep', '--deep-error', mode, '--deep-z-tolerance', '0']
        if seed is not None:
            command += ['--seed', str(seed)]
        with (directory/'process.log').open('w') as log:
            status = subprocess.run(command, env=env, cwd=REPO, stdout=log, stderr=subprocess.STDOUT)
        if rejection:
            log = (directory/'process.log').read_text(errors='replace')
            if (not status.returncode or rejection.lower() not in log.lower()
                    or (directory/'scene.deep.exr').read_bytes() != sentinel):
                raise ValueError('Missing explicit/atomic rejection: '+str(directory))
            result['rejections'][device+'/'+case['name']] = dict(passed=True, reason=rejection,
                                                               log=str(directory/'process.log'))
            save()
        elif status.returncode or not (directory/'render.json').exists():
            raise RuntimeError('Render failed: '+str(directory/'process.log'))
        return directory

    try:
        for case in cases:
            if args.smoke and case['name'] != 'constant':
                continue
            for device in args.device:
                if case['rejection']:
                    render(case, device, 'reject', '1e-3', rejection=case['rejection'])
                    continue
                controls = [render(case, device, 'off-'+str(i))
                            for i in range(1, (2 if device == 'CPU' or args.smoke else 21))]
                seeds = [] if device == 'CPU' or args.smoke else [
                    render(case, device, 'seed-'+str(i), seed=100+i) for i in range(1, 5)]
                for mode in (('strict',) if args.smoke else ('strict', '1e-4', '1e-3')):
                    directory = render(case, device, mode, mode)
                    value = validate(directory)
                    image = read(directory/'scene.deep.exr')
                    alpha = read(directory/'beauty.exr').channels()['A'].pixels
                    maximum = max(abs(1-flattened_transmittance(pixel(image, x, y))-float(alpha[y,x]))
                                  for y in range(alpha.shape[0]) for x in range(alpha.shape[1]))
                    if maximum > bound(image):
                        raise ValueError('Independent native alpha gate failed: '+str(maximum))
                    value['native_alpha_error'] = maximum
                    if device == 'CPU':
                        value['beauty'] = exact_flat(directory/'beauty.exr', controls[0]/'beauty.exr')
                        value['raw_beauty'] = exact_flat(directory/'render-passes.exr', controls[0]/'render-passes.exr')
                    elif not args.smoke:
                        value['beauty'] = validate_cuda_beauty(directory, controls[:5], pool=controls,
                            seed_references=seeds, builds=REPO/'builds/validation/beauty-builds.json',
                            reader_backend='openexr')
                        if not value['beauty']['passed']:
                            raise ValueError('OSL OptiX beauty gate failed: '+str(directory))
                    if not args.smoke:
                        rerun = render(case, device, mode+'-rerun', mode)
                        value['identity'] = compare(directory/'scene.deep.exr', rerun/'scene.deep.exr')
                        if not value['identity']['passed']:
                            raise ValueError('OSL same-build identity failed: '+str(directory))
                    if device == 'OPTIX':
                        cpu = read(root/'CPU'/case['name']/mode/'scene.deep.exr')
                        cross = max(abs(flattened_transmittance(pixel(image,x,y))-
                                        flattened_transmittance(pixel(cpu,x,y)))
                                    for y in range(alpha.shape[0]) for x in range(alpha.shape[1]))
                        value['cpu_flattened_alpha_difference'] = cross
                        if cross > 1e-4 and pristine and case['name'] in pristine['cases']:
                            executable = Path(pristine['executable'])
                            pristine_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
                            if pristine_hash != pristine['sha256']:
                                raise ValueError('Pristine executable hash differs from recorded evidence')
                            images = []
                            for backend in ('CPU', 'OPTIX'):
                                evidence = Path(pristine['cases'][case['name']][backend])
                                settings = json.loads((evidence/'render.json').read_text())
                                current = json.loads((root/backend/case['name']/mode/'render.json').read_text())
                                keys = ('source_sha256', 'device', 'samples', 'resolution', 'percentage', 'seed', 'adaptive')
                                if (settings['deep'] or settings['renderer_sha256'] != pristine_hash
                                        or any(settings[k] != current[k] for k in keys)):
                                    raise ValueError('Incompatible pristine evidence: '+str(evidence))
                                images.append(read(evidence/'beauty.exr').channels()['A'].pixels)
                            native = float(abs(images[0]-images[1]).max())
                            cpu_value = result['cases']['CPU/'+case['name']+'/'+mode]
                            waived = native_alpha_waiver(cross, native,
                                (cpu_value['native_alpha_error'], maximum), (bound(cpu), bound(image)))
                            value['native_backend_waiver'] = dict(passed=waived,
                                pristine_beauty_difference=native, deep_difference=cross,
                                pristine_sha256=pristine_hash, evidence=pristine['cases'][case['name']])
                    value['exr_bytes'] = (directory/'scene.deep.exr').stat().st_size
                    result['cases'][device+'/'+case['name']+'/'+mode] = value
                    save()
                    if (device == 'OPTIX' and cross > 1e-4
                            and not value.get('native_backend_waiver', {}).get('passed')):
                        raise ValueError('OSL cross-backend flattened alpha gate failed: '+str(cross))
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
