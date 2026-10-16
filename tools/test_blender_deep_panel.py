"""Capture the Deep panel with a GUI Blender, then quit without saving.

Use --factory-startup --enable-event-simulate --python SCRIPT -- FRESH_DIRECTORY.
Optional --tooltip X Y hovers Deep Alpha at window coordinates (bottom origin).
Inspect the screenshots; test_blender_deep_ui.py checks the draw/RNA contract.
"""
import argparse
import bpy
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--tooltip', nargs=2, type=int)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
args.output.mkdir(parents=True, exist_ok=False)
assert not bpy.app.background
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.deep_error = 0.001
scene.cycles.use_deep_ids = True
layer = bpy.context.view_layer
layer.cycles.use_pass_deep = False
assert not layer.cycles.use_pass_deep_color
bpy.context.preferences.view.show_splash = False
bpy.context.preferences.view.show_tooltips = True
window = bpy.context.window
area = max(bpy.context.screen.areas, key=lambda a: a.width * a.height)
area.type = 'PROPERTIES'
area.spaces.active.context = 'VIEW_LAYER'
stage = 0
screenshots = []


def capture(name):
    path = args.output / (name + '.png')
    bpy.ops.screen.screenshot(filepath=str(path))
    screenshots.append(path.name)


def step():
    global stage
    if stage == 0:
        capture('alpha-off')
        layer.cycles.use_pass_deep = True
    elif stage == 1:
        capture('alpha-on')
        scene.cycles.deep_error = 0
    elif stage == 2:
        capture('strict-ids-inactive')
        assert scene.cycles.use_deep_ids, 'Drawing must not reset saved IDs'
        scene.cycles.deep_error = 0.001
    elif stage == 3 and args.tooltip:
        x, y = args.tooltip
        window.event_simulate(type='MOUSEMOVE', value='NOTHING', x=x, y=y)
    else:
        if args.tooltip:
            capture('alpha-tooltip')
        (args.output / 'results.json').write_text(json.dumps({
            'screenshots': screenshots,
            'alpha_tooltip': layer.cycles.bl_rna.properties['use_pass_deep'].description,
            'color_tooltip': layer.cycles.bl_rna.properties['use_pass_deep_color'].description,
            'strict_does_not_reset_ids': scene.cycles.use_deep_ids,
        }, indent=2))
        bpy.ops.wm.quit_blender()
        return None
    stage += 1
    area.tag_redraw()
    return 4


bpy.app.timers.register(step, first_interval=5)
