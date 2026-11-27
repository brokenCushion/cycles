# SPDX-License-Identifier: Apache-2.0
"""Check deep alpha against beauty alpha for a ground plane coplanar with a volume box's floor.

blender --factory-startup --background --python SCRIPT -- --output DIR
  [--device CPU|CUDA|OPTIX] [--offsets -1e-3 0 1e-3] [--samples 128] [--size 48]
  [--density 0.5] [--tolerance 0.01] [--motion] [--scatter]

A 2 m cube (bottom face at z=0) with a homogeneous black Volume Absorption
(--scatter: Absorption + Scatter, half each) sits on an
opaque diffuse plane at z=OFFSET; the camera looks down through the cube at the
plane. Each case builds a fresh in-memory scene (no .blend is read or saved),
renders beauty with a transparent film plus deep output, and compares flattened
deep alpha 1 - prod(1 - A) with beauty alpha, over all pixels and over the pixels
where the plane is seen through the volume's floor ("floor" mask).
Non-coplanar offsets must match beauty: a case fails if mean |deep - beauty|
exceeds the tolerance or |mean deep - mean beauty| exceeds a quarter of it, on
either mask. At offset 0 native beauty is not a reference: its closest-hit query
returns either the cube's exit face or the plane at the same FLOAT t, and after
the exit face the next segment starts past t, so the plane is lost for those rays
(stock Cycles; CPU loses some rays, OptiX all of them). Deep's own visibility
chain keeps every crossing at a tied t and always sees the plane. The coplanar
case therefore requires every floor pixel's deep alpha to be opaque (>= 1 - tol)
and reports beauty's loss as 'beauty_ground_loss'.
Writes DIR/results_DEVICE[_motion].json; exits nonzero on any failed case.
--motion slides the plane and cube together along x during the shutter.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--device', choices=('CPU', 'CUDA', 'OPTIX'), default='CPU')
parser.add_argument('--offsets', nargs='+', type=float, default=[-1e-3, 0.0, 1e-3])
parser.add_argument('--density', type=float, default=0.5)
parser.add_argument('--samples', type=int, default=128)
parser.add_argument('--size', type=int, default=48)
parser.add_argument('--tolerance', type=float, default=0.01)
parser.add_argument('--motion', action='store_true')
parser.add_argument('--scatter', action='store_true', help='Absorption + Scatter instead of pure absorption')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)


def read_deep(path):
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
    count = np.zeros((spec.height, spec.width), dtype=int)
    for p in range(spec.width * spec.height):
        n = data.samples(p)
        count[p // spec.width, p % spec.width] = n
        flat[p // spec.width, p % spec.width] = 1 - math.prod(
            1 - data.deep_value(p, alpha, s) for s in range(n))
    # Deep EXR rows are top-down; Blender float buffers too once read through OIIO.
    return flat, count


def read_beauty_alpha(path):
    import numpy as np
    import OpenImageIO as oiio
    buf = oiio.ImageBuf(str(path))
    pixels = buf.get_pixels(oiio.FLOAT)
    alpha = buf.spec().channelnames.index('A')
    return np.asarray(pixels[:, :, alpha], dtype=float)


def floor_mask(scene, camera, offset, size):
    """Pixels whose whole footprint sees the plane through the cube's floor (|x|,|y| < 0.9)."""
    import numpy as np
    frame = [camera.matrix_world @ v for v in camera.data.view_frame(scene=scene)]
    # view_frame order: top-right, bottom-right, bottom-left, top-left.
    tr, br, bl, tl = frame
    origin = camera.matrix_world.translation
    mask = np.zeros((size, size), dtype=bool)
    for row in range(size):
        for col in range(size):
            hit_all = True
            for du, dv in ((0, 0), (1, 0), (0, 1), (1, 1)):
                u, v = (col + du) / size, (row + dv) / size
                top = tl.lerp(tr, u)
                bottom = bl.lerp(br, u)
                point = top.lerp(bottom, v)
                d = point - origin
                t = (offset - origin.z) / d.z
                p = origin + d * t
                if not (abs(p.x) < 0.9 and abs(p.y) < 0.9):
                    hit_all = False
            mask[row, col] = hit_all
    return mask


