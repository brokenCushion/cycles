"""Blender -b --python SCRIPT -- FRESH_ROOT; ordinary Python SCRIPT ROOT --validate."""
import json
from pathlib import Path
import sys

args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else sys.argv[1:]
root = Path(args[0])
if '--validate' in args:
    from deep_exr import read, check_deep
    result = json.loads((root/'results.json').read_text())
    reports = {}
    for layer in ('Surface', 'Volume'):
        image = read(root / (layer+'_0001.deep.exr'))
        reports[layer] = check_deep(image)
        header = image.header()
        if layer == 'Surface':
            assert 'cycles:deepZTolerance' not in header
            assert 'cycles:deepVolumeMethod' not in header
        else:
            assert abs(header['cycles:deepZTolerance']-.0001) < 1e-10
            expected_scope = 'shader_evaluated_scalar_extinction' if result.get('shader_eval', False) else 'native_scalar_extinction'
            assert header['cycles:deepScope'] == expected_scope
            assert any(zb > z for front,back in zip(image.channels()['Z'].pixels.flat,
                       image.channels()['ZBack'].pixels.flat) if front is not None
                       for z,zb in zip(front,back))
        reports[layer]['headers'] = {k:str(v) for k,v in header.items() if k.startswith('cycles:deep')}
    (root/'validation.json').write_text(json.dumps(reports, indent=2))
    print('DEEP_LAYER_OVERRIDES_PASS', flush=True)
    raise SystemExit()

import bpy
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scan_blender_deep_scene import fixture
from scan_blender_deep_scene import scan, effective_deep_settings
root.mkdir(parents=True, exist_ok=False)
s, *_ = fixture()
s.camera.data.type = 'PERSP'
s.render.image_settings.file_format = 'OPEN_EXR'
s.cycles.deep_output_path = str(root/'{view_layer}_####')
s.cycles.deep_z_tolerance = .0001
s.cycles.use_deep_volume = True
s.cycles.use_deep_volume_shader_eval = True
s.cycles.deep_volume_step = .01
s.cycles.deep_max_events = 4096
surface = s.view_layers[0]; surface.name = 'Surface'
surface.cycles.deep_volume_mode = 'OFF'
surface.cycles.deep_z_tolerance_mode = 'CUSTOM'
surface.cycles.deep_z_tolerance = 0
surface.cycles.deep_max_events_mode = 'CUSTOM'
surface.cycles.deep_max_events = 64
volume = s.view_layers.new('Volume'); volume.cycles.use_pass_deep = True
volume.cycles.deep_volume_mode = 'SCENE'
collection = bpy.data.collections.new('VolumeOnly'); s.collection.children.link(collection)
bpy.ops.mesh.primitive_cube_add(size=1, location=(0,0,2))
cube = bpy.context.object
for owner in list(cube.users_collection): owner.objects.unlink(cube)
collection.objects.link(cube)
mat = bpy.data.materials.new('Absorption'); mat.use_nodes = True
mat.node_tree.nodes.clear()
output = mat.node_tree.nodes.new('ShaderNodeOutputMaterial')
absorb = mat.node_tree.nodes.new('ShaderNodeVolumeAbsorption')
absorb.inputs['Density'].default_value = .2
absorb.inputs['Color'].default_value = (.5,.5,.5,1)
mat.node_tree.links.new(absorb.outputs[0], output.inputs['Volume'])
cube.data.materials.append(mat)
surface.layer_collection.children['VolumeOnly'].exclude = True
assert effective_deep_settings(s, SimpleNamespace(cycles=SimpleNamespace())) == (True,s.cycles.deep_z_tolerance)
report = scan(s)
(root/'scanner.json').write_text(json.dumps(report, indent=2))
assert not report['blocks'], report
assert [(r['effective_volume'],r['effective_z_tolerance']) for r in report['layers']] == [(False,0),(True,s.cycles.deep_z_tolerance)]
assert [(r['effective_events'],r['effective_shader_eval']) for r in report['layers']] == [(64,False),(4096,True)]
bpy.ops.render.render()
assert all((root/(name+'_0001.deep.exr')).is_file() for name in ('Surface','Volume'))
surface.cycles.deep_max_events_mode = 'SCENE'
volume.use = False
volume.cycles.use_pass_deep = False
s.cycles.deep_output_path = str(root/'negative'/'{view_layer}_####')
negative = scan(s)
assert any(f['rule']=='events' for l in negative['layers'] for f in l['findings']), negative
try: bpy.ops.render.render()
except RuntimeError as error: assert 'invalid deep traversal limit' in str(error), str(error)
else: raise AssertionError('Surface 4096 must be rejected')
assert not list((root/'negative').glob('*.deep.exr'))
(root/'results.json').write_text(json.dumps(dict(passed=True,shader_eval=True,negative=negative), indent=2))
print('DEEP_LAYER_OVERRIDES_RENDER_PASS', flush=True)
