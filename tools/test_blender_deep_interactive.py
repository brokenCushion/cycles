"""Small F12 regression: launch without -b.

blender --factory-startup --python SCRIPT -- FRESH_OUTPUT
Uses Blender's interactive render operator. Send a real ESC key to its window
each time cancel-request.json changes; event_simulate bypasses G.is_break.
Require results.json's passed flag: GUI shutdown alone does not signal success.
"""
import bpy
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_deep_identity import identity

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=False)
assert not bpy.app.background
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 8
scene.cycles.use_adaptive_sampling = False
scene.cycles.use_denoising = False
scene.cycles.deep_error = 0
scene.cycles.deep_output_path = ''
assert not scene.cycles.deep_write_diagnostics
bpy.context.view_layer.cycles.use_pass_deep = True
scene.render.resolution_x = scene.render.resolution_y = 64
scene.render.resolution_percentage = 100
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.filepath = str(root / 'repeat' / 'frame_####')
bpy.context.preferences.view.render_display_type = 'NONE'
bpy.context.preferences.view.show_splash = False
counts = {'pre': 0, 'complete': 0, 'cancel': 0}
result = {'background': bpy.app.background, 'passed': False}
stage = 0
started = time.monotonic()
job_started = None
prior = None
first_identity = None


def pre(*_):
    counts['pre'] += 1


def complete(*_):
    counts['complete'] += 1


def cancelled(*_):
    counts['cancel'] += 1


bpy.app.handlers.render_pre.append(pre)
bpy.app.handlers.render_complete.append(complete)
bpy.app.handlers.render_cancel.append(cancelled)


def deep_path():
    return Path(scene.render.frame_path()).with_suffix('.deep.exr')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def published(csv):
    path = deep_path()
    value, _ = identity(path)
    assert path.stat().st_size > 0
    sidecar = Path(str(path) + '.samples.csv')
    assert sidecar.exists() == csv
    if csv:
        assert sidecar.read_text().startswith('file_x,file_y,sample,depth,alpha,event\n')
    assert not list(root.rglob('*.partial-*')), 'Partial publication left behind'
    return {'deep': path.relative_to(root).as_posix(), 'identity': value,
            'sha256': digest(path), 'csv': csv}


def invoke(animation=False):
    global job_started
    assert bpy.ops.render.render('INVOKE_DEFAULT', animation=animation) == {'RUNNING_MODAL'}
    job_started = time.monotonic()


def escape():
    assert bpy.app.is_job_running('RENDER'), 'Render finished before cancellation'
    # A warm cube scene has already entered its render callback; keep the job
    # running for two seconds before exercising the normal ESC cancellation path.
    assert time.monotonic() - job_started >= 2
    (root / 'cancel-request.json').write_text(json.dumps({'render': counts['pre']}))


def step():
    global stage, prior, first_identity
    assert time.monotonic() - started < 180, 'Interactive verification timed out'
    running = bpy.app.is_job_running('RENDER')
    if stage == 0:
        invoke()
    elif stage == 1:
        if running:
            return 0.1
        assert counts['complete'] == 1 and counts['cancel'] == 0
        result['first_f12_csv_off'] = published(False)
        first_identity = result['first_f12_csv_off']['identity']
        scene.cycles.deep_write_diagnostics = True
        bpy.data.objects['Cube'].location.x = 0.25
        invoke()
    elif stage == 2:
        if running:
            return 0.1
        assert counts['complete'] == 2 and counts['cancel'] == 0
        result['second_f12_csv_on'] = published(True)
        assert result['second_f12_csv_on']['identity'] != first_identity
        path = deep_path()
        prior = {path: digest(path), Path(str(path) + '.samples.csv'): digest(Path(str(path) + '.samples.csv'))}
        scene.cycles.samples = 1024
        scene.render.resolution_x = scene.render.resolution_y = 128
        bpy.data.objects['Cube'].location.x = -0.5
        invoke()
    elif stage == 3:
        if time.monotonic() - job_started < 2:
            return 0.1
        assert counts['pre'] >= 3 and counts['complete'] == 2
        escape()
    elif stage == 4:
        if running:
            return 0.1
        assert counts['cancel'] == 1 and counts['complete'] == 2
        assert {path: digest(path) for path in prior} == prior
        assert not list(root.rglob('*.partial-*'))
        result['cancel_existing'] = {'previous_exr_and_csv_unchanged': True,
                                     'no_partial_files': True,
                                     'previous_sha256': {p.name: h for p, h in prior.items()}}
        scene.render.filepath = str(root / 'cancel-fresh' / 'frame_####')
        invoke()
    elif stage == 5:
        if time.monotonic() - job_started < 2:
            return 0.1
        assert counts['pre'] >= 4 and counts['complete'] == 2
        escape()
    elif stage == 6:
        if running:
            return 0.1
        assert counts['cancel'] == 2 and counts['complete'] == 2
        assert not deep_path().exists()
        assert not Path(str(deep_path()) + '.samples.csv').exists()
        assert not list(root.rglob('*.partial-*'))
        result['cancel_fresh'] = {'no_exr_or_csv': True, 'no_partial_files': True}
        scene.render.resolution_x = scene.render.resolution_y = 64
        scene.cycles.samples = 8
        scene.cycles.deep_write_diagnostics = False
        scene.frame_start = scene.frame_end = 2
        scene.frame_set(2)
        scene.render.filepath = str(root / 'animation' / 'frame_####')
        invoke(animation=True)
    elif stage == 7:
        if running:
            return 0.1
        assert counts['complete'] == 3 and counts['cancel'] == 2
        result['interactive_animation_frame_2'] = published(False)
        assert deep_path().name == 'frame_0002.deep.exr'
        assert Path(scene.render.frame_path()).is_file()
        result['counts'] = counts
        result['passed'] = True
        (root / 'results.json').write_text(json.dumps(result, indent=2))
        print('DEEP_INTERACTIVE_TESTS_PASS', json.dumps(result), flush=True)
        bpy.ops.wm.quit_blender()
        return None
    stage += 1
    return 0.1


def guarded_step():
    try:
        return step()
    except Exception:
        result.update(error=traceback.format_exc(), failed_stage=stage, counts=counts)
        (root / 'results.json').write_text(json.dumps(result, indent=2))
        traceback.print_exc()
        bpy.ops.wm.quit_blender()
        return None


bpy.app.timers.register(guarded_step, first_interval=10)
