# SPDX-License-Identifier: Apache-2.0
"""Check deep alpha against beauty alpha for scaled native VDBs, Object and World space.

blender --factory-startup --background --python SCRIPT -- --vdb FILE --output DIR
  [--device CPU|CUDA|OPTIX] [--scales 0.00375 2.0 0.25,1,0.5] [--samples 256] [--size 32]
  [--tolerance 0.008] [--motion]

Cycles' native volume closures multiply density by the object's volume density
(1 / |M * normalize(1,1,1)| for Volume Space = Object, 1 for World). Deep native
grid extinction must do the same. Each case builds a fresh in-memory scene (no
.blend is read or saved), renders beauty with a transparent film and deep output,
and compares flattened deep alpha 1 - prod(1 - A) with beauty alpha. Object-space
cases use the same multiplier at every scale (uniform-scale optical depth is scale
invariant); World-space cases multiply it by that volume density instead.
Writes DIR/results_DEVICE[_motion].json; exits nonzero if any case's mean |deep - beauty|
exceeds the tolerance or |mean deep - mean beauty| exceeds a quarter of it.
--motion also gives each volume a translating transform during the shutter
(object transform motion; the volume density factor is static, as in beauty).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

parser = argparse.ArgumentParser()
parser.add_argument('--vdb', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--device', choices=('CPU', 'CUDA', 'OPTIX'), default='CPU')
parser.add_argument('--scales', nargs='+', default=['0.00375', '2.0', '0.25,1,0.5'],
                    help='Uniform S or non-uniform X,Y,Z object scales')
parser.add_argument('--spaces', nargs='+', default=['OBJECT', 'WORLD'])
parser.add_argument('--density', type=float, default=2.0)
parser.add_argument('--samples', type=int, default=256)
parser.add_argument('--size', type=int, default=32)
parser.add_argument('--tolerance', type=float, default=0.008)
parser.add_argument('--motion', action='store_true')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
source = args.vdb.resolve(strict=True)
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()


def read_deep_flat(path):
    import numpy as np
    import OpenImageIO as oiio
    image = oiio.ImageInput.open(str(path))
    if image is None:
        raise RuntimeError(oiio.geterror())
    spec = image.spec()
    data = image.read_native_deep_image()
    image.close()
    alpha = spec.channelnames.index('A')
    flat = np.zeros((spec.height, spec.width))
    for p in range(spec.width * spec.height):
        flat[p // spec.width, p % spec.width] = 1 - math.prod(
            1 - data.deep_value(p, alpha, s) for s in range(data.samples(p)))
    return flat


def read_beauty_alpha(path):
    import numpy as np
    import OpenImageIO as oiio
    buf = oiio.ImageBuf(str(path))
    pixels = buf.get_pixels(oiio.FLOAT)
    alpha = buf.spec().channelnames.index('A')
    return np.asarray(pixels[:, :, alpha], dtype=float)


def render_case(scale, space):
    import numpy as np
    xyz = [float(v) for v in scale.split(',')] if ',' in scale else [float(scale)] * 3
    if len(xyz) != 3 or not all(math.isfinite(v) and v > 0 for v in xyz):
        raise ValueError('Invalid scale ' + scale)
    # Cycles' scalar object-space volume density (scene/object.cpp).
    unit = Vector((1, 1, 1)).normalized()
    volume_density = 1 / Vector([xyz[i] * unit[i] for i in range(3)]).length
    label = f'{space.lower()}_{scale.replace(",", "x")}' + ('_motion' if args.motion else '')
    directory = root / args.device / label
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob('*.exr'):
        stale.unlink()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    volume = bpy.data.volumes.new('VDB')
    volume.filepath = source.as_posix()
    volume.render.precision = 'FULL'
    volume.render.space = space
    obj = bpy.data.objects.new('VDB', volume)
    scene.collection.objects.link(obj)
    if not volume.grids.load() or 'density' not in {g.name for g in volume.grids}:
        raise RuntimeError('Expected a VDB with a density grid')
    obj.scale = xyz
    material = bpy.data.materials.new('Absorb')
    material.use_nodes = True
    material.cycles.volume_interpolation = 'LINEAR'
    nodes = material.node_tree.nodes
    nodes.clear()
    attribute = nodes.new('ShaderNodeAttribute')
    attribute.attribute_name = 'density'
    multiply = nodes.new('ShaderNodeMath')
    multiply.operation = 'MULTIPLY'
    multiply.inputs[1].default_value = args.density * (1 if space == 'OBJECT' else volume_density)
    absorption = nodes.new('ShaderNodeVolumeAbsorption')
    absorption.inputs['Color'].default_value = (0, 0, 0, 1)
    output = nodes.new('ShaderNodeOutputMaterial')
    links = material.node_tree.links
    links.new(attribute.outputs['Fac'], multiply.inputs[0])
    links.new(multiply.outputs[0], absorption.inputs['Density'])
    links.new(absorption.outputs['Volume'], output.inputs['Volume'])
    volume.materials.append(material)
    bpy.context.view_layer.update()
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lower = Vector([min(p[i] for p in corners) for i in range(3)])
    upper = Vector([max(p[i] for p in corners) for i in range(3)])
    center, radius = (lower + upper) / 2, (upper - lower).length / 2
    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    scene.camera = camera
    distance = 1.1 * radius / math.sin(camera.data.angle_x / 2)
    camera.location = center + Vector((0.25, -1, 0.15)).normalized() * distance
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.clip_start = max(1e-6, distance - 1.5 * radius)
    camera.data.clip_end = distance + 1.5 * radius
    if args.motion:
        # Translate by ~5% of the bound during the shutter; density factor is static.
        scene.render.use_motion_blur = True
        for frame, offset in ((0, -0.05), (1, 0.0), (2, 0.05)):
            obj.location = Vector((offset * radius, 0, 0))
            obj.keyframe_insert('location', frame=frame)
        scene.frame_set(1)
    world = bpy.data.worlds.new('White')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    scene.world = world
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    used = ['CPU']
    if args.device != 'CPU':
        preferences = bpy.context.preferences.addons['cycles'].preferences
        preferences.compute_device_type = args.device
        preferences.refresh_devices()
        candidates = [d for d in preferences.devices if d.type == args.device]
        if not candidates:
            raise RuntimeError('No ' + args.device + ' device')
        for d in preferences.devices:
            d.use = d.id == candidates[0].id
        scene.cycles.device = 'GPU'
        used = [d.name for d in preferences.devices if d.use]
    scene.cycles.samples = args.samples
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.pixel_filter_type = 'BOX'
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = args.size
    scene.render.resolution_percentage = 100
    scene.render.use_compositing = False
    scene.render.use_sequencer = False
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.render.filepath = str(directory / 'beauty.exr')
    layer = bpy.context.view_layer
    if hasattr(layer.cycles, 'use_pass_deep'):
        layer.cycles.use_pass_deep = True
        scene.cycles.use_deep_output = False
    else:
        scene.cycles.use_deep_output = True
    scene.cycles.use_deep_volume = True
    scene.cycles.deep_error = 0.001
    scene.cycles.deep_output_path = str(directory / 'deep_####')
    scene.cycles.deep_max_events = 64
    scene.cycles.deep_memory_mb = 2048
    if bpy.ops.render.render(write_still=True) != {'FINISHED'}:
        raise RuntimeError('Render did not finish')
    deep_files = sorted(directory.glob('deep_*.deep.exr'))
    if len(deep_files) != 1:
        raise RuntimeError(f'Expected one deep EXR in {directory}')
    deep = read_deep_flat(deep_files[0])
    beauty = read_beauty_alpha(directory / 'beauty.exr')
    if deep.shape != beauty.shape:
        raise RuntimeError('Deep and beauty sizes differ')
    error = np.abs(deep - beauty)
    return {
        'case': label, 'scale': xyz, 'space': space, 'device': args.device, 'devices': used,
        'multiplier': multiply.inputs[1].default_value,
        'beauty_alpha_mean': float(beauty.mean()), 'deep_alpha_mean': float(deep.mean()),
        'bias': float(deep.mean() - beauty.mean()), 'mean_abs_error': float(error.mean()), 'max_abs_error': float(error.max()),
        'passed': bool(error.mean() <= args.tolerance and
                       abs(deep.mean() - beauty.mean()) <= args.tolerance / 4),
    }


results = [render_case(scale, space) for scale in args.scales for space in args.spaces]
if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('VDB changed')
report = {'vdb': source.as_posix(), 'vdb_sha256': source_hash, 'tolerance': args.tolerance,
          'renderer': bpy.app.binary_path, 'results': results,
          'passed': all(r['passed'] for r in results)}
suffix = '_motion' if args.motion else ''
(root / f'results_{args.device}{suffix}.json').write_text(json.dumps(report, indent=2) + '\n')
for r in results:
    print('VDB_DENSITY', json.dumps(r))
print('VDB_DENSITY_PASS' if report['passed'] else 'VDB_DENSITY_FAIL', flush=True)
sys.exit(0 if report['passed'] else 1)
