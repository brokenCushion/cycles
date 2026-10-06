# SPDX-License-Identifier: Apache-2.0
"""Run fresh Blender VDB cases and Gaffer checks sequentially.

gaffer env python SCRIPT BLENDER CASES.json OUTPUT [CPU|CUDA]
Use the CUDA toolchain wrapper for CUDA. Inputs are never saved by rendering.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

blender, manifest_path, output = (Path(v).resolve() for v in sys.argv[1:4])
device = sys.argv[4] if len(sys.argv) > 4 else 'CPU'
if device not in ('CPU', 'CUDA'):
    raise ValueError('Expected CPU or CUDA')
manifest = json.loads(manifest_path.read_text())
repo = Path(__file__).resolve().parent.parent
env = dict(os.environ, BLENDER_USER_RESOURCES=str(repo / 'builds/blender/user-resources'))
blender_env = dict(env)
blender_env.pop('OCIO', None)
output.mkdir(parents=True, exist_ok=True)
report = {'device': device, 'blender_sha256': hashlib.sha256(blender.read_bytes()).hexdigest(),
          'renderer_colour_configuration': 'Blender bundled config; child OCIO unset',
          'cases': {}, 'passed': False}
report_path = output / 'report.json'
expected_errors = {
    'reject_ao_opacity': 'ray-traced ambient occlusion cannot drive deep opacity',
    'reject_dof': 'static pinhole camera',
    'reject_motion': 'static pinhole camera',
    'reject_orthographic': 'requires mono perspective',
    'reject_cubic': 'require linear interpolation',
    'reject_color': 'requires scalar extinction',
    'reject_nonlinear': 'nonlinear products of density',
    'reject_reflection': 'unreflected volumes',
}


def run(command, log):
    with log.open('w') as stream:
        child_env = blender_env if command[0] == str(blender) else env
        result = subprocess.run(command, env=child_env, cwd=repo, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=1800)
    return result.returncode


for name, case in manifest['cases'].items():
    directory = output / name
    if directory.exists():
        raise RuntimeError('Use fresh output: ' + str(directory))
    directory.mkdir()
    scene = Path(case['scene'])
    source_hash = hashlib.sha256(scene.read_bytes()).hexdigest()
    rejected = name.startswith('reject_')
    print(device, name, flush=True)
    stats = {'passed': False, 'source_sha256': source_hash, 'rejection': rejected}
    report['cases'][name] = stats
    start = time.monotonic()
    runs = ['deep'] if rejected else ['beauty', 'deep']
    if not rejected and device == 'CUDA':
        runs.insert(1, 'beauty-repeat')
    for kind in runs:
        destination = directory / kind
        destination.mkdir()
        target = destination / 'scene.deep.exr'
        sentinel = b'previous-complete-deep-frame'
        if rejected:
            target.write_bytes(sentinel)
        command = [str(blender), '--factory-startup', '--background', '--disable-autoexec',
                   str(scene), '--python-exit-code', '1', '--python',
                   str(repo / 'tools/render_blender_deep_scene.py'), '--',
                   '--output', str(destination), '--samples', str(case['samples']), '--percentage', '100',
                   '--device', device]
        if name in ('denoised_volume', 'adaptive_denoised_volume'):
            command += ['--save-render-passes']
        if kind == 'deep':
            command += ['--deep', '--deep-volume', '--deep-memory-mb', '1024',
                        '--deep-max-events', str(case.get('deep_max_events', 16))]
        log = directory / (kind + '.log')
        code = run(command, log)
        if rejected:
            text = log.read_text(errors='replace')
            if code == 0 or target.read_bytes() != sentinel or expected_errors[name] not in text:
                report_path.write_text(json.dumps(report, indent=2))
                raise RuntimeError('Expected safe rejection: ' + name + '; see ' + str(log))
            staging = [str(p) for p in destination.iterdir() if '.partial-' in p.name]
            if staging:
                raise RuntimeError('Staging output left after rejection: ' + str(staging))
            stats['expected_error'] = expected_errors[name]
        elif code != 0:
            report_path.write_text(json.dumps(report, indent=2))
            raise RuntimeError('Render failed: ' + str(log))
    if not rejected:
        command = [sys.executable, str(repo / 'src/deep/validate_native_vdb_gaffer.py'),
                   str(directory / 'deep'), str(directory / 'beauty')]
        if device == 'CUDA':
            command += ['--beauty-repeat', str(directory / 'beauty-repeat')]
        if name == 'zero_extinction':
            command += ['--expect-empty']
        if run(command, directory / 'gaffer.log') != 0:
            report_path.write_text(json.dumps(report, indent=2))
            raise RuntimeError('Gaffer validation failed: ' + str(directory / 'gaffer.log'))
        stats['validation'] = json.loads((directory / 'deep/gaffer_validation.json').read_text())
        if name == 'adaptive_volume' and not (
                0 < stats['validation']['min_accepted_population'] < case['samples']):
            raise RuntimeError('Adaptive fixture did not exercise early convergence')
    if hashlib.sha256(scene.read_bytes()).hexdigest() != source_hash:
        raise RuntimeError('Input fixture changed: ' + str(scene))
    stats.update(passed=True, seconds=time.monotonic()-start)
    report_path.write_text(json.dumps(report, indent=2) + '\n')

if all(n in manifest['cases'] for n in ('overlapping_grids', 'overlap_first_only', 'overlap_second_only')):
    directory = output / 'overlapping_grids'
    command = [sys.executable, str(repo / 'src/deep/validate_native_vdb_gaffer.py'),
               str(directory / 'deep'), str(directory / 'beauty'), '--overlap-reference',
               str(output / 'overlap_first_only/deep'), str(output / 'overlap_second_only/deep')]
    if device == 'CUDA':
        command += ['--beauty-repeat', str(directory / 'beauty-repeat')]
    if run(command, directory / 'overlap.log') != 0:
        raise RuntimeError('Independent-layer overlap product failed: ' + str(directory / 'overlap.log'))
    report['overlap'] = json.loads((directory / 'deep/overlap_validation.json').read_text())
if all(n in manifest['cases'] for n in ('scattering', 'overlap_first_only')):
    hashes = [hashlib.sha256((output / n / 'deep/scene.deep.exr').read_bytes()).hexdigest()
              for n in ('scattering', 'overlap_first_only')]
    report['equivalent_extinction'] = {'identical': hashes[0] == hashes[1], 'sha256': hashes}
    if hashes[0] != hashes[1]:
        report_path.write_text(json.dumps(report, indent=2) + '\n')
        raise RuntimeError('Equivalent absorption/scattering deep alpha differs')
if all(n in manifest['cases'] for n in ('camera_ray_depth', 'scattering')):
    hashes = [hashlib.sha256((output / n / 'deep/scene.deep.exr').read_bytes()).hexdigest()
              for n in ('camera_ray_depth', 'scattering')]
    report['camera_ray_depth'] = {'identical': hashes[0] == hashes[1], 'sha256': hashes}
    if hashes[0] != hashes[1]:
        raise RuntimeError('Camera-constant Ray Depth expression changed deep alpha')
report['passed'] = True
report_path.write_text(json.dumps(report, indent=2) + '\n')
print('PASS', report_path, flush=True)
