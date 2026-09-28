# SPDX-License-Identifier: Apache-2.0
"""Bundle passing M8 render graphs for Gaffer review.

gaffer env python SCRIPT --matrix DIRECTORY [--scale DIRECTORY]
  [--surface DIRECTORY] --output FILE.gfr
Each box exposes the actual deep point cloud and its linked depth-cut controls.
"""
import argparse
import json
from pathlib import Path

import CyclesDeep
import Gaffer
import GafferScene
import imath

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--matrix', type=Path, required=True)
parser.add_argument('--scale', type=Path)
parser.add_argument('--surface', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
script = Gaffer.ScriptNode()
sources = []
if args.scale:
    scale = args.scale.resolve()
    if not json.loads((scale / 'report.json').read_text())['passed']:
        raise RuntimeError('Production qualification has not passed')
    for device in ('cpu', 'cuda'):
        sources.append((f'Production_{device.upper()}', scale / f'{device}-1',
                        'native_vdb_review.gfr', 'VDBDeepPoints', 'VolumeDepthCut'))
for device in ('CPU', 'CUDA'):
    root = args.matrix.resolve() / f'native-{device}'
    if not json.loads((root / 'report.json').read_text())['passed']:
        raise RuntimeError(f'{device} native matrix has not passed')
    for name in ('mixed_surface', 'overlapping_grids', 'camera_inside',
                 'scattering', 'transformed_grid', 'zero_extinction'):
        sources.append((f'{device}_{name}', root / name / 'deep',
                        'native_vdb_review.gfr', 'VDBDeepPoints', 'VolumeDepthCut'))
if args.surface:
    sources.append(('Blender_surface_asset', args.surface.resolve(),
                    'blender_deep_review.gfr', 'SceneDeepPoints', 'SceneDepthCut'))
report = {'passed': False, 'graphs': []}
for index, (name, directory, filename, cloud_name, cut_name) in enumerate(sources):
    validation = json.loads((directory / 'gaffer_validation.json').read_text())
    if not validation['passed']:
        raise RuntimeError(f'Unqualified render: {directory}')
    loaded = Gaffer.ScriptNode()
    loaded['fileName'].setValue((directory / filename).as_posix())
    loaded.load()
    box = Gaffer.Box(name)
    script.addChild(box)
    for node in list(loaded.children(Gaffer.Node)):
        box.addChild(node)
    output = Gaffer.PlugAlgo.promote(box[cloud_name]['out'])
    controls = Gaffer.PlugAlgo.promote(box[cut_name]['farClip'])
    controls.setName('depthCut')
    box.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(
        (index % 3) * 45, -(index // 3) * 35),
        flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    Gaffer.Metadata.registerValue(box, 'description',
        'Points read from the rendered deep EXR. Press F in the Viewer to frame '
        'the cloud. Enable depthCut and change its '
        'value to slice the cloud. Enter the box to view beauty and flattened alpha. '
        f'Validated render: {directory.as_posix()}')
    report['graphs'].append({'name': name, 'render': directory.as_posix(),
                             'points': validation['displayed_points']})
note = Gaffer.Backdrop('ReviewInstructions')
script.addChild(note)
note['title'].setValue('M8 deep alpha review')
note['description'].setValue(
    'Select a box to view its deep points. Press F in the 3D Viewer to frame.\n'
    'Enable depthCut and adjust its value to slice the points.\n'
    'Enter a box to view beauty, full alpha and image depth cuts.\n'
    'Max Points limits the preview; the EXR retains its stored samples.')
note.addChild(Gaffer.V2fPlug('__uiPosition', defaultValue=imath.V2f(0, 45),
    flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
output_path = args.output.resolve()
output_path.parent.mkdir(parents=True, exist_ok=True)
script['fileName'].setValue(output_path.as_posix())
script.save()
focus = sources[0][0]
with output_path.open('a') as stream:
    stream.write(f'\nparent.selection().add(parent[{focus!r}])\n'
                 f'parent.setFocus(parent[{focus!r}])\n')
reloaded = Gaffer.ScriptNode()
reloaded['fileName'].setValue(output_path.as_posix())
reloaded.load()
if not reloaded.getFocus().isSame(reloaded[focus]):
    raise RuntimeError('Review focus did not reload')
for item in report['graphs']:
    points = reloaded[item['name']]['out'].object('/deepPoints')
    if points.numPoints != item['points'] or not points.arePrimitiveVariablesValid():
        raise RuntimeError('Reloaded deep cloud differs: ' + item['name'])
report['passed'] = True
output_path.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
