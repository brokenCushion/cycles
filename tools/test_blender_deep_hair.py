"""Static Ribbon fixtures. Blender --python SCRIPT -- FRESH_ROOT --baseline.

The baseline runs before curve admission: visible curves must reject, while
excluded, indirect-only and camera-invisible curves must not block deep output.
"""
import json
from pathlib import Path
import sys


def cases():
    names = [f'single_{segments}_{background}' for segments in (1, 4)
             for background in ('world', 'plane')]
    names += [f'single_{segments}_constant_world' for segments in (1, 4)]
    names += ['dense', 'fold', 'adjacent', 'interleaved', 'two_objects', 'instance',
              'junkshop', 'indirect', 'excluded', 'camera_off', 'shared', 'holdout',
              'occluded', 'occluding', 'frame1', 'frame2', 'grazing', 'tip', 'inside']
    names += [f'{model}_{mix}' for model in ('legacy', 'transmission', 'chiang', 'huang')
              for mix in ('opaque', 'constant', 'varying')]
    names += [f'chain_{count}' for count in (8, 16, 17, 32, 64, 65)]
    return names


def material(model='legacy', mix='opaque'):
    import bpy
    mat = bpy.data.materials.new('Hair'); mat.use_nodes = True
    nodes = mat.node_tree.nodes; nodes.clear(); link = mat.node_tree.links.new
    if model == 'junkshop':
        graph = json.loads(Path(__file__).with_name('test_blender_deep_hair_material.json').read_text())
        restored = []
        for specification in graph['nodes']:
            node = nodes.new(specification['type']); node.name = specification['name']
            for name, value in specification['properties'].items(): setattr(node, name, value)
            assert len(node.inputs) == len(specification['inputs'])
            for socket, value in zip(node.inputs, specification['inputs']):
                if value is not None: socket.default_value = value
            restored.append(node)
        for source, output, target, input_index in graph['links']:
            link(restored[source].outputs[output], restored[target].inputs[input_index])
        assert len(nodes) == 6 and len(mat.node_tree.links) == 5
        return mat
    out = nodes.new('ShaderNodeOutputMaterial')
    hair = nodes.new('ShaderNodeBsdfHairPrincipled' if model in ('chiang', 'huang')
                     else 'ShaderNodeBsdfHair')
    if model in ('chiang', 'huang'): hair.model = model.upper()
    elif model == 'transmission': hair.component = 'Transmission'
    if mix != 'opaque':
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        blend = nodes.new('ShaderNodeMixShader'); blend.inputs[0].default_value = .1
        link(transparent.outputs[0], blend.inputs[1]); link(hair.outputs[0], blend.inputs[2])
        if mix == 'varying':
            geometry = nodes.new('ShaderNodeNewGeometry'); noise = nodes.new('ShaderNodeTexNoise')
            link(geometry.outputs['Position'], noise.inputs['Vector'])
            link(noise.outputs['Fac'], blend.inputs[0])
        hair = blend
    link(hair.outputs[0], out.inputs['Surface'])
    return mat


def groom(name, strands, mat, collection, radius=.12):
    import bpy
    data = bpy.data.hair_curves.new(name)
    data.add_curves([len(points) for points in strands])
    kinds = data.attributes.get('curve_type') or data.attributes.new('curve_type', 'INT8', 'CURVE')
    kinds.data.foreach_set('value', [0] * len(strands))  # Catmull-Rom, not polyline.
    points = [p for strand in strands for p in strand]
    data.attributes['position'].data.foreach_set('vector', [v for p in points for v in p])
    radii = data.attributes.new('radius', 'FLOAT', 'POINT')
    radii.data.foreach_set('value', [radius] * len(points))
    obj = bpy.data.objects.new(name, data); collection.objects.link(obj)
    data.materials.append(mat)
    return obj


