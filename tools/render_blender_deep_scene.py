"""Render the unmodified scene through native Blender; also used for deep-on/off pairs.

blender --factory-startup --background --disable-autoexec SCENE --python SCRIPT --
  --output DIRECTORY [--samples 4] [--percentage 25]

The .blend is never saved. Geometry, materials, camera and DOF are preserved.
Compositing is disabled so the saved EXR is the renderer result being compared.
"""
import argparse
import bpy
import hashlib
import json
import math
from pathlib import Path
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--samples', type=int, default=4)
parser.add_argument('--percentage', type=int, default=25)
parser.add_argument('--deep', action='store_true')
parser.add_argument('--deep-max-events', type=int, default=16)
parser.add_argument('--deep-memory-mb', type=int, default=512)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
if not 1 <= args.samples <= 4096 or not 1 <= args.percentage <= 100:
    raise ValueError('Invalid sample count or resolution percentage')
if not 1 <= args.deep_max_events <= 64 or not 1 <= args.deep_memory_mb <= 1024:
    raise ValueError('Invalid deep event capacity or working memory')
directory = args.output.resolve()
directory.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
source_hash = hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = args.samples
scene.render.threads_mode = 'FIXED'
scene.render.threads = 8
scene.render.resolution_percentage = args.percentage
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
scene.render.filepath = str(directory / 'beauty.exr')
if args.deep:
    if not hasattr(scene.cycles, 'use_deep_output'):
        raise RuntimeError('This Blender does not include the custom deep adapter')
    scene.cycles.use_deep_output = True
    scene.cycles.deep_output_path = str(directory / 'scene.deep.exr')
    scene.cycles.deep_max_events = args.deep_max_events
    scene.cycles.deep_memory_mb = args.deep_memory_mb
elif hasattr(scene.cycles, 'use_deep_output'):
    scene.cycles.use_deep_output = False
report = {
    'blender': bpy.app.version_string,
    'build_hash': bpy.app.build_hash.decode(),
    'source_file': bpy.data.filepath,
    'camera': scene.camera.name,
    'camera_type': scene.camera.data.type,
    'ortho_scale': scene.camera.data.ortho_scale,
    'frame': scene.frame_current,
    'samples': args.samples,
    'resolution': [scene.render.resolution_x, scene.render.resolution_y],
    'percentage': args.percentage,
    'dof': scene.camera.data.dof.use_dof,
    'adaptive': scene.cycles.use_adaptive_sampling,
    'denoising': scene.cycles.use_denoising,
    'materials': len(bpy.data.materials),
    'objects': len(scene.objects),
    'compositing': False,
    'deep': args.deep,
    'deep_max_events': args.deep_max_events if args.deep else None,
    'deep_memory_mb': args.deep_memory_mb if args.deep else None,
    'view_layers': [layer.name for layer in scene.view_layers if layer.use],
    'pixel_filter': scene.cycles.pixel_filter_type,
    'source_sha256': source_hash,
    'camera_world_matrix': [list(row) for row in scene.camera.matrix_world],
    'camera_frame': [list(v) for v in scene.camera.data.view_frame(scene=scene)],
    'camera_translation': list(scene.camera.matrix_world.to_translation()),
    'camera_rotation_degrees': [math.degrees(v) for v in scene.camera.matrix_world.to_euler('XYZ')],
    'camera_scale': list(scene.camera.matrix_world.to_scale()),
    'pixel_aspect': [scene.render.pixel_aspect_x, scene.render.pixel_aspect_y],
}
(directory/'settings.json').write_text(json.dumps(report, indent=2))
start = time.monotonic()
start_ns = time.time_ns()
bpy.ops.render.render(write_still=True)
report['seconds'] = time.monotonic()-start
report['beauty_exists'] = (directory/'beauty.exr').is_file()
if not report['beauty_exists']:
    raise RuntimeError('Blender did not write beauty.exr')
if args.deep:
    deep = directory/'scene.deep.exr'
    if not deep.is_file() or deep.stat().st_mtime_ns < start_ns:
        raise RuntimeError('Blender did not publish a fresh deep EXR')
    report['deep_bytes'] = deep.stat().st_size
if hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Source blend changed during the render; revalidate asset identity')
(directory/'render.json').write_text(json.dumps(report, indent=2))
print('BLENDER_SCENE_RENDER', json.dumps(report))
