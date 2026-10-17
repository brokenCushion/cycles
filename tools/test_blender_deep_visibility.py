"""Blender -b --factory-startup --disable-autoexec --python SCRIPT -- FRESH_OUTPUT."""
import json
from pathlib import Path
import sys

import bpy

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=False)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.render.resolution_x = scene.render.resolution_y = 16
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.use_compositing = scene.render.use_sequencer = False
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.filepath = str(root / 'beauty_####')
scene.cycles.samples = 1
scene.cycles.use_denoising = scene.cycles.use_adaptive_sampling = False
scene.cycles.deep_error = .001
scene.cycles.deep_max_events = 64
scene.cycles.deep_memory_mb = 512
scene.world = bpy.data.worlds.new('World')
scene.world.use_nodes = True
bpy.ops.object.camera_add(location=(0, 0, 8))
scene.camera = bpy.context.view_layer.objects.active
scene.camera.data.type = 'ORTHO'
scene.camera.data.ortho_scale = 6
bpy.ops.mesh.primitive_plane_add(size=6)
back = bpy.context.view_layer.objects.active
back.name = 'Back'
collection = bpy.data.collections.new('Hair collection')
scene.collection.children.link(collection)
bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 2))
emitter = bpy.context.view_layer.objects.active
emitter.name = 'Hair emitter'
for owner in list(emitter.users_collection):
    owner.objects.unlink(emitter)
collection.objects.link(emitter)
material = bpy.data.materials.new('Hair')
material.use_nodes = True
nodes = material.node_tree.nodes
nodes.clear()
hair = nodes.new('ShaderNodeBsdfHair')
output = nodes.new('ShaderNodeOutputMaterial')
material.node_tree.links.new(hair.outputs[0], output.inputs['Surface'])
emitter.data.materials.append(material)
bpy.ops.object.particle_system_add()
particles = emitter.particle_systems[-1].settings
particles.type = 'HAIR'
particles.count = 8
particles.hair_length = .2
particles.material = 1
a = bpy.context.view_layer
a.name = 'A'
b = scene.view_layers.new('B')
results = {}


def render(name, layer, reject=False):
    for candidate in scene.view_layers:
        candidate.use = candidate == layer
        candidate.cycles.use_pass_deep = candidate == layer
    directory = root / name
    scene.cycles.deep_output_path = str(directory / '{view_layer}_####')
    try:
        bpy.ops.render.render()
    except RuntimeError as error:
        assert reject and 'Deep: unsupported deep surface node: hair_bsdf' in str(error), str(error)
        assert not list(directory.glob('*.deep.exr'))
        results[name] = str(error).strip()
    else:
        assert not reject, 'Camera-visible Hair BSDF unexpectedly accepted'
        path = directory / f'{layer.name}_0001.deep.exr'
        assert path.is_file() and path.stat().st_size
        results[name] = str(path)
    assert not list(directory.glob('*.partial*'))
    print('DEEP_VISIBILITY_CASE_PASS', name, flush=True)


a.layer_collection.children[collection.name].indirect_only = True
render('indirect-only-A', a)
render('visible-B', b, reject=True)
a.layer_collection.children[collection.name].indirect_only = False
emitter.visible_camera = False
render('camera-off', a)
emitter.visible_camera = True
render('camera-on', a, reject=True)
# Use a plain mesh pair so modifiers cannot split the shared datablock at sync.
bpy.context.view_layer.objects.active = emitter
bpy.ops.object.particle_system_remove()
emitter.visible_camera = False
instance = bpy.data.objects.new('Visible shared mesh', emitter.data)
scene.collection.objects.link(instance)
assert instance.data is emitter.data and instance.visible_camera
render('shared-visible', a, reject=True)
bpy.data.objects.remove(instance, do_unlink=True)
emitter.visible_camera = True
a.layer_collection.children[collection.name].holdout = True
render('holdout', a, reject=True)
a.layer_collection.children[collection.name].holdout = False
a.layer_collection.children[collection.name].exclude = True
render('excluded', a)
(root / 'results.json').write_text(json.dumps(dict(passed=True, tests=results), indent=2))
print('DEEP_VISIBILITY_TESTS_PASS', len(results), 'cases', flush=True)