def fixture(name):
    import bpy
    from test_scan_blender_deep_scene import fixture as base
    scene, plane, _, _, _ = base()
    scene.cycles_curves.shape = 'RIBBONS'
    scene.cycles.deep_z_tolerance = 0
    scene.cycles.deep_write_diagnostics = True
    scene.cycles.use_deep_ids = True
    scene.cycles.transparent_max_bounces = 128
    scene.cycles.samples = 4
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_depth = '32'
    scene.render.image_settings.exr_codec = 'ZIP'
    layer = scene.view_layers[0]
    layer.cycles.deep_volume_mode = 'OFF'
    layer.cycles.deep_z_tolerance_mode = 'CUSTOM'; layer.cycles.deep_z_tolerance = 0
    layer.cycles.deep_max_events_mode = 'CUSTOM'; layer.cycles.deep_max_events = 64
    if name in ('chain_8', 'chain_16', 'chain_17'): layer.cycles.deep_max_events = 16
    collection = bpy.data.collections.new('Groom'); scene.collection.children.link(collection)
    model, mix = 'legacy', 'opaque'
    if '_constant_' in name: mix = 'constant'
    if name.split('_')[0] in ('legacy', 'transmission', 'chiang', 'huang'):
        model, mix = name.split('_')
    if name == 'junkshop': model = name
    if name.startswith('chain_') or name in ('dense', 'interleaved', 'fold', 'adjacent'):
        mix = 'constant'
    mat = material(model, mix)
    strands = [[(0, -1, 2), (0, 1, 2)]]
    if name.startswith('single_4'):
        strands = [[(0, y, 2) for y in (-1, -.5, 0, .5, 1)]]
    if name.startswith('chain_'):
        count = int(name.split('_')[1])
        strands = [[(0, -1.5, 1 + i * .05), (0, 1.5, 1 + i * .05)] for i in range(count)]
    elif name == 'dense':
        strands = [[(x / 12, -1.5, 1 + z * .1), (x / 12, 1.5, 1 + z * .1)]
                   for x in range(-18, 19) for z in range(8)]
    elif name == 'fold':
        strands = [[(-.5, -1, 2), (.5, -.5, 3), (-.5, 0, 2), (.5, .5, 3), (-.5, 1, 2)]]
    elif name == 'adjacent':
        strands = [[(0, -1, 2), (0, 0, 2)], [(0, 0, 2), (0, 1, 2)]]
    elif name == 'interleaved':
        strands = [[(0, -1, 3), (0, 1, 3), (0, 1, 1), (0, -1, 1)],
                   [(0, -1, 2), (0, 1, 2)]]
    obj = groom('Strands', strands, mat, collection, .25 if name.startswith('single_') else .12)
    if name in ('two_objects', 'shared'):
        obj.visible_camera = name != 'shared'
        other = bpy.data.objects.new('Other', obj.data); collection.objects.link(other)
        other.location.x = .4
    if name == 'instance':
        scene.collection.children.unlink(collection)
        inst = bpy.data.objects.new('Instance', None); scene.collection.objects.link(inst)
        inst.instance_type = 'COLLECTION'; inst.instance_collection = collection
        inst.scale = (.8, 1.1, 1)
    elif name == 'indirect': layer.layer_collection.children['Groom'].indirect_only = True
    elif name == 'excluded': layer.layer_collection.children['Groom'].exclude = True
    elif name == 'camera_off': obj.visible_camera = False
    elif name == 'holdout': layer.layer_collection.children['Groom'].holdout = True
    elif name == 'occluded': plane.location.z = 4
    elif name == 'frame2':
        scene.frame_set(2)
        for point in obj.data.attributes['position'].data: point.vector.x += .5
    elif name == 'grazing': obj.location.x = .12
    elif name == 'tip': obj.location.y = 1
    elif name == 'inside': scene.camera.location.z = 2
    if name.endswith('_world') or name.startswith('chain_') or name == 'inside':
        bpy.data.objects.remove(plane, do_unlink=True)
    return scene


def baseline(root):
    import bpy
    from scan_blender_deep_scene import scan
    root.mkdir(parents=True, exist_ok=False)
    results = []
    for name in cases():
        scene = fixture(name)
        scene.cycles.deep_output_path = str(root / name / '{view_layer}_####')
        scene.render.filepath = str(root / name / 'beauty_####')
        report = scan(scene)
        expected = name not in ('indirect', 'excluded', 'camera_off')
        error = None
        try: bpy.ops.render.render()
        except RuntimeError as exc: error = str(exc)
        files = list((root / name).glob('*.deep.exr'))
        passed = (bool(report['blocks']) == expected and
                  ((error is not None and 'only static polygon meshes' in error and not files)
                   if expected else (error is None and bool(files))))
        results.append(dict(case=name, expected_rejection=expected, scanner=report,
                            error=error, files=[str(p) for p in files], passed=passed))
        (root / 'results.json').write_text(json.dumps(results, indent=2))
        print('HAIR_BASELINE', name, 'PASS' if passed else 'FAIL', flush=True)
        assert passed, results[-1]
    print('HAIR_BASELINE_PASS', len(results), flush=True)


