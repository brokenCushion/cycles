# SPDX-License-Identifier: Apache-2.0
"""Final full-scene CPU regression using the supplied, unchanged Blender asset.

gaffer env python SCRIPT BLENDER SCENE PREVIOUS_DEEP_DIRECTORY OUTPUT
Checks fresh deep-on/off beauty, legacy deep byte identity and Gaffer depth cuts.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import GafferImage
import imath

blender, scene, previous, output = (Path(v).resolve() for v in sys.argv[1:])
repo = Path(__file__).resolve().parent.parent
if output.exists():
    raise RuntimeError('Use a fresh output directory')
output.mkdir(parents=True)
env = dict(os.environ, BLENDER_USER_RESOURCES=str(repo / 'builds/blender/user-resources'))
blender_env = dict(env)
# Gaffer supplies its own ACES config. These fixtures use Blender's bundled
# config; inheriting Gaffer's OCIO silently changes the supplied scene's colours.
blender_env.pop('OCIO', None)
report = {'passed': False, 'blender_sha256': hashlib.sha256(blender.read_bytes()).hexdigest(),
          'renderer_colour_configuration': 'Blender bundled config; child OCIO unset'}
old = json.loads((previous / 'render.json').read_text())
source_hash = hashlib.sha256(scene.read_bytes()).hexdigest()
if source_hash != old['source_sha256']:
    raise RuntimeError('The source asset differs from the legacy regression')
for mode in ('beauty', 'deep'):
    directory = output / mode
    command = [str(blender), '--factory-startup', '--background', '--disable-autoexec',
               str(scene), '--python-exit-code', '1', '--python',
               str(repo / 'tools/render_blender_deep_scene.py'), '--',
               '--output', str(directory), '--samples', '128', '--percentage', '100']
    if mode == 'deep':
        command += ['--deep']
    with (output / (mode + '.log')).open('w') as log:
        subprocess.run(command, cwd=repo, env=blender_env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=2400)
    print('Full Blender asset:', mode, 'rendered', flush=True)
settings = [json.loads((output / mode / 'render.json').read_text()) for mode in ('beauty', 'deep')]
for key in ('source_sha256', 'samples', 'resolution', 'percentage', 'camera_world_matrix',
            'camera_frame', 'frame', 'adaptive', 'dof', 'denoising', 'pixel_filter'):
    if any(s[key] != old[key] for s in settings):
        raise RuntimeError('Legacy scene settings differ: ' + key)
hashes = [hashlib.sha256((directory / 'scene.deep.exr').read_bytes()).hexdigest()
          for directory in (previous, output / 'deep')]
if hashes[0] != hashes[1]:
    raise RuntimeError('Surface deep EXR differs from the qualified legacy output')
readers = []
# Keep the nodes alive; retaining only output plugs loses their reader parents.
for mode in ('beauty', 'deep'):
    reader = GafferImage.ImageReader()
    reader['fileName'].setValue((output / mode / 'beauty.exr').as_posix())
    readers.append(reader)
if readers[0]['out']['format'].getValue() != readers[1]['out']['format'].getValue():
    raise RuntimeError('Beauty formats differ')
width, height = settings[0]['resolution']
tile = GafferImage.ImagePlug.tileSize()
for y in range(0, height, tile):
    for x in range(0, width, tile):
        for channel in ('R', 'G', 'B', 'A'):
            a, b = (r['out'].channelData(channel, imath.V2i(x, y)) for r in readers)
            if list(a) != list(b) or not all(math.isfinite(v) for v in a):
                raise RuntimeError(f'Beauty changed or nonfinite at tile {x},{y}, {channel}')
with (output / 'gaffer.log').open('w') as log:
    subprocess.run([sys.executable, str(repo / 'src/deep/validate_blender_deep_gaffer.py'),
                    str(output / 'deep')], cwd=repo, env=env, stdout=log,
                   stderr=subprocess.STDOUT, check=True, timeout=600)
report.update(passed=True, source_sha256=source_hash, resolution=[width, height],
              samples=128, deep_sha256=hashes, byte_identical_legacy=True,
              max_beauty_error=0.0, renders=settings,
              reader=json.loads((output / 'deep/gaffer_validation.json').read_text()))
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS', output / 'report.json', flush=True)
