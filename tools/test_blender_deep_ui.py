"""Run with the patched Blender: -b --factory-startup --python SCRIPT -- OUTPUT.

OUTPUT must be fresh. UI drawing assertions supplement, not replace, visual review.
The qualified-scene identity and old-build migration checks run separately.
"""
import bpy
import json
from pathlib import Path
import sys
from types import SimpleNamespace

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=False)
results = {}
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 4
scene.cycles.use_denoising = False
scene.cycles.use_adaptive_sampling = False
scene.render.resolution_x = scene.render.resolution_y = 32
scene.render.resolution_percentage = 100
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.image_settings.file_format = 'PNG'
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.frame_start, scene.frame_end = 1, 2
layer = bpy.context.view_layer
layer.name = 'DeepLayer'
cscene = scene.cycles
assert not layer.cycles.use_pass_deep
assert not cscene.deep_write_diagnostics
assert cscene.deep_output_path == ''
layer.cycles.use_pass_deep = True
bpy.ops.wm.save_as_mainfile(filepath=str(root / 'paths.blend'))


def expected_path(override=None):
    original = scene.render.filepath
    try:
        if override is not None:
            scene.render.filepath = override
        path = Path(scene.render.frame_path())
        return path if str(path).endswith('.deep.exr') else path.with_suffix('.deep.exr')
    finally:
        scene.render.filepath = original


def animation(name, path, override='', extensions=True, diagnostics=False):
    scene.render.filepath = path
    scene.render.use_file_extension = extensions
    cscene.deep_output_path = override
    cscene.deep_write_diagnostics = diagnostics
    expected, beauties = [], []
    for frame in (1, 2):
        scene.frame_set(frame)
        expected.append(expected_path(override or None))
        beauties.append(Path(scene.render.frame_path()))
    assert expected[0] != expected[1]
    assert not any(p.exists() for p in expected)
    assert bpy.ops.render.render(animation=True) == {'FINISHED'}
    assert all(p.is_file() for p in beauties), beauties
    assert all(p.is_file() and p.stat().st_size > 0 for p in expected), expected
    assert all(Path(str(p) + '.samples.csv').exists() == diagnostics for p in expected)
    results[name] = [str(p) for p in expected]


animation('hashes', str(root / 'hashes' / 'shot_####'))
animation('no_hashes', str(root / 'no_hashes' / 'shot_'))
animation('relative', '//relative/shot_####')
animation('override', str(root / 'override-beauty' / 'beauty_####'),
          '//override/shot_####')
animation('override_no_hashes', str(root / 'override-plain-beauty' / 'beauty_'),
          str(root / 'override-plain' / 'shot_'))
animation('no_extension', str(root / 'no-extension' / 'shot_####'), extensions=False)
animation('csv_surface', str(root / 'csv-surface' / 'shot_####'), diagnostics=True)

# With diagnostics off, both branches must still initialize/release every deep row.
volume = bpy.data.materials.new('AbsorbingVolume')
volume.use_nodes = True
volume.node_tree.nodes.clear()
out = volume.node_tree.nodes.new('ShaderNodeOutputMaterial')
absorb = volume.node_tree.nodes.new('ShaderNodeVolumeAbsorption')
absorb.inputs['Color'].default_value = (0.5, 0.5, 0.5, 1)
absorb.inputs['Density'].default_value = 0.2
volume.node_tree.links.new(absorb.outputs[0], out.inputs['Volume'])
cube = bpy.data.objects['Cube']
old_materials = list(cube.data.materials)
cube.data.materials.clear()
cube.data.materials.append(volume)
cscene.use_deep_volume = True
cscene.use_deep_volume_shader_eval = True
cscene.deep_volume_step = 0.25
cscene.deep_max_events = 4096
animation('volume_no_csv', str(root / 'volume-no-csv' / 'shot_####'))
animation('volume_csv', str(root / 'volume-csv' / 'shot_####'), diagnostics=True)
for frame in (1, 2):
    a = root / 'volume-no-csv' / f'shot_{frame:04}.deep.exr'
    b = root / 'volume-csv' / f'shot_{frame:04}.deep.exr'
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from compare_deep_identity import identity
    assert identity(a)[0] == identity(b)[0], 'CSV switch changed volume deep data'
results['volume_csv_identity'] = True
cscene.use_deep_volume = False
cscene.use_deep_volume_shader_eval = False
cscene.deep_volume_step = 0
cscene.deep_max_events = 16
cube.data.materials.clear()
for material in old_materials:
    cube.data.materials.append(material)

other = scene.view_layers.new('BeautyLayer')
other.cycles.use_pass_deep = True
scene.render.filepath = str(root / 'rejected' / 'shot_####')
cscene.deep_output_path = ''
try:
    bpy.ops.render.render()
