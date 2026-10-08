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
import os
from pathlib import Path
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--samples', type=int, default=4)
parser.add_argument('--percentage', type=int, default=25)
parser.add_argument('--threads', type=int, default=8)
parser.add_argument('--deep', action='store_true')
parser.add_argument('--deep-volume', action='store_true')
parser.add_argument('--deep-volume-shader-eval', action='store_true')
parser.add_argument('--deep-volume-step', type=float, default=0)
parser.add_argument('--device', choices=('CPU', 'CUDA', 'OPTIX'), default='CPU')
parser.add_argument('--deep-max-events', type=int, default=16)
parser.add_argument('--deep-memory-mb', type=int, default=512)
parser.add_argument('--deep-ids', action='store_true')
parser.add_argument('--deep-error', default='0.001')
parser.add_argument('--deep-z-tolerance', type=float, default=1e-4)
parser.add_argument('--deep-samples', type=int, default=0)
parser.add_argument('--fixed-sampling', action='store_true')
parser.add_argument('--seed', type=int, help='Independent deep-off Monte Carlo control seed')
parser.add_argument('--save-render-passes', action='store_true',
                    help='Save native noisy/denoising passes for beauty isolation checks')
parser.add_argument('--diagnostic-sample-count', action='store_true',
                    help='Save accepted sample counts to diagnose adaptive beauty differences')
parser.add_argument('--capture-only', action='store_true',
                    help='Diagnostic capture/beauty test; skips curve fitting and deep EXR publication')
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
if not math.isfinite(args.deep_z_tolerance) or args.deep_z_tolerance < 0:
    raise ValueError('Deep z tolerance must be finite and nonnegative')
if args.deep_samples < 0:
    raise ValueError('Deep samples must be nonnegative')
if args.fixed_sampling:
    bpy.context.scene.cycles.use_adaptive_sampling = False
error = 0.0 if args.deep_error == 'strict' else float(args.deep_error)
if args.deep_error != 'strict' and not (1e-6 < error <= 1e-2):
    raise ValueError('Deep error must be strict or (1e-6,0.01]')
if args.diagnostic_sample_count and not args.save_render_passes:
    raise ValueError('Sample-count diagnostics require saved native render passes')
if args.capture_only:
    if not (args.deep and args.deep_volume and args.save_render_passes):
        raise ValueError('Capture-only requires deep volume and saved native render passes')
    os.environ['CYCLES_DEEP_VALIDATE_CAPTURE_ONLY'] = '1'
elif os.environ.get('CYCLES_DEEP_VALIDATE_CAPTURE_ONLY') == '1':
    raise ValueError('Capture-only environment requires explicit --capture-only')
if not 1 <= args.samples <= 4096 or not 1 <= args.percentage <= 100:
    raise ValueError('Invalid sample count or resolution percentage')
if not 1 <= args.threads <= 1024:
    raise ValueError('Invalid CPU thread count')
if not 1 <= args.deep_max_events <= (8192 if args.deep_volume else 64) or not 1 <= args.deep_memory_mb <= 2147483647:
    raise ValueError('Invalid deep event capacity or working memory')
directory = args.output.resolve()
directory.mkdir(parents=True, exist_ok=True)
if args.capture_only and (directory / 'scene.deep.exr').exists():
    raise ValueError('Capture-only requires a directory without previous deep output')
scene = bpy.context.scene
if args.seed is not None:
    scene.cycles.seed = args.seed
source_hash = hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
if args.device != 'CPU':
    preferences = bpy.context.preferences.addons['cycles'].preferences
    preferences.compute_device_type = args.device
    preferences.refresh_devices()
    candidates = [device for device in preferences.devices if device.type == args.device]
    if not candidates:
        raise RuntimeError('No '+args.device+' render device available')
    selected_id = candidates[0].id
    for device in preferences.devices:
        device.use = device.type == args.device and device.id == selected_id
    scene.cycles.device = 'GPU'
scene.cycles.samples = args.samples
scene.render.threads_mode = 'FIXED'
scene.render.threads = args.threads
scene.render.resolution_percentage = args.percentage
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
scene.render.filepath = str(directory / 'beauty.exr')
if args.save_render_passes:
    for layer in scene.view_layers:
        layer.cycles.denoising_store_passes = True
if args.capture_only or args.diagnostic_sample_count:
    for layer in scene.view_layers:
        layer.cycles.pass_debug_sample_count = True
