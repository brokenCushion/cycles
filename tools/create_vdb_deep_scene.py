# SPDX-License-Identifier: Apache-2.0
"""Create the native VDB acceptance scene without modifying the supplied asset.

blender --factory-startup --background --python SCRIPT -- --vdb FILE --output DIR
Creates scene.blend and scene.json; it does not claim or enable deep support.
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
parser.add_argument('--density-scale', type=float, default=0.02)
parser.add_argument('--resolution', type=int, default=256)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not math.isfinite(args.density_scale) or args.density_scale <= 0:
    raise ValueError('Density scale must be finite and positive')
if not 16 <= args.resolution <= 2048:
    raise ValueError('Resolution must be between 16 and 2048')
source = args.vdb.resolve(strict=True)
directory = args.output.resolve()
directory.mkdir(parents=True, exist_ok=True)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
volume = bpy.data.volumes.new('SuppliedVDB')
volume.filepath = source.as_posix()
obj = bpy.data.objects.new('SuppliedVDB', volume)
scene.collection.objects.link(obj)
if not volume.grids.load():
    raise RuntimeError('Native Blender VDB metadata load failed')
grids = {grid.name: grid for grid in volume.grids}
if 'density' not in grids or grids['density'].data_type != 'FLOAT':
    raise RuntimeError('Expected the supplied FLOAT density grid')

material = bpy.data.materials.new('ScalarDensityAbsorption')
material.use_nodes = True
material.cycles.volume_interpolation = 'LINEAR'
nodes = material.node_tree.nodes
nodes.clear()
attribute = nodes.new('ShaderNodeAttribute')
attribute.attribute_name = 'density'
multiply = nodes.new('ShaderNodeMath')
multiply.operation = 'MULTIPLY'
multiply.inputs[1].default_value = args.density_scale
absorption = nodes.new('ShaderNodeVolumeAbsorption')
absorption.inputs['Color'].default_value = (0, 0, 0, 1)
output = nodes.new('ShaderNodeOutputMaterial')
links = material.node_tree.links
links.new(attribute.outputs['Fac'], multiply.inputs[0])
links.new(multiply.outputs[0], absorption.inputs['Density'])
links.new(absorption.outputs['Volume'], output.inputs['Volume'])
volume.materials.append(material)

bpy.context.view_layer.update()
evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
corners = [evaluated.matrix_world @ Vector(corner) for corner in evaluated.bound_box]
lower = Vector(tuple(min(p[i] for p in corners) for i in range(3)))
upper = Vector(tuple(max(p[i] for p in corners) for i in range(3)))
center = (lower + upper) / 2
radius = (upper - lower).length / 2
if not math.isfinite(radius) or radius <= 0:
    raise RuntimeError('Native VDB evaluated bounds are empty or invalid')
camera_data = bpy.data.cameras.new('VDBCamera')
camera = bpy.data.objects.new('VDBCamera', camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = 'PERSP'
camera_data.lens = 50
camera_data.sensor_fit = 'HORIZONTAL'
distance = 1.15 * radius / math.sin(camera_data.angle_x / 2)
camera.location = center + Vector((0.25, -1, 0.15)).normalized() * distance
camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera_data.clip_start = max(0.001, distance - radius * 1.5)
camera_data.clip_end = distance + radius * 1.5
camera_data.dof.use_dof = False
world = bpy.data.worlds.new('WhiteTransmittanceReference')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
scene.world = world
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 1
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.cycles.pixel_filter_type = 'BOX'
scene.render.resolution_x = scene.render.resolution_y = args.resolution
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
scene.render.filepath = (directory / 'beauty.exr').as_posix()
bpy.ops.wm.save_as_mainfile(filepath=(directory / 'scene.blend').as_posix())
if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Supplied VDB changed during scene creation')
report = {
    'source': source.as_posix(), 'source_sha256': source_hash,
    'grids': {name: {'type': grid.data_type,
                     'matrix_object': [list(row) for row in grid.matrix_object]}
              for name, grid in grids.items()},
    'bounds_world': [list(lower), list(upper)],
    'density_scale': args.density_scale,
    'resolution': [args.resolution, args.resolution], 'samples': 1,
    'material': 'density * scale, scalar absorption; no scattering/emission',
    'deep_enabled': False,
    'scene': (directory / 'scene.blend').as_posix(),
}
(directory / 'scene.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
