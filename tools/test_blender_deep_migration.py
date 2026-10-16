"""Create with the old Blender, then load/test with the patched Blender.

Old: -b --factory-startup --python SCRIPT -- DIRECTORY --write-legacy
New: -b DIRECTORY/legacy.blend --python SCRIPT -- DIRECTORY
"""
import bpy
import json
from pathlib import Path
import sys

args = sys.argv[sys.argv.index('--') + 1:]
root = Path(args[0]).resolve()
scene = bpy.context.scene
settings = {
    'deep_output_path': '//legacy/shot_####',
    'use_deep_volume': True,
    'use_deep_volume_shader_eval': True,
    'deep_volume_step': 0.125,
    'use_deep_ids': True,
    'deep_error': 0.002,
    'deep_z_tolerance': 0.0002,
    'deep_samples': 7,
    'deep_max_events': 8192,
    'deep_memory_mb': 1024,
}
if '--write-legacy' in args:
    root.mkdir(parents=True, exist_ok=False)
    assert not hasattr(bpy.context.view_layer.cycles, 'use_pass_deep')
    scene.render.engine = 'CYCLES'
    bpy.context.view_layer.name = 'LegacyRenderLayer'
    scene.view_layers.new('DisabledLayer').use = False
    scene.cycles.use_deep_output = True
    for name, value in settings.items():
        setattr(scene.cycles, name, value)
    expected = {name: getattr(scene.cycles, name) for name in settings}
    (root / 'expected.json').write_text(json.dumps(expected, indent=2))
    bpy.ops.wm.save_as_mainfile(filepath=str(root / 'legacy.blend'))
else:
    from cycles.version_update import migrate_deep
    expected = json.loads((root / 'expected.json').read_text())

    def check_settings():
        assert {name: getattr(bpy.context.scene.cycles, name) for name in settings} == expected

    assert scene.view_layers['LegacyRenderLayer'].cycles.use_pass_deep
    assert not scene.view_layers['DisabledLayer'].cycles.use_pass_deep
    assert not scene.cycles.use_deep_output
    assert not scene.cycles.deep_write_diagnostics
    check_settings()
    migrate_deep(None)
    check_settings()
    assert scene.view_layers['LegacyRenderLayer'].cycles.use_pass_deep
    scene.view_layers['LegacyRenderLayer'].cycles.use_pass_deep = False
    migrate_deep(None)
    assert not scene.view_layers['LegacyRenderLayer'].cycles.use_pass_deep
    bpy.ops.wm.save_as_mainfile(filepath=str(root / 'migrated-disabled.blend'))
    bpy.ops.wm.open_mainfile(filepath=str(root / 'migrated-disabled.blend'))
    assert not bpy.context.scene.view_layers['LegacyRenderLayer'].cycles.use_pass_deep
    check_settings()
    (root / 'results.json').write_text(json.dumps({
        'legacy_enable_migrated': True, 'all_settings_preserved': True,
        'disabled_layer_unchanged': True, 'idempotent': True,
        'explicit_disable_survives_reload': True,
    }, indent=2))
    print('DEEP_MIGRATION_TESTS_PASS', root)
