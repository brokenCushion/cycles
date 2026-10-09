"""Export the recorded linear beauty using the supplied Blender scene's view settings.

blender --background SCENE --python SCRIPT -- RENDER_DIRECTORY
The source scene is never saved.
"""
import bpy
import hashlib
import json
from pathlib import Path
import sys

directory = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
settings = json.loads((directory/'render.json').read_text())
source = Path(bpy.data.filepath)
if hashlib.sha256(source.read_bytes()).hexdigest() != settings['source_sha256']:
    raise RuntimeError('Preview scene differs from the recorded render source')
scene = bpy.context.scene
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '8'
beauty = bpy.data.images.load(str(directory/'beauty.exr'), check_existing=False)
beauty.save_render(str(directory/'beauty.png'), scene=scene)
print('BLENDER_BEAUTY_PREVIEW', directory/'beauty.png', scene.view_settings.view_transform)
