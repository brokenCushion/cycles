"""Blender --background --python SCRIPT -- OUTPUT_DIRECTORY: Phase 8c fixtures."""
import json
from pathlib import Path
import sys
import bpy

root = Path(sys.argv[sys.argv.index('--')+1]).resolve()
root.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene.render.engine = 'CYCLES'
scene.cycles.samples = 4
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.render.film_transparent = True
scene.render.resolution_x, scene.render.resolution_y = 17, 9
scene.render.resolution_percentage = 100
scene.cycles.transparent_max_bounces = scene.cycles.min_transparent_bounces = 16
bpy.ops.object.camera_add()
scene.camera = bpy.context.object
scene.camera.data.type = 'PERSP'
scene.camera.data.lens = 110
scene.camera.data.clip_start, scene.camera.data.clip_end = .1, 100
image = bpy.data.images.new('DensityTexture', width=256, height=4, float_buffer=True)
image.colorspace_settings.name = 'Non-Color'
image.pixels = [v for y in range(4) for x in range(256)
                for v in ((.8+.04*(x+.5)/256,)*3+(1,))]
image.filepath_raw, image.file_format = str(root/'density.exr'), 'OPEN_EXR'
image.save()
cases = []
objects = []
for name in ('constant', 'texture', 'grid', 'svm_optin'):
    scenes = {}
    for osl in (True, False):
        for obj in objects:
            bpy.data.objects.remove(obj, do_unlink=True)
        objects.clear()
        scene.cycles.shading_system = osl and name != 'svm_optin'
        if name == 'constant':
            bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, -3))
        else:
            path = root/(('svm-optin-baked' if not osl and name == 'svm_optin' else
                          'texture-baked' if not osl and name == 'texture' else
                          'texture' if name in ('texture', 'svm_optin') else 'grid')+'.vdb')
            bpy.ops.object.volume_import(filepath=str(path))
            bpy.context.object.data.render.precision = 'FULL'
        obj = bpy.context.object
        objects.append(obj)
        material = bpy.data.materials.new(name+('_OSL' if osl else '_analytic'))
        material.use_nodes = True
        nodes, links = material.node_tree.nodes, material.node_tree.links
        nodes.clear()
        out = nodes.new('ShaderNodeOutputMaterial')
        if osl and name != 'svm_optin':
            source = root/(name+'.osl')
            expression = 'float d = 0.35;'
            if name != 'constant':
                expression = 'float d = 0; getattribute("geom:density", d);'
            if name == 'texture':
                expression += ' d *= texture("'+(root/'density.exr').as_posix()+'", (P[0]+2)/4, 0.5, "interp", "bilinear", "wrap", "clamp");'
            source.write_text('shader fixture(output closure color Volume = 0) { '+expression+' Volume = d*absorption(); }\n')
            script = nodes.new('ShaderNodeScript')
            script.mode, script.filepath = 'EXTERNAL', str(source)
            if 'Volume' not in script.outputs:
                raise RuntimeError('OSL volume compilation failed: '+name)
            links.new(script.outputs['Volume'], out.inputs['Volume'])
        else:
            absorption = nodes.new('ShaderNodeVolumeAbsorption')
            absorption.inputs['Color'].default_value = (0, 0, 0, 1)
            absorption.inputs['Density'].default_value = .35
            if name != 'constant':
                attr = nodes.new('ShaderNodeAttribute')
                attr.attribute_name = 'density'
                density = attr.outputs['Fac']
                if name == 'svm_optin' and osl:
                    multiply = nodes.new('ShaderNodeMath')
                    multiply.operation = 'MULTIPLY'
                    links.new(density, multiply.inputs[0])
                    links.new(density, multiply.inputs[1])
                    density = multiply.outputs[0]
                links.new(density, absorption.inputs['Density'])
            links.new(absorption.outputs[0], out.inputs['Volume'])
        obj.data.materials.append(material)
        label = 'eval' if osl else 'analytic'
        scenes[label] = str(root/(name+'-'+label+'.blend'))
        bpy.ops.wm.save_as_mainfile(filepath=scenes[label])
    cases.append(dict(name=name, scenes=scenes, step=.005 if name in ('constant','texture','svm_optin') else 0,
                      voxel_step=.125, optin=name == 'svm_optin',
                      analytic_equivalence=('baked squared density; trilinear approximation of quadratic, <=5e-7 local density difference'
                                            if name == 'svm_optin' else
                                            'texture-baked trilinear grid; same continuous interior field, native texture rounding differs'
                                            if name == 'texture' else 'same geometry/extinction')))
for name, expression, reason in (
        ('reject_trace', 'float d = trace(P, I) ? 0.35 : 0.7; Volume = d*absorption();', 'trace'),
        ('reject_non_grey', 'Volume = color(0.2, 0.4, 0.6)*absorption();', 'extinction'),
        ('reject_non_finite', 'float d = 1e30*(1e30+P[0]); Volume = d*absorption();', 'extinction')):
    bpy.ops.wm.open_mainfile(filepath=str(root/'constant-eval.blend'))
    script = next(n for n in bpy.context.scene.objects['Cube'].data.materials[0].node_tree.nodes
                  if n.bl_idname == 'ShaderNodeScript')
    source = root/(name+'.osl')
    source.write_text('shader fixture(output closure color Volume = 0) { '+expression+' }\n')
    script.filepath = str(source)
    scene_path = str(root/(name+'.blend'))
    bpy.ops.wm.save_as_mainfile(filepath=scene_path)
    cases.append(dict(name=name, scenes=dict(eval=scene_path), rejection=reason, optin=False))
(root/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