def control(root, device='CPU'):
    import bpy
    import hashlib
    root.mkdir(parents=True, exist_ok=False)
    scene = fixture('single_1_plane')
    bpy.data.objects.remove(bpy.data.objects['Strands'], do_unlink=True)
    if device != 'CPU':
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = device; prefs.refresh_devices()
        candidates = [d for d in prefs.devices if d.type == device]
        assert candidates, device
        for d in prefs.devices: d.use = d.id == candidates[0].id and d.type == device
        scene.cycles.device = 'GPU'
    layer = scene.view_layers[0]
    layer.cycles.denoising_store_passes = True
    layer.cycles.pass_debug_sample_count = True
    digest = hashlib.sha256(Path(bpy.app.binary_path).read_bytes()).hexdigest()
    source = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    for label in [*(f'off{i}' for i in range(1 if device == 'CPU' else 5)), 'on']:
        directory = root / label; directory.mkdir()
        layer.cycles.use_pass_deep = label == 'on'
        scene.cycles.deep_output_path = str(directory / 'control_####')
        scene.render.filepath = str(directory / 'beauty.exr')
        scene.render.image_settings.color_depth = '32'
        scene.render.image_settings.exr_codec = 'ZIP'
        bpy.ops.render.render(write_still=True)
        scene.render.image_settings.media_type = 'MULTI_LAYER_IMAGE'
        scene.render.image_settings.color_depth = '32'
        scene.render.image_settings.exr_codec = 'ZIP'
        scene.render.image_settings.file_format = 'OPEN_EXR_MULTILAYER'
        scene.render.image_settings.color_depth = '32'
        scene.render.image_settings.exr_codec = 'ZIP'
        scene.render.image_settings.use_exr_interleave = True
        scene.render.image_settings.color_depth = '32'
        scene.render.image_settings.exr_codec = 'ZIP'
        bpy.data.images['Render Result'].save_render(str(directory / 'render-passes.exr'), scene=scene)
        # IMAGE selects PNG temporarily; restore EXR before setting its FLOAT depth.
        scene.render.image_settings.media_type = 'IMAGE'
        scene.render.image_settings.file_format = 'OPEN_EXR'
        scene.render.image_settings.color_depth = '32'
        scene.render.image_settings.exr_codec = 'ZIP'
        settings = dict(renderer_sha256=digest, source_sha256=source, source_kind='procedural fixture script',
                        device=device, deep=label == 'on', samples=4, resolution=[32, 32], percentage=100,
                        frame=scene.frame_current, adaptive=False, adaptive_threshold=scene.cycles.adaptive_threshold,
                        adaptive_min_samples=scene.cycles.adaptive_min_samples, seed=scene.cycles.seed,
                        use_animated_seed=scene.cycles.use_animated_seed, denoising=False,
                        denoiser=scene.cycles.denoiser, denoising_use_gpu=scene.cycles.denoising_use_gpu,
                        pixel_filter=scene.cycles.pixel_filter_type, threads=4, save_render_passes=True,
                        diagnostic_sample_count_pass=True,
                        camera_world_matrix=[list(row) for row in scene.camera.matrix_world],
                        camera_frame=[list(v) for v in scene.camera.data.view_frame(scene=scene)])
        (directory / 'render.json').write_text(json.dumps(settings, indent=2))
    (root / 'results.json').write_text(json.dumps(dict(width=32, height=32, samples=4, device=device)))
    print('HAIR_DIAGNOSTIC_CONTROL_RENDERED', flush=True)


def float_channels(path):
    import numpy as np
    import OpenEXR
    channels = OpenEXR.File(str(path), separate_channels=True).channels()
    bad = sorted(k for k, c in channels.items() if c.pixels.dtype != np.float32)
    assert not bad, f'export precision mismatch: {path}: non-FLOAT channels {bad}'
    return channels


def validate_control(root):
    import numpy as np
    from validate_blender_deep_hair import validate
    result = validate(root / 'on/control_0001.deep.exr', 32, 32, 4)
    assert result['maximum_events'] == 1 and result['maximum_raw_hits'] >= 1, result
    device = json.loads((root / 'results.json').read_text())['device']
    for label in [*(f'off{i}' for i in range(1 if device == 'CPU' else 5)), 'on']:
        for filename in ('beauty.exr', 'render-passes.exr'):
            float_channels(root / label / filename)
    if device == 'CPU':
        for filename in ('beauty.exr', 'render-passes.exr'):
            before = float_channels(root / 'on' / filename)
            after = float_channels(root / 'off0' / filename)
            assert before.keys() == after.keys()
            assert all(np.array_equal(before[k].pixels, after[k].pixels) for k in before)
        result['cpu_beauty_exact'] = True
    else:
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/deep'))
        from cuda_beauty_gate import validate_cuda_beauty
        policy = validate_cuda_beauty(root / 'on', [root / f'off{i}' for i in range(5)],
                                      reader_backend='openexr')
        assert policy['passed'], root / 'on/cuda_beauty_validation.json'
        result['gpu_beauty_policy_passed'] = True
    (root / 'validation.json').write_text(json.dumps(result, indent=2))
    print('HAIR_DIAGNOSTIC_CONTROL_PASS', result, flush=True)


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    assert len(args) == 2, args
    {'--baseline': baseline, '--control': control, '--validate-control': validate_control,
     '--control-CUDA': lambda root: control(root, 'CUDA'),
     '--control-OPTIX': lambda root: control(root, 'OPTIX')}[args[1]](Path(args[0]))
