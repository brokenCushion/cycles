# SPDX-License-Identifier: Apache-2.0
"""Create a bounded two-object deepID fixture; run with Blender --python ... -- PATH."""
from pathlib import Path
import sys
import bpy

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.render.resolution_x = 16
scene.render.resolution_y = 12
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.cycles.samples = 1
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.cycles.pixel_filter_type = 'BOX'
scene.cycles.filter_width = 1
scene.render.use_compositing = False
scene.render.use_sequencer = False
bpy.ops.object.camera_add()
camera = bpy.context.object
camera.name = 'IDCamera'
camera.data.lens = 5000
camera.data.sensor_width = 1
camera.data.clip_start = .125
camera.data.clip_end = 20
scene.camera = camera
for name, front, back, density in [('KnownFogA', 2, 8, .3), ('KnownFogB', 4, 10, .2)]:
    bpy.ops.mesh.primitive_cube_add(location=(0,0,-(front+back)/2))
    obj = bpy.context.object
    obj.name = name
    obj.scale = (100,100,(back-front)/2)
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    absorption = nodes.new('ShaderNodeVolumeAbsorption')
    absorption.inputs['Color'].default_value = (0,0,0,1)
    absorption.inputs['Density'].default_value = density
    material.node_tree.links.new(absorption.outputs['Volume'],output.inputs['Volume'])
    obj.data.materials.append(material)
path = Path(sys.argv[sys.argv.index('--')+1]).resolve()
path.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(path))
