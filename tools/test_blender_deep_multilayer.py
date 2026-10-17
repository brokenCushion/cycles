"""Blender -b --factory-startup --python SCRIPT -- FRESH_OUTPUT [--device OPTIX].

Use --baseline for the qualified build's single-layer reference. Validate with
ordinary Python (OpenEXR/numpy): SCRIPT OUTPUT --validate --reference BASELINE.
For --cancel, omit -b and send real ESC when cancel-request.json appears, as in
test_blender_deep_interactive.py. Native input requires an unlocked desktop.
Require results.json, not just a zero exit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--device', choices=('CPU', 'OPTIX'), default='CPU')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--validate', action='store_true')
parser.add_argument('--reference', type=Path)
parser.add_argument('--cancel', action='store_true')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])
root = args.output.resolve()


def validate():
    from compare_deep_ids import curve_error, name_hash
    from deep_exr import check_deep, pixel, read

    results = json.loads((root / 'results.json').read_text())
    assert results['passed']
    reports = {}
    for filename, layer in results['files'].items():
        image = read(root / filename)
        report = check_deep(image)
        manifest = json.loads(image.header()['cycles:deepIDManifest'])
        holdouts = json.loads(image.header().get('cycles:deepIDHoldoutManifest', '{}'))
        assert all(name_hash(name) == int(key, 16) for key, name in manifest.items())
        assert set(holdouts.values()) == ({'Holdout'} if layer == 'A' else set()), holdouts
        # HoldoutManifest is a subset of IDManifest; only held-out objects are marked.
        assert all(manifest.get(key) == name for key, name in holdouts.items())
        observed = {int(identifier) for values in image.channels()['id'].pixels.flat
                    if values is not None for identifier in values}
        names = {manifest[f'{identifier:08x}'] for identifier in observed}
        expected = {'Back', 'Front', 'Transparent', 'Indirect', 'Holdout', 'Excluded'}
        if layer == 'A':
            expected.remove('Indirect')
        if layer == 'B':
            expected.remove('Excluded')
        assert names == expected, (filename, names, expected)
        assert ('Excluded' in manifest.values()) == (layer != 'B')
        h, w = image.channels()['A'].pixels.shape
        # Interior pixels avoid antialiasing edges: half-transparent at Z=5,
        # backed by the opaque plane (Z=8) or the foreground overlap (Z=6).
        errors = []
        for x, back in ((w // 2, 8), (w // 3, 6)):
            samples = pixel(image, x, h // 2)
            assert len(samples) > 1, (filename, x, samples)
            assert all(min(abs(z - 5), abs(z - back)) < 1e-5 and abs(z - zb) < 1e-5
                       for z, zb, _ in samples), samples
            # Float intersections can shift a discontinuity by one ULP; check
            # depth above, then compare opacity at the known plane boundaries.
            snapped = [(round(z, 5), round(zb, 5), a) for z, zb, a in samples]
            errors.append(curve_error(snapped, [(5, 5, .5), (back, back, 1)]))
        assert max(errors) <= report['deep_error'], (filename, errors)
        report.update(observed_objects=sorted(names), holdout_objects=list(holdouts.values()),
                      maximum_depth_curve_error=max(errors))
        reports[filename] = report
    if args.reference:
        before = args.reference / 'single' / 'shot_0001.deep.exr'
        after = root / 'single' / 'shot_0001.deep.exr'
        assert before.read_bytes() == after.read_bytes(), 'Qualified single-layer bytes changed'
        results['single_layer_reference'] = dict(path=str(before), identical=True,
                                                sha256=hashlib.sha256(after.read_bytes()).hexdigest())
    results['validation'] = reports
    (root / 'validation.json').write_text(json.dumps(results, indent=2))
    print('DEEP_MULTILAYER_VALIDATION_PASS', args.device, len(reports), 'files', flush=True)


if args.validate:
    validate()
    sys.exit(0)

import bpy
root.mkdir(parents=True, exist_ok=False)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.render.resolution_x = scene.render.resolution_y = 32
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.use_compositing = scene.render.use_sequencer = False
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
cscene = scene.cycles
cscene.device = 'CPU'
cscene.samples = 16
cscene.seed = 123
cscene.use_adaptive_sampling = cscene.use_denoising = False
cscene.pixel_filter_type = 'BOX'
cscene.filter_width = 1
cscene.deep_error = .001
cscene.deep_z_tolerance = 0
cscene.use_deep_ids = True
cscene.deep_max_events = 64
cscene.deep_memory_mb = 512
if args.device == 'OPTIX':
    preferences = bpy.context.preferences.addons['cycles'].preferences
    preferences.compute_device_type = 'OPTIX'
    preferences.refresh_devices()
    devices = [d for d in preferences.devices if d.type == 'OPTIX']
    assert devices, 'No OptiX device'
    for device in preferences.devices:
        device.use = device.id == devices[0].id
    cscene.device = 'GPU'

world = bpy.data.worlds.new('World')
world.use_nodes = True
scene.world = world
bpy.ops.object.camera_add(location=(0, 0, 8))
scene.camera = bpy.context.view_layer.objects.active
scene.camera.data.type = 'ORTHO'
scene.camera.data.ortho_scale = 6


def plane(name, location, scale, alpha=1):
    collection = bpy.data.collections.new(name)
    scene.collection.children.link(collection)
    bpy.ops.mesh.primitive_plane_add(size=2, location=location)
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.scale = (*scale, 1)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    diffuse = nodes.new('ShaderNodeBsdfDiffuse')
    closure = diffuse
    if alpha != 1:
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        closure = nodes.new('ShaderNodeMixShader')
        closure.inputs[0].default_value = alpha
        material.node_tree.links.new(transparent.outputs[0], closure.inputs[1])
        material.node_tree.links.new(diffuse.outputs[0], closure.inputs[2])
    material.node_tree.links.new(closure.outputs[0], output.inputs['Surface'])
    obj.data.materials.append(material)


plane('Back', (0, 0, 0), (2.8, 2.8))
plane('Front', (-1, 0, 2), (.7, 1))
plane('Transparent', (0, 0, 3), (1.8, 1.2), .5)
plane('Indirect', (-2, 1.9, 4), (.45, .45))
plane('Holdout', (2, 1.9, 4), (.45, .45))
plane('Excluded', (2, -1.9, 4), (.45, .45))
layer_a = bpy.context.view_layer
layer_a.name = 'A'
layer_b = scene.view_layers.new('B')
layer_c = scene.view_layers.new('C')
layers = [layer_a, layer_b, layer_c]
layer_a.layer_collection.children['Indirect'].indirect_only = True
layer_a.layer_collection.children['Holdout'].holdout = True
layer_b.layer_collection.children['Excluded'].exclude = True
results = dict(passed=False, device=args.device, files={}, tests={})


def configure(enabled=('A', 'B', 'C'), rendering=('A', 'B', 'C')):
    scene.render.use_single_layer = False
    for layer in layers:
        layer.cycles.use_pass_deep = layer.name in enabled
        layer.use = layer.name in rendering


def render(name, template='{view_layer}_####', frames=(1,), override=True):
    directory = root / name
    scene.render.filepath = str(directory / 'beauty_####')
    cscene.deep_output_path = str(directory / template) if override else ''
    if not override:
        scene.render.filepath = str(directory / template)
    scene.frame_start, scene.frame_end = frames[0], frames[-1]
    scene.frame_set(frames[0])
    assert bpy.ops.render.render(animation=len(frames) > 1) == {'FINISHED'}
    expected = []
    for layer in layers:
        if not (layer.use and layer.cycles.use_pass_deep):
            continue
        for frame in frames:
            filename = template.replace('{view_layer}', layer.name).replace('####', f'{frame:04}')
            path = directory / (filename + '.deep.exr')
            assert path.is_file() and path.stat().st_size, path
            results['files'][path.relative_to(root).as_posix()] = layer.name
            expected.append(path)
    assert sorted(directory.glob('*.deep.exr')) == sorted(expected)
    results['tests'][name] = [str(path) for path in expected]
    return expected


if args.cancel:
    import time
    import traceback

    assert not bpy.app.background
    configure()
    scene.render.resolution_x = scene.render.resolution_y = 512
    cscene.samples = 4096
    cscene.deep_samples = 8
    cscene.deep_memory_mb = 2048
    layer_a.samples = layer_c.samples = 1
    scene.render.filepath = str(root / 'beauty_####')
    cscene.deep_output_path = str(root / '{view_layer}_####')
    bpy.context.preferences.view.render_display_type = 'NONE'
    counts = dict(complete=0, cancel=0)

    def complete(*_):
        counts['complete'] += 1

    def cancelled(*_):
        counts['cancel'] += 1

    bpy.app.handlers.render_complete.append(complete)
    bpy.app.handlers.render_cancel.append(cancelled)
    started = time.monotonic()
    a_published = None
    invoked = False
    requested = False

    def step():
        global invoked, requested, a_published
        try:
            assert time.monotonic() - started < 180, 'Cancellation timed out'
            if not invoked:
                # Exercise the actual foreground Single Layer path as well as
                # the background rejection case; B and C stay render-enabled.
                bpy.context.window.view_layer = layer_a
                scene.render.use_single_layer = True
                cscene.deep_output_path = str(root / 'single-collision' / 'shot_####')
                try:
                    bpy.ops.render.render()
                except RuntimeError as error:
                    assert 'would all write' in str(error) and 'add {view_layer}' in str(error)
                    results['tests']['foreground_single_collision'] = str(error)
                else:
                    raise AssertionError('Single Layer accepted colliding deep paths')
                assert not (root / 'single-collision').exists()
                cscene.deep_output_path = str(root / 'single-unique' / '{view_layer}_####')
                assert bpy.ops.render.render() == {'FINISHED'}
                assert [p.name for p in (root / 'single-unique').glob('*.deep.exr')] == ['A_0001.deep.exr']
                results['tests']['foreground_single_only_A_rendered'] = True
                scene.render.use_single_layer = False
                cscene.deep_output_path = str(root / '{view_layer}_####')
                counts.update(complete=0, cancel=0)
                assert bpy.ops.render.render('INVOKE_DEFAULT') == {'RUNNING_MODAL'}
                invoked = True
                return .1
            running = bpy.app.is_job_running('RENDER')
            if (root / 'A_0001.deep.exr').exists() and a_published is None:
                a_published = time.monotonic()
            if not requested and a_published is not None and time.monotonic() - a_published >= 2:
                assert running and not (root / 'B_0001.deep.exr').exists()
                (root / 'cancel-request.json').write_text(json.dumps(dict(layer='B')))
                requested = True
            if running:
                return .1
            assert requested and counts == dict(complete=0, cancel=1), counts
            assert sorted(p.name for p in root.glob('*.deep.exr')) == ['A_0001.deep.exr']
            assert not list(root.rglob('*.partial-*'))
            results.update(passed=True, counts=counts, no_partial_files=True)
            print('DEEP_MULTILAYER_CANCEL_PASS', flush=True)
        except Exception:
            results['error'] = traceback.format_exc()
            traceback.print_exc()
        (root / 'results.json').write_text(json.dumps(results, indent=2))
        bpy.ops.wm.quit_blender()
        return None

    bpy.app.timers.register(step, first_interval=5)
else:
    configure(('A',), ('A',))
    render('single', 'shot_####', override=False)
    if not args.baseline:
        configure()
        render('all')
        for layer in layers:
            configure((layer.name,), (layer.name,))
            path, = render('alone-' + layer.name)
            if args.device == 'CPU':
                assert path.read_bytes() == (root / 'all' / path.name).read_bytes(), layer.name
        results['tests']['isolation'] = 'byte-identical' if args.device == 'CPU' else 'existence'
        configure()
        render('animation', frames=(1, 2, 3))
        if args.device == 'CPU':
            def reject(name, override, single=False, disabled=False):
                configure(('A', 'B'), ('A',) if disabled else ('A', 'B'))
                scene.render.use_single_layer = single
                scene.frame_set(1)
                scene.render.filepath = str(root / name / 'shot_####')
                cscene.deep_output_path = str(root / name / override) if override else ''
                try:
                    bpy.ops.render.render(layer='A' if single else '')
                except RuntimeError as error:
                    message = str(error)
                    assert 'Deep layers ' in message and 'would all write' in message, message
                    assert 'add {view_layer} to Output > Deep > Path Override' in message, message
                    assert 'A' in message and 'B' in message, message
                    results['tests'][name] = message
                else:
                    raise AssertionError('Colliding paths were accepted: ' + name)
                assert not (root / name).exists(), 'Preflight created output before rejection'

            reject('collision-blank', '')
            reject('collision-override', 'shot_####')
            reject('collision-single-layer', '', single=True)
            reject('collision-disabled', '', disabled=True)
            configure(('A', 'B'), ('A',))
            layer_b.name = 'a'
            scene.render.filepath = str(root / 'collision-case' / 'beauty_####')
            cscene.deep_output_path = str(root / 'collision-case' / '{view_layer}_####')
            try:
                bpy.ops.render.render()
            except RuntimeError as error:
                assert 'would all write' in str(error), str(error)
                results['tests']['collision-case'] = str(error)
            else:
                raise AssertionError('Case-insensitive collision was accepted')
            assert not (root / 'collision-case').exists()
            layer_b.name = 'B'
            configure(('A', 'B'), ('A',))
            render('disabled-unique')
            configure(('A',), ('A', 'B'))
            render('mixed')
            # The same resolver accepts the token in the default deep path too.
            # No beauty is written in this still render; never recommend this for beauty.
            configure()
            render('default-token', override=False)
            bpy.ops.wm.save_as_mainfile(filepath=str(root / 'fixture.blend'))
            scene.render.filepath = str(root / 'relative' / 'beauty_####')
            cscene.deep_output_path = '//relative/{blend_name}_{view_layer}_####'
            assert bpy.ops.render.render() == {'FINISHED'}
            for layer in layers:
                path = root / 'relative' / f'fixture_{layer.name}_0001.deep.exr'
                assert path.is_file()
                results['files'][path.relative_to(root).as_posix()] = layer.name
            results['tests']['relative_blend_name'] = True
    results['passed'] = True
    (root / 'results.json').write_text(json.dumps(results, indent=2))
    print('DEEP_MULTILAYER_RENDER_PASS', args.device, len(results['files']), 'files', flush=True)
