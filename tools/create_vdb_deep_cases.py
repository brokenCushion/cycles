# SPDX-License-Identifier: Apache-2.0
"""Create CPU/CUDA coverage cases from the supplied VDB fixture, without changing it.

blender --background --python SCRIPT -- --source scene.blend --output DIRECTORY
These small fixtures qualify combinations; they are not production-scale tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
from mathutils import Vector

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--resolution', type=int, default=96)
parser.add_argument('--width', type=int)
parser.add_argument('--height', type=int)
parser.add_argument('--cases', nargs='+',
                    choices=('mixed_surface', 'overlapping_grids', 'camera_inside',
                             'far_clip_inside', 'overlap_first_only', 'overlap_second_only', 'scattering',
                             'transformed_grid', 'gaussian_filter', 'blackman_harris_filter', 'zero_extinction',
                             'adaptive_volume', 'camera_ray_depth', 'textured_world',
                             'combined_boundary', 'split_boundary_reference',
                             'surface_ao', 'opaque_mix_ao', 'mirrored_surface', 'expanded_capacity',
                             'denoised_volume', 'adaptive_denoised_volume', 'opaque_foreground',
                             'reject_ao_opacity',
                             'reject_dof', 'reject_motion',
                             'reject_orthographic', 'reject_cubic', 'half_precision',
                             'reject_color', 'reject_nonlinear', 'reject_reflection'),
                    default=('mixed_surface', 'overlapping_grids', 'camera_inside', 'far_clip_inside'))
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
width = args.resolution if args.width is None else args.width
height = args.resolution if args.height is None else args.height
if not all(16 <= v <= 2048 for v in (width, height)):
    raise ValueError('Width and height must be between 16 and 2048')
source = args.source.resolve(strict=True)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
manifest = {'source': str(source), 'source_sha256': source_hash, 'cases': {}}

for case in args.cases:
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    grid = bpy.data.objects['SuppliedVDB']
    bpy.context.view_layer.update()
    evaluated = grid.evaluated_get(bpy.context.evaluated_depsgraph_get())
    corners = [evaluated.matrix_world @ Vector(p) for p in evaluated.bound_box]
    center = sum(corners, Vector()) / 8
    extent = max((p-center).length for p in corners)
    camera = scene.camera
    right = camera.rotation_euler.to_matrix() @ Vector((1, 0, 0))
    if case in ('mixed_surface', 'surface_ao', 'opaque_mix_ao', 'mirrored_surface', 'reject_ao_opacity'):
        bpy.ops.mesh.primitive_cube_add(size=1, location=center)
        surface = bpy.context.object
        surface.name = 'HalfTransparentOccluder'
        surface.rotation_euler = camera.rotation_euler
        surface.scale = (extent*.45, extent*.65, extent*.06)
        if case == 'mirrored_surface':
            surface.scale.x *= -1
        material = bpy.data.materials.new('ScalarHalfTransparency')
        material.use_nodes = True
        nodes = material.node_tree.nodes
        nodes.clear()
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        diffuse = nodes.new('ShaderNodeBsdfDiffuse')
        mix = nodes.new('ShaderNodeMixShader')
        mix.inputs[0].default_value = .5
        output = nodes.new('ShaderNodeOutputMaterial')
        links = material.node_tree.links
        links.new(transparent.outputs[0], mix.inputs[1])
        links.new(diffuse.outputs[0], mix.inputs[2])
        links.new(mix.outputs[0], output.inputs['Surface'])
        surface.data.materials.append(material)
        if case in ('surface_ao', 'opaque_mix_ao', 'reject_ao_opacity'):
            ao = nodes.new('ShaderNodeAmbientOcclusion')
            links.new(ao.outputs['Color'], diffuse.inputs['Color'])
            if case in ('opaque_mix_ao', 'reject_ao_opacity'):
                links.new(ao.outputs['AO'], mix.inputs[0])
            if case == 'opaque_mix_ao':
                opaque = nodes.new('ShaderNodeBsdfDiffuse')
                links.new(opaque.outputs[0], mix.inputs[1])
                nodes.remove(transparent)
    elif case == 'opaque_foreground':
        forward = camera.rotation_euler.to_matrix() @ Vector((0, 0, -1))
        bpy.ops.mesh.primitive_plane_add(size=extent * 8,
            location=camera.location + forward * camera.data.clip_start * 1.05)
        surface = bpy.context.object
        surface.rotation_euler = camera.rotation_euler
        material = bpy.data.materials.new('OpaqueForeground')
        material.use_nodes = True
        surface.data.materials.append(material)
    elif case == 'overlapping_grids':
        other = grid.copy()
        other.name = 'OffsetVDB'
        other.location += right * extent * .15
        scene.collection.objects.link(other)
    elif case == 'overlap_second_only':
        # Cycles adjust_volume_tfm() adds a name-hashed offset. Preserve the
        # combined object's name so an isolated render samples the same field.
        grid.name = 'OffsetVDB'
        grid.location += right * extent * .15
    elif case in ('scattering', 'camera_ray_depth', 'denoised_volume', 'adaptive_denoised_volume'):
        material = grid.data.materials[0]
        nodes, links = material.node_tree.nodes, material.node_tree.links
        absorption = next(n for n in nodes if n.bl_idname == 'ShaderNodeVolumeAbsorption')
        density = absorption.inputs['Density'].links[0].from_socket
        output = absorption.outputs['Volume'].links[0].to_socket
        scatter = nodes.new('ShaderNodeVolumeScatter')
        scatter.inputs['Color'].default_value = (.5, .5, .5, 1)
        scatter.inputs['Anisotropy'].default_value = .6
        # Match the original black absorption's extinction with half-grey
        # scattering at twice the density; beauty is expected to differ.
        multiply = nodes.new('ShaderNodeMath')
        multiply.operation = 'MULTIPLY'
        multiply.inputs[1].default_value = 2
        links.new(density, multiply.inputs[0])
        links.new(multiply.outputs[0], scatter.inputs['Density'])
        links.new(scatter.outputs['Volume'], output)
        nodes.remove(absorption)
        if case == 'camera_ray_depth':
            path = nodes.new('ShaderNodeLightPath')
            power = nodes.new('ShaderNodeMath')
            power.operation = 'POWER'
            power.inputs[0].default_value = .65
            links.new(path.outputs['Ray Depth'], power.inputs[1])
            camera_density = nodes.new('ShaderNodeMath')
            camera_density.operation = 'MULTIPLY'
            links.new(multiply.outputs[0], camera_density.inputs[0])
            links.new(power.outputs[0], camera_density.inputs[1])
            links.new(camera_density.outputs[0], scatter.inputs['Density'])
            depth = nodes.new('ShaderNodeMath')
            depth.operation = 'ADD'
            depth.inputs[1].default_value = 1
            links.new(path.outputs['Ray Depth'], depth.inputs[0])
            phase = nodes.new('ShaderNodeMath')
            phase.operation = 'POWER'
            phase.inputs[0].default_value = .9
            links.new(depth.outputs[0], phase.inputs[1])
            links.new(phase.outputs[0], scatter.inputs['Anisotropy'])
    elif case == 'textured_world':
        tree = scene.world.node_tree
        background = next(n for n in tree.nodes if n.type == 'BACKGROUND')
        texture = tree.nodes.new('ShaderNodeTexEnvironment')
        image = bpy.data.images.new('DeepWorldFixture', width=8, height=4, float_buffer=True)
        image.pixels = [v for y in range(4) for x in range(8)
                        for v in (.05 + x*.03, .1 + y*.02, .2, 1)]
        image.pack()
        texture.image = image
        tree.links.new(texture.outputs['Color'], background.inputs['Color'])
    elif case in ('combined_boundary', 'split_boundary_reference'):
        bpy.ops.mesh.primitive_cube_add(size=extent*.02, location=center)
        boundary = bpy.context.object
        boundary.name = 'CombinedBoundary'
        material = bpy.data.materials.new('CombinedBoundaryMaterial')
        material.use_nodes = True
        nodes, links = material.node_tree.nodes, material.node_tree.links
        nodes.clear()
        output = nodes.new('ShaderNodeOutputMaterial')
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        transparent.inputs['Color'].default_value = (.5, .5, .5, 1)
        links.new(transparent.outputs[0], output.inputs['Surface'])
        absorption = nodes.new('ShaderNodeVolumeAbsorption')
        scatter = nodes.new('ShaderNodeVolumeScatter')
        for node in (absorption, scatter):
            node.inputs['Color'].default_value = (.025259342, .485081881, .637839675, 1)
            node.inputs['Density'].default_value = .06
        add = nodes.new('ShaderNodeAddShader')
        links.new(absorption.outputs[0], add.inputs[0])
        links.new(scatter.outputs[0], add.inputs[1])
        links.new(add.outputs[0], output.inputs['Volume'])
        boundary.data.materials.append(material)
        if case == 'split_boundary_reference':
            separate = boundary.copy()
            separate.data = boundary.data.copy()
            scene.collection.objects.link(separate)
            pure_volume = material.copy()
            separate.data.materials.clear()
            separate.data.materials.append(pure_volume)
            pure_volume.node_tree.nodes.clear()
            out = pure_volume.node_tree.nodes.new('ShaderNodeOutputMaterial')
            reference_absorption = pure_volume.node_tree.nodes.new('ShaderNodeVolumeAbsorption')
            reference_absorption.inputs['Color'].default_value = (0, 0, 0, 1)
            reference_absorption.inputs['Density'].default_value = .06
            pure_volume.node_tree.links.new(reference_absorption.outputs[0], out.inputs['Volume'])
            links.remove(output.inputs['Volume'].links[0])
    elif case == 'camera_inside':
        camera.location = center
        camera.data.clip_start = .01
        camera.data.clip_end = extent * 2
    elif case == 'far_clip_inside':
        forward = camera.rotation_euler.to_matrix() @ Vector((0, 0, -1))
        camera.data.clip_end = (center-camera.location).dot(forward)
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.cycles.samples = 4
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    # Rejection fixtures retain the unsupported setting in the saved scene.
    # Rendering must fail without replacing a previous completed deep output.
    if case == 'transformed_grid':
        grid.scale = (1.1, .8, 1.2)
        grid.rotation_euler.z = .2
        grid.location += right * extent * .05
    elif case == 'gaussian_filter':
        scene.cycles.pixel_filter_type = 'GAUSSIAN'
    elif case == 'blackman_harris_filter':
        scene.cycles.pixel_filter_type = 'BLACKMAN_HARRIS'
    elif case == 'zero_extinction':
        multiply = next(n for n in grid.data.materials[0].node_tree.nodes if n.bl_idname == 'ShaderNodeMath')
        multiply.inputs[1].default_value = 0
    elif case in ('adaptive_volume', 'adaptive_denoised_volume'):
        scene.cycles.use_adaptive_sampling = True
        scene.cycles.samples = 64
        scene.cycles.adaptive_min_samples = 4
        scene.cycles.adaptive_threshold = .5
    elif case == 'expanded_capacity':
        scene.cycles.samples = 3
    if case in ('denoised_volume', 'adaptive_denoised_volume'):
        scene.cycles.use_denoising = True
        scene.cycles.denoiser = 'OPENIMAGEDENOISE'
        scene.cycles.denoising_use_gpu = True
    elif case == 'reject_dof':
        camera.data.dof.use_dof = True
        camera.data.dof.focus_distance = (center-camera.location).length
        camera.data.dof.aperture_fstop = 2
    elif case == 'reject_motion':
        scene.render.use_motion_blur = True
    elif case == 'reject_orthographic':
        camera.data.type = 'ORTHO'
    elif case == 'reject_cubic':
        grid.data.materials[0].cycles.volume_interpolation = 'CUBIC'
    elif case == 'half_precision':
        grid.data.render.precision = 'HALF'
    elif case == 'reject_color':
        material = grid.data.materials[0]
        absorption = next(n for n in material.node_tree.nodes if n.bl_idname == 'ShaderNodeVolumeAbsorption')
        absorption.inputs['Color'].default_value = (.1, .2, .1, 1)
    elif case == 'reject_nonlinear':
        material = grid.data.materials[0]
        attribute = next(n for n in material.node_tree.nodes if n.bl_idname == 'ShaderNodeAttribute')
        multiply = next(n for n in material.node_tree.nodes if n.bl_idname == 'ShaderNodeMath')
        material.node_tree.links.new(attribute.outputs['Fac'], multiply.inputs[1])
    elif case == 'reject_reflection':
        grid.scale.x = -1
    if hasattr(scene.cycles, 'use_deep_output'):
        scene.cycles.use_deep_output = False
    path = root / case / 'scene.blend'
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    manifest['cases'][case] = {'scene': str(path), 'resolution': [width, height],
                              'samples': scene.cycles.samples, 'center': list(center),
                              'deep_max_events': 1 if case == 'opaque_foreground' else
                                                 8192 if case == 'expanded_capacity' else 16,
                              'volume_names': sorted(o.name for o in scene.objects if o.type == 'VOLUME'),
                              'clip': [camera.data.clip_start, camera.data.clip_end]}
if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Source fixture changed')
(root / 'cases.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
