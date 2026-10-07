# SPDX-License-Identifier: Apache-2.0
"""Create fixed holdout/ordinary pairs: Blender --python SCRIPT -- DIRECTORY."""
import json
from pathlib import Path
import sys
import bpy

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=True)
cases = {}
definitions = [(kind, position, .6 if kind.endswith('_cutout') else 1.)
               for kind, position in [('object', 'front'), ('object', 'inside'),
                                      ('material', 'front'), ('material', 'inside'),
                                      ('object_cutout', 'inside'), ('material_cutout', 'inside')]]
for kind, position, opacity in definitions:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.render.resolution_x = scene.render.resolution_y = 16
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.cycles.samples = 16
    scene.cycles.seed = 123
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.pixel_filter_type = 'BOX'
    scene.cycles.filter_width = 1
    scene.render.use_compositing = scene.render.use_sequencer = False
    world = bpy.data.worlds.new('World')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (.1, .1, .1, 1)
    scene.world = world
    bpy.ops.object.camera_add(location=(0, 0, 10))
    scene.camera = bpy.context.object
    scene.camera.name = 'HoldoutCamera'
    scene.camera.data.lens = 50
    scene.camera.data.clip_start = .125
    scene.camera.data.clip_end = 30
    bpy.ops.mesh.primitive_cube_add(size=4)
    fog = bpy.context.object
    fog.name = 'Fog'
    material = bpy.data.materials.new('Absorption')
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    absorption = nodes.new('ShaderNodeVolumeAbsorption')
    absorption.inputs['Color'].default_value = (0, 0, 0, 1)
    absorption.inputs['Density'].default_value = .3
    material.node_tree.links.new(absorption.outputs['Volume'], output.inputs['Volume'])
    fog.data.materials.append(material)
    bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, 3 if position == 'front' else 0))
    probe = bpy.context.object
    probe.name = 'HoldoutPlane'
    material = bpy.data.materials.new('ProbeMaterial')
    material.use_nodes = True
    probe.data.materials.append(material)

    def surface(holdout):
        nodes = material.node_tree.nodes
        nodes.clear()
        output = nodes.new('ShaderNodeOutputMaterial')
        closure = nodes.new('ShaderNodeHoldout' if holdout else 'ShaderNodeBsdfDiffuse')
        if opacity < 1:
            transparent = nodes.new('ShaderNodeBsdfTransparent')
            mix = nodes.new('ShaderNodeMixShader')
            mix.inputs[0].default_value = 1 - opacity
            material.node_tree.links.new(closure.outputs[0], mix.inputs[1])
            material.node_tree.links.new(transparent.outputs[0], mix.inputs[2])
            closure = mix
        material.node_tree.links.new(closure.outputs[0], output.inputs['Surface'])

    # A backing plane also exercises visibility after a partial holdout.
    bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, -4))
    bpy.context.object.name = 'BackingPlane'
    backing = bpy.data.materials.new('Backing')
    backing.use_nodes = True
    bpy.context.object.data.materials.append(backing)
    name = kind + '_' + position
    directory = root / name
    directory.mkdir()
    surface(False)
    ordinary = directory / 'ordinary.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(ordinary))
    probe.is_holdout = kind.startswith('object')
    surface(kind.startswith('material'))
    holdout = directory / 'holdout.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(holdout))
    cases[name] = dict(ordinary=ordinary.as_posix(), holdout=holdout.as_posix(),
                       position=position, opacity=opacity, holdout_name=probe.name)
    if name == 'object_front':
        probe.is_holdout = False
        probe.is_shadow_catcher = True
        shadow = root / 'reject_shadow.blend'
        bpy.ops.wm.save_as_mainfile(filepath=str(shadow))
        probe.is_shadow_catcher = False
        probe.cycles.is_caustics_caster = True
        caustics = root / 'reject_caustics.blend'
        bpy.ops.wm.save_as_mainfile(filepath=str(caustics))
(root / 'cases.json').write_text(json.dumps(dict(cases=cases), indent=2) + '\n')