def material(name, build):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    build(nt, nt.nodes.new('ShaderNodeOutputMaterial'))
    return m


def render_case(offset):
    import numpy as np
    label = f'offset_{offset:+.0e}'.replace('+0e+00', '0') + ('_motion' if args.motion else '')
    directory = root / args.device / label
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob('*.exr'):
        stale.unlink()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    def ground(nt, out):
        d = nt.nodes.new('ShaderNodeBsdfDiffuse')
        d.inputs['Color'].default_value = (.3, .28, .25, 1)
        nt.links.new(d.outputs[0], out.inputs['Surface'])

    def mist(nt, out):
        a = nt.nodes.new('ShaderNodeVolumeAbsorption')
        a.inputs['Color'].default_value = (0, 0, 0, 1)
        if not args.scatter:
            a.inputs['Density'].default_value = args.density
            nt.links.new(a.outputs[0], out.inputs['Volume'])
            return
        a.inputs['Density'].default_value = args.density / 2
        v = nt.nodes.new('ShaderNodeVolumeScatter')
        v.inputs['Density'].default_value = args.density / 2
        add = nt.nodes.new('ShaderNodeAddShader')
        nt.links.new(a.outputs[0], add.inputs[0])
        nt.links.new(v.outputs[0], add.inputs[1])
        nt.links.new(add.outputs[0], out.inputs['Volume'])

    bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, offset))
    plane = bpy.context.object
    plane.data.materials.append(material('ground', ground))
    bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 1))
    cube = bpy.context.object
    cube.data.materials.append(material('mist', mist))
    if args.motion:
        scene.render.use_motion_blur = True
        for obj, z in ((plane, offset), (cube, 1.0)):
            for frame, x in ((0, -0.05), (1, 0.0), (2, 0.05)):
                obj.location = (x, 0, z)
                obj.keyframe_insert('location', frame=frame)
        scene.frame_set(1)

    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.location = (2.2, -3.0, 4.2)
    camera.rotation_euler = (Vector((0, 0, 0.2)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.lens = 40
    world = bpy.data.worlds.new('World')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (.2, .2, .2, 1)
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
    deep, count = read_deep(deep_files[0])
    beauty = read_beauty_alpha(directory / 'beauty.exr')
    if deep.shape != beauty.shape:
        raise RuntimeError('Deep and beauty sizes differ')
    mask = floor_mask(scene, camera, offset, args.size)
    if not mask.any():
        raise RuntimeError('Empty floor mask')
    result = {'case': label, 'offset': offset, 'device': args.device, 'devices': used,
              'floor_pixels': int(mask.sum())}
    passed = True
    for name, m in (('all', np.ones_like(mask)), ('floor', mask)):
        d, b = deep[m], beauty[m]
        error = np.abs(d - b)
        result[name] = {'beauty_alpha_mean': float(b.mean()), 'deep_alpha_mean': float(d.mean()),
                        'bias': float(d.mean() - b.mean()), 'mean_abs_error': float(error.mean()),
                        'max_abs_error': float(error.max()), 'mean_samples': float(count[m].mean())}
        if offset != 0:
            passed &= bool(error.mean() <= args.tolerance and abs(d.mean() - b.mean()) <= args.tolerance / 4)
    if offset == 0:
        result['beauty_ground_loss'] = float(1 - beauty[mask].mean())
        passed = bool(deep[mask].min() >= 1 - args.tolerance)
    result['passed'] = passed
    return result


results = [render_case(offset) for offset in args.offsets]
report = {'tolerance': args.tolerance, 'renderer': bpy.app.binary_path, 'density': args.density,
          'scatter': args.scatter, 'results': results, 'passed': all(r['passed'] for r in results)}
suffix = '_motion' if args.motion else ''
(root / f'results_{args.device}{suffix}.json').write_text(json.dumps(report, indent=2) + '\n')
for r in results:
    print('COPLANAR_FLOOR', json.dumps(r))
print('COPLANAR_FLOOR_PASS' if report['passed'] else 'COPLANAR_FLOOR_FAIL', flush=True)
sys.exit(0 if report['passed'] else 1)
