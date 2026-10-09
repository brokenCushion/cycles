"""Read-only inventory of the supplied scene for native Blender deep integration."""
import bpy
import collections
import json
import re
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
# Include mesh-shaped media: the deep triangle loop scans the entire evaluated
# mesh, including triangles using surface-only materials.
volume_materials = {m for m in bpy.data.materials if m.node_tree and any(
    n.type == 'OUTPUT_MATERIAL' and n.is_active_output and n.inputs['Volume'].is_linked
    for n in m.node_tree.nodes)}
mesh_volume_bounds = []
for obj in scene.objects:
    if obj.type != 'MESH' or not any(m in volume_materials for m in obj.data.materials):
        continue
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        mesh_volume_bounds.append({'object': obj.name, 'bound_triangles': len(mesh.loop_triangles),
                                   'count_basis': 'evaluated mesh, all material slots'})
    finally:
        evaluated.to_mesh_clear()
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
    'mesh_volume_bounds': mesh_volume_bounds,
    'volume_ray_marching': scene.cycles.volume_biased,
    # Cycles VolumeMeshBuilder uses six bounding-box quads when ray marching
    # is disabled. Sparse ray-marching meshes need renderer-side counts.
    'volume_bounds': [
        {'object': o.name, 'data': o.data.name, 'file': bpy.path.abspath(o.data.filepath),
         'bound_triangles_if_nonempty': None if scene.cycles.volume_biased else 12,
         'count_basis': 'src/scene/volume.cpp:generate_vertices_and_quads',
         'precision': o.data.render.precision}
        for o in scene.objects if o.type == 'VOLUME'],
    'camera_dof': scene.camera.data.dof.use_dof if scene.camera else None,
    'object_types': dict(collections.Counter(o.type for o in scene.objects)),
    'modifiers': dict(collections.Counter(m.type for o in scene.objects for m in o.modifiers if m.show_render)),
    'world_nodes': dict(collections.Counter(n.bl_idname for n in nodes(scene.world.node_tree))),
    'materials': materials,
    'images': [{'name': i.name, 'path': i.filepath, 'packed': bool(i.packed_file),
                'source': i.source} for i in bpy.data.images],
}
if '--renderer-log' in sys.argv:
    renderer_log = Path(sys.argv[sys.argv.index('--renderer-log') + 1])
    report['renderer_volume_bounds'] = [
        {'object': name, 'bound_triangles': int(count)}
        for name, count in re.findall(r'Deep volume bound: object=(.*?) triangles=(\d+)',
                                     renderer_log.read_text())]
    if not report['renderer_volume_bounds']:
        raise ValueError('Renderer log contains no measured volume bounds')
    report['renderer_volume_bounds_log'] = str(renderer_log.resolve())
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2))
print('DEEP_SCENE_INVENTORY', output)
