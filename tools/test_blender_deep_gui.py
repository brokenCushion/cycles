"""Launch the patched Blender without -b, with --python SCRIPT -- FRESH_DIRECTORY.

Captures actual panels, a rendered viewport, and an F12 deep render.
Inspect screenshots and the launch log before accepting the GUI verification.
"""
import bpy
import json
from pathlib import Path
import sys

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=False)
assert not bpy.app.background
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 8
scene.cycles.preview_samples = 8
scene.cycles.use_denoising = False
scene.cycles.use_preview_denoising = False
scene.cycles.use_adaptive_sampling = False
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.resolution_x = scene.render.resolution_y = 64
scene.render.resolution_percentage = 100
scene.render.filepath = str(root / 'f12' / 'frame_####')
scene.cycles.deep_output_path = ''
scene.cycles.deep_write_diagnostics = True
bpy.context.preferences.view.show_splash = False
bpy.context.preferences.view.render_display_type = 'NONE'
area = max(bpy.context.screen.areas, key=lambda a: a.width * a.height)
result = {'screenshots': [], 'viewport_started': False, 'f12_completed': False}
stage = 0


def screenshot(name):
    path = root / (name + '.png')
    bpy.ops.screen.screenshot(filepath=str(path))
    result['screenshots'].append(str(path))
    print('DEEP_GUI_SCREENSHOT', path, flush=True)


def render_done(*_):
    result['f12_completed'] = True


bpy.app.handlers.render_complete.append(render_done)


def step():
    global stage
    if stage == 0:
        area.type = 'PROPERTIES'
        area.spaces.active.context = 'VIEW_LAYER'
        bpy.context.view_layer.cycles.use_pass_deep = False
    elif stage == 1:
        screenshot('view-layer-off')
        bpy.context.view_layer.cycles.use_pass_deep = True
    elif stage == 2:
        screenshot('view-layer-on')
        area.spaces.active.context = 'OUTPUT'
    elif stage == 3:
        screenshot('output-on')
        bpy.context.view_layer.cycles.use_pass_deep = False
    elif stage == 4:
        screenshot('output-off')
        area.spaces.active.context = 'RENDER'
    elif stage == 5:
        screenshot('render-tab')
        bpy.context.view_layer.cycles.use_pass_deep = True
        area.type = 'VIEW_3D'
        area.spaces.active.shading.type = 'RENDERED'
        stage += 1
        return 25
    elif stage == 6:
        screenshot('viewport-rendered')
        result['viewport_started'] = True
        area.spaces.active.shading.type = 'SOLID'
    elif stage == 7:
        # This is the operator invoked by F12, with the normal interactive job path.
        assert bpy.ops.render.render('INVOKE_DEFAULT') == {'RUNNING_MODAL'}
        stage += 1
        return 1
    elif stage == 8:
        if bpy.app.is_job_running('RENDER'):
            return 1
        assert result['f12_completed'], 'Interactive render failed'
        assert (root / 'f12' / 'frame_0001.deep.exr').is_file()
        assert (root / 'f12' / 'frame_0001.deep.exr.samples.csv').is_file()
        area.type = 'INFO'
    elif stage == 9:
        screenshot('f12-reports')
        result['f12_deep_and_csv_written'] = True
        (root / 'results.json').write_text(json.dumps(result, indent=2))
        print('DEEP_GUI_TESTS_DONE; inspect viewport and note screenshots', flush=True)
        return None
    stage += 1
    for editor in bpy.context.screen.areas:
        editor.tag_redraw()
    return 3


bpy.app.timers.register(step, first_interval=3)
