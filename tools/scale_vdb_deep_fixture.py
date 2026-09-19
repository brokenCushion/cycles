"""Create a uniformly scaled VDB fixture for a physical-length/depth check.

Run Blender on the generated VDB scene, then --python SCRIPT -- OUTPUT_DIRECTORY.
The camera and volume scale by two, extinction by one half. Expected deep alpha
is unchanged and camera depths double. Source scene and VDB remain untouched.
"""
import hashlib
import json
from pathlib import Path
import sys
import bpy

source = Path(bpy.data.filepath)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
directory = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
directory.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
volumes = [obj for obj in scene.objects if obj.type == 'VOLUME']
if len(volumes) != 1 or scene.camera is None or len(scene.objects) != 2:
    raise RuntimeError('Expected the generated one-volume/one-camera fixture')
volume = volumes[0]
material = volume.data.materials[0]
multipliers = [node for node in material.node_tree.nodes if node.type == 'MATH']
if len(multipliers) != 1 or multipliers[0].operation != 'MULTIPLY':
    raise RuntimeError('Expected the generated density multiplier')
scale = 2.0
# Preserve the camera's original rotation exactly. Assigning matrix_world
# decomposes it and perturbs rotation before the renderer sees the test.
volume.location *= scale
volume.scale *= scale
scene.camera.location *= scale
scene.camera.data.clip_start *= scale
scene.camera.data.clip_end *= scale
multipliers[0].inputs[1].default_value /= scale
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(directory / 'scene.blend'))
if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Source fixture changed')
(directory / 'transform.json').write_text(json.dumps({
    'source': str(source), 'source_sha256': source_hash,
    'world_scale': scale, 'extinction_multiplier': 1/scale,
    'expected_depth_scale': scale, 'expected_transmittance_change': 0,
}, indent=2))
