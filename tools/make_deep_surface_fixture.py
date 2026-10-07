# SPDX-License-Identifier: Apache-2.0
"""Blender --background --python TOOL -- OUTPUT.blend: sloped 1024-ray fixture."""
import bpy
import math
from pathlib import Path
import sys

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_plane_add(size=100, rotation=(0, math.radians(35), 0))
bpy.context.object.name = 'SlopedSurface'
bpy.ops.object.camera_add(location=(0, 0, 10))
scene = bpy.context.scene
scene.camera = bpy.context.object
scene.camera.data.lens = 50
scene.render.engine = 'CYCLES'
scene.render.resolution_x = 16
scene.render.resolution_y = 8
scene.render.resolution_percentage = 100
scene.cycles.samples = 1024
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.world.color = (.1, .1, .1)
output = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(output))
