"""Read-only inventory of the supplied scene for native Blender deep integration."""
import bpy
import collections
import json
from pathlib import Path
import sys

output = Path(sys.argv[sys.argv.index('--') + 1])
scene = bpy.context.scene

def nodes(tree, seen=None):
    seen = set() if seen is None else seen
    if not tree or tree.as_pointer() in seen:
        return []
    seen.add(tree.as_pointer())
    result = list(tree.nodes)
    for node in tree.nodes:
        if node.type == 'GROUP':
            result.extend(nodes(node.node_tree, seen))
    return result

materials = []
for material in bpy.data.materials:
    shader_nodes = nodes(material.node_tree)
    transmission = []
    for node in shader_nodes:
        if node.type == 'BSDF_PRINCIPLED':
            socket = node.inputs.get('Transmission Weight')
            if socket and (socket.is_linked or socket.default_value != 0):
                transmission.append({'node': node.name, 'linked': socket.is_linked,
                                     'value': socket.default_value})
    materials.append({'name': material.name,
                      'nodes': dict(collections.Counter(n.bl_idname for n in shader_nodes)),
                      'transmission': transmission})
report = {
    'blender': bpy.app.version_string,
    'build_hash': bpy.app.build_hash.decode(),
    'file': bpy.data.filepath,
    'scene': scene.name,
    'frame': scene.frame_current,
    'camera': scene.camera.name if scene.camera else None,
    'camera_type': scene.camera.data.type if scene.camera else None,
    'resolution': [scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage],
    'samples': scene.cycles.samples,
    'adaptive': scene.cycles.use_adaptive_sampling,
    'denoise': scene.cycles.use_denoising,
    'motion': scene.render.use_motion_blur,
    'camera_dof': scene.camera.data.dof.use_dof if scene.camera else None,
    'object_types': dict(collections.Counter(o.type for o in scene.objects)),
    'modifiers': dict(collections.Counter(m.type for o in scene.objects for m in o.modifiers if m.show_render)),
    'world_nodes': dict(collections.Counter(n.bl_idname for n in nodes(scene.world.node_tree))),
    'materials': materials,
    'images': [{'name': i.name, 'path': i.filepath, 'packed': bool(i.packed_file),
                'source': i.source} for i in bpy.data.images],
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2))
print('DEEP_SCENE_INVENTORY', output)
