"""Blender --background --python SCRIPT -- DIRECTORY: Phase 8b surface fixtures."""
import json
from pathlib import Path
import sys
import bpy

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.shading_system = True
scene.cycles.samples = 4
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.cycles.transparent_max_bounces = 16
# Three planes: disable roulette so native alpha is an independent exact oracle.
scene.cycles.min_transparent_bounces = 16
scene.render.film_transparent = True
scene.render.use_persistent_data = True
scene.render.resolution_x, scene.render.resolution_y = 33, 17
scene.render.resolution_percentage = 100
bpy.ops.object.camera_add()
scene.camera = bpy.context.object
scene.camera.data.type = 'ORTHO'
scene.camera.data.ortho_scale = 4
planes = []
for i in range(3):
    bpy.ops.mesh.primitive_plane_add(size=10, location=(0, 0, -2-i))
    planes.append(bpy.context.object)

image = bpy.data.images.new('AlphaTexture', width=4, height=4, float_buffer=True)
image.colorspace_settings.name = 'Non-Color'
image.pixels = [v for y in range(4) for x in range(4)
                for v in ((x+y+1)/10, (x+y+1)/10, (x+y+1)/10, 1)]
image.filepath_raw = str(root/'alpha.exr')
image.file_format = 'OPEN_EXR'
image.save()

expressions = {
    'constant': 'float a = 0.35;',
    'texture': 'float a = texture("'+(root/'alpha.exr').as_posix()+'", u, v, "interp", "closest", "wrap", "clamp");',
    'noise': 'float a = clamp(0.35 + 0.15 * noise("perlin", P * 3), 0.05, 0.95);',
    'camera': 'float a = raytype("camera") ? 0.35 : 0.9;',
    'trace': 'float a = trace(P, I) ? 0.35 : 0.9;',
    'shadow': 'float a = raytype("shadow") ? 0.35 : 0.9;',
    'bevel': 'float a = texture("@bevel", 4, 0.05);',
    'ao': 'float a = texture("@ao", 4, 0.05);',
    'ray_depth': 'float a = 0; getattribute("path:ray_depth", a); a = 0.35 + a * 0.01;',
    'ray_length': 'float a = 0; getattribute("path:ray_length", a); a = 0.35 + a * 0.01;',
    'unknown_attribute': 'float a = 0; getattribute("missing_attribute", a); a = 0.35 + a * 0.01;',
    'dynamic_attribute': 'float a = 0; string key = u < 0.5 ? "object:alpha" : "missing_attribute"; getattribute(key, a);',
    'unknown_userdata': 'float a = opacity;',
    'non_grey': 'float a = 0.35;',
    'non_finite': 'float a = 1e30 * (1e30 + u);',
    'closure_loop': 'float a = 0.35;',
    'arena_overflow': 'float a = 0.35;',
}
reject = {
    'trace': 'trace', 'shadow': 'ray-type queries', 'bevel': '@bevel', 'ao': '@ao', 'ray_depth': 'path:ray_depth',
    'ray_length': 'path:ray_length', 'unknown_attribute': 'missing_attribute',
    'dynamic_attribute': 'dynamic/unknown', 'unknown_userdata': 'unknown userdata',
    'non_grey': 'extinction', 'non_finite': 'extinction', 'closure_loop': 'closure-building loops',
    'arena_overflow': 'closure',
}
cases = []
for name in [*expressions, 'mixed']:
    material = bpy.data.materials.new('OSL_'+name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    if name == 'mixed':
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        emission = nodes.new('ShaderNodeEmission')
        mix = nodes.new('ShaderNodeMixShader')
        mix.inputs[0].default_value = 0.35
        material.node_tree.links.new(transparent.outputs[0], mix.inputs[1])
        material.node_tree.links.new(emission.outputs[0], mix.inputs[2])
        material.node_tree.links.new(mix.outputs[0], output.inputs[0])
    else:
        node = nodes.new('ShaderNodeScript')
        node.mode = 'EXTERNAL'
        source = root/(name+'.osl')
        params = 'float opacity = 0.35 [[int lockgeom=0]], ' if name == 'unknown_userdata' else ''
        body = expressions[name] + '\nCi = (1-a)*transparent() + a*emission();'
        if name == 'non_grey':
            body = 'Ci = color(0.2, 0.4, 0.6)*transparent() + 0.35*emission();'
        if name == 'closure_loop':
            body = 'Ci = 0; for (int j=0; j<int(u*10)+2; ++j) Ci += 0.01*transparent();'
        if name == 'arena_overflow':
            body = 'Ci = 0;\n' + '\n'.join('Ci += (0.01 + u*'+str(i+1)+')*transparent();' for i in range(40))
        source.write_text('shader fixture('+params+'output closure color BSDF = 0) {\n'+body.replace('Ci', 'BSDF')+'\n}\n')
        node.filepath = str(source)
        if 'BSDF' not in node.outputs:
            raise RuntimeError('OSL compilation failed: '+name)
        material.node_tree.links.new(node.outputs['BSDF'], output.inputs[0])
    for plane in planes:
        plane.data.materials.clear()
        plane.data.materials.append(material)
    if name == 'mixed':
        planes[1].data.materials[0] = bpy.data.materials['OSL_constant']
    scene.render.filepath = str(root/(name+'.exr'))
    bpy.ops.wm.save_as_mainfile(filepath=str(root/(name+'.blend')))
    cases.append(dict(name=name, scene=str(root/(name+'.blend')), rejection=reject.get(name)))
(root/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