except RuntimeError as error:
    assert 'DeepLayer' in str(error) and 'BeautyLayer' in str(error), str(error)
    results['two_deep_layers'] = str(error)
else:
    raise AssertionError('Two deep layers must fail')
assert not (root / 'rejected').exists()
other.cycles.use_pass_deep = False
scene.render.image_settings.media_type = 'MULTI_LAYER_IMAGE'
scene.render.image_settings.file_format = 'OPEN_EXR_MULTILAYER'
scene.render.image_settings.use_exr_interleave = True
animation('mixed_layers', str(root / 'mixed-layers' / 'shot_####'))
for frame in (1, 2):
    import struct
    with (root / 'mixed-layers' / f'shot_{frame:04}.exr').open('rb') as stream:
        def cstring():
            value = bytearray()
            while True:
                byte = stream.read(1)
                assert byte, 'Truncated beauty header'
                if byte == b'\0':
                    return bytes(value)
                value.extend(byte)
        assert struct.unpack('<I', stream.read(4))[0] == 20000630
        stream.read(4)
        attributes = {}
        while name := cstring():
            kind = cstring()
            size = struct.unpack('<I', stream.read(4))[0]
            attributes[name] = stream.read(size)
        channels = attributes[b'channels']
        assert b'DeepLayer.Combined.R\0' in channels, channels
        assert b'BeautyLayer.Combined.R\0' in channels, channels
results['both_layers_rendered'] = True

scene.render.use_multiview = True
try:
    bpy.ops.render.render()
except RuntimeError as error:
    assert 'mono' in str(error), str(error)
    results['mono_only'] = str(error)
else:
    raise AssertionError('Multiview deep must fail')
scene.render.use_multiview = False

# Bake while Deep remains enabled: the optional null layer must keep capture off.
scene.render.filepath = str(root / 'bake' / 'should_not_exist_####')
image = bpy.data.images.new('BakeTarget', width=16, height=16, float_buffer=True)
material = bpy.data.materials.new('BakeEmission')
material.use_nodes = True
nodes = material.node_tree.nodes
nodes.clear()
output = nodes.new('ShaderNodeOutputMaterial')
emission = nodes.new('ShaderNodeEmission')
emission.inputs['Color'].default_value = (0.2, 0.4, 0.8, 1)
material.node_tree.links.new(emission.outputs[0], output.inputs['Surface'])
texture = nodes.new('ShaderNodeTexImage')
texture.image = image
nodes.active = texture
cube.data.materials.clear()
cube.data.materials.append(material)
for obj in bpy.context.selected_objects:
    obj.select_set(False)
cube.select_set(True)
bpy.context.view_layer.objects.active = cube
assert bpy.ops.object.bake(type='EMIT') == {'FINISHED'}
pixels = list(image.pixels)
assert any(abs(pixels[i] - 0.2) < 0.001 and abs(pixels[i + 2] - 0.8) < 0.001
           for i in range(0, len(pixels), 4))
assert not (root / 'bake').exists()
results['bake_deep_off'] = True

from cycles import ui
assert not hasattr(ui, 'CYCLES_RENDER_PT_deep')
assert ui.classes.index(ui.CYCLES_RENDER_PT_passes_deep) == ui.classes.index(ui.CYCLES_RENDER_PT_passes_crypto) + 1
assert ui.CYCLES_RENDER_PT_passes_deep.bl_parent_id == 'CYCLES_RENDER_PT_passes'
assert ui.CYCLES_RENDER_PT_output_deep.bl_parent_id == 'RENDER_PT_output'


class Layout:
    def __init__(self, log, parent=None):
        self.log, self.parent, self.active = log, parent, True

    def column(self):
        return Layout(self.log, self)

    def enabled(self):
        return self.active and (self.parent is None or self.parent.enabled())

    def prop(self, owner, name, **kwargs):
        self.log.append(('prop', name, self.enabled()))

    def label(self, text, **kwargs):
        self.log.append(('label', text, self.enabled()))

    def separator(self):
        pass


for enabled in (False, True):
    layer.cycles.use_pass_deep = enabled
    log = []
    ui.CYCLES_RENDER_PT_passes_deep.draw(SimpleNamespace(layout=Layout(log)), bpy.context)
    props = [entry for entry in log if entry[0] == 'prop']
    assert len(props) == 10 and all(entry[2] == enabled for entry in props), log
    assert any(entry[1] == 'Shared by all view layers' for entry in log)
    note = ' '.join(entry[1] for entry in log if entry[0] == 'label')
    assert ('Deep in interactive (F12) renders is experimental; background renders are the qualified path.' in note) == enabled
    assert ui.CYCLES_RENDER_PT_output_deep.poll(bpy.context) == enabled
results['panel_draw_contract'] = True
(root / 'results.json').write_text(json.dumps(results, indent=2))
print('DEEP_UI_TESTS_PASS', root)