if args.deep:
    if not hasattr(scene.cycles, 'use_deep_output'):
        raise RuntimeError('This Blender does not include the custom deep adapter')
    scene.cycles.use_deep_output = True
    scene.cycles.use_deep_volume = args.deep_volume
    if hasattr(scene.cycles, "deep_volume_step"):
        scene.cycles.use_deep_volume_shader_eval = args.deep_volume_shader_eval
        scene.cycles.deep_volume_step = args.deep_volume_step
    elif args.deep_volume_shader_eval or args.deep_volume_step:
        raise RuntimeError("This Blender does not include volume shader evaluation")
    scene.cycles.deep_error = error
    scene.cycles.deep_z_tolerance = args.deep_z_tolerance
    scene.cycles.deep_samples = args.deep_samples
    if hasattr(scene.cycles, "use_deep_ids"):
        scene.cycles.use_deep_ids = args.deep_ids
    elif args.deep_ids:
        raise RuntimeError("This Blender does not include deep IDs")
    scene.cycles.deep_output_path = str(directory / 'scene.deep.exr')
    scene.cycles.deep_max_events = args.deep_max_events
    scene.cycles.deep_memory_mb = args.deep_memory_mb
elif hasattr(scene.cycles, 'use_deep_output'):
    scene.cycles.use_deep_output = False
report = {
    'renderer_sha256': hashlib.sha256(Path(bpy.app.binary_path).read_bytes()).hexdigest(),
    'blender': bpy.app.version_string,
    'build_hash': bpy.app.build_hash.decode(),
    'source_file': bpy.data.filepath,
    'camera': scene.camera.name,
    'camera_type': scene.camera.data.type,
    'ortho_scale': scene.camera.data.ortho_scale,
    'frame': scene.frame_current,
    'samples': args.samples,
    'threads': args.threads,
    'resolution': [scene.render.resolution_x, scene.render.resolution_y],
    'percentage': args.percentage,
    'dof': scene.camera.data.dof.use_dof,
    'adaptive': scene.cycles.use_adaptive_sampling,
    'adaptive_threshold': scene.cycles.adaptive_threshold,
    'adaptive_min_samples': scene.cycles.adaptive_min_samples,
    'seed': scene.cycles.seed,
    'use_animated_seed': scene.cycles.use_animated_seed,
    'denoising': scene.cycles.use_denoising,
    'denoiser': scene.cycles.denoiser,
    'denoising_use_gpu': scene.cycles.denoising_use_gpu,
    'save_render_passes': args.save_render_passes,
    'capture_only': args.capture_only,
    'diagnostic_sample_count_pass': any(layer.cycles.pass_debug_sample_count
                                       for layer in scene.view_layers),
    'kernel_source_override': os.environ.get('CYCLES_KERNEL_PATH'),
    'materials': len(bpy.data.materials),
    'objects': len(scene.objects),
    'compositing': False,
    'deep': args.deep,
    'device': args.device,
    'deep_volume': args.deep and args.deep_volume,
    'deep_volume_shader_eval': args.deep and args.deep_volume_shader_eval,
    'deep_volume_step': args.deep_volume_step if args.deep else 0,
    'deep_error': error if args.deep else None,
    'deep_z_tolerance': args.deep_z_tolerance if args.deep and error else 0,
    'deep_samples': args.deep_samples if args.deep else None,
    'deep_ids': args.deep_ids if args.deep else False,
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
if bpy.ops.render.render(write_still=True) != {'FINISHED'}:
    raise RuntimeError('Render did not finish')
if args.save_render_passes:
    scene.render.image_settings.media_type = 'MULTI_LAYER_IMAGE'
    scene.render.image_settings.file_format = 'OPEN_EXR_MULTILAYER'
    # Validators read one EXR part; newer Blender defaults to one part per pass.
    interleave = scene.render.image_settings.use_exr_interleave
    scene.render.image_settings.use_exr_interleave = True
    bpy.data.images['Render Result'].save_render(str(directory / 'render-passes.exr'), scene=scene)
    scene.render.image_settings.use_exr_interleave = interleave
    scene.render.image_settings.media_type = 'IMAGE'
    scene.render.image_settings.file_format = 'OPEN_EXR'
report['seconds'] = time.monotonic()-start
report['beauty_exists'] = (directory/'beauty.exr').is_file()
if not report['beauty_exists']:
    raise RuntimeError('Blender did not write beauty.exr')
if args.capture_only:
    if (directory / 'scene.deep.exr').exists():
        raise RuntimeError('Renderer did not honor capture-only mode')
    report['deep_published'] = False
    report['scope'] = 'Capture and beauty diagnostic; no deep alpha/publication qualification'
elif args.deep:
    deep = directory/'scene.deep.exr'
    if not deep.is_file() or deep.stat().st_mtime_ns < start_ns:
        raise RuntimeError('Blender did not publish a fresh deep EXR')
    report['deep_bytes'] = deep.stat().st_size
if hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Source blend changed during the render; revalidate asset identity')
(directory/'render.json').write_text(json.dumps(report, indent=2))
print('BLENDER_SCENE_RENDER', json.dumps(report))
