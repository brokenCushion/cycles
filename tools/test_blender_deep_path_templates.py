"""Render-backed path drift fixtures; run each case in a separate Blender process."""
import json
from pathlib import Path
import sys
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scan_blender_deep_scene import fixture
from scan_blender_deep_scene import scan, template_tokens

root = Path(sys.argv[sys.argv.index('--')+1])
case = sys.argv[-1]
root.mkdir(parents=True, exist_ok=False)
s, *_ = fixture()
s.render.image_settings.file_format = 'OPEN_EXR'
s.render.filepath = str(root/'beauty_0001.exr')
s.cycles.deep_output_path = str(root/'{view_layer}_####')
s.render.use_compositing = True
tree = bpy.data.node_groups.new('Compositor', 'CompositorNodeTree')
s.compositing_node_group = tree
source = tree.nodes.new('CompositorNodeRLayers')
node = tree.nodes.new('CompositorNodeOutputFile')
node.name = 'BG'
node.directory = str(root/'flat')
node.file_name = '{node_name}_####'
node.format.file_format = 'OPEN_EXR_MULTILAYER'
if not len(node.file_output_items): node.file_output_items.new('RGBA', 'Image')
tree.links.new(source.outputs['Image'], node.inputs[0])
if case == 'view_layer': node.file_name = '{view_layer}_####'
elif case == 'brace': node.file_name = '{node_name_####'
elif case == 'format': node.file_name = '{fps:04d}_####'
elif case == 'string_format': node.file_name = '{node_name:###}_####'
elif case == 'directory': node.directory = str(root/'{view_layer}')
elif case == 'item':
    node.format.media_type = 'IMAGE'
    node.format.file_format = 'OPEN_EXR'
    node.file_output_items[0].name = '{view_layer}'
elif case == 'render': s.render.filepath = str(root/'{view_layer}_####')
elif case == 'deep': s.cycles.deep_output_path = str(root/'{view_layer}_####')
elif case == 'numeric': node.file_name = '{fps:##.##}_{resolution_x:###}_####'
elif case != 'valid': raise ValueError(case)
assert list(template_tokens('{{literal}}')) == list('{literal}')
report = scan(s)
(root/'scanner.json').write_text(json.dumps(report, indent=2))
expected = case not in ('valid', 'deep', 'numeric')
assert bool(report['path_findings']) == expected, report
error = None
try: bpy.ops.render.render(write_still=True)
except RuntimeError as exc: error = str(exc)
flat = list((root/'flat').glob('*.exr'))
if not expected:
    assert error is None and flat and list(root.glob('*.deep.exr')), (error, flat)
(root/'result.json').write_text(json.dumps(dict(case=case, expected_block=expected,
    render_error=error, files=[str(p.relative_to(root)) for p in root.rglob('*.exr')]), indent=2))
print('PATH_FIXTURE_COMPLETE', case, flush=True)
