"""Read-only deep preflight prediction; run inside the custom Blender, never renders."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import traceback

import bpy

# validate_shader(): Blender IDs -> exact NODE_DEFINE names, qualified by Step 2.
NODE_RULES = {
    'BsdfDiffuse': 'diffuse_bsdf', 'BsdfPrincipled': 'principled_bsdf',
    'BsdfGlass': 'glass_bsdf', 'BsdfTranslucent': 'translucent_bsdf',
    'BsdfTransparent': 'transparent_bsdf', 'Emission': 'emission', 'Holdout': 'holdout',
    'MixShader': 'mix_closure', 'AddShader': 'add_closure',
    'TexChecker': 'checker_texture', 'TexCoord': 'texture_coordinate',
    'Mapping': 'mapping', 'TexNoise': 'noise_texture', 'TexGradient': 'gradient_texture',
    'ValToRGB': 'rgb_ramp', 'Invert': 'invert', 'MixRGB': 'mix_color',
    'Bump': 'bump', 'TexImage': 'image_texture', 'Math': 'math', 'Clamp': 'clamp',
    'RGB': 'color', 'Value': 'value', 'NewGeometry': 'geometry', 'Fresnel': 'fresnel',
    'NormalMap': 'normal_map', 'BrightContrast': 'brightness_contrast',
    'HueSaturation': 'hsv', 'RGBCurve': 'rgb_curves', 'SeparateColor': 'separate_color',
    'CombineColor': 'combine_color', 'RGBToBW': 'rgb_to_bw', 'Attribute': 'attribute',
    'TexWave': 'wave_texture', 'TexBrick': 'brick_texture',
    'BsdfHair': 'hair_bsdf', 'BsdfHairPrincipled': 'principled_hair_bsdf',
    'BsdfGlossy': 'glossy_bsdf',
    'BsdfAnisotropic': 'glossy_bsdf', 'BsdfMetallic': 'metallic_bsdf',
    'BsdfRefraction': 'refraction_bsdf', 'BsdfSheen': 'sheen_bsdf',
    'BsdfToon': 'toon_bsdf', 'SubsurfaceScattering': 'subsurface_scattering',
    'VectorMath': 'vector_math', 'MapRange': 'map_range', 'SeparateXYZ': 'separate_xyz',
    'CombineXYZ': 'combine_xyz', 'VectorRotate': 'vector_rotate', 'FloatCurve': 'float_curve',
    'VectorCurve': 'vector_curves', 'Normal': 'normal', 'Blackbody': 'blackbody',
    'Wavelength': 'wavelength', 'VectorTransform': 'vector_transform',

}
# validate_shader() special cases; deep_closure_can_transmit() controls opacity edges.
LIGHT_PATH_OUTPUTS = {'Is Camera Ray', 'Is Shadow Ray', 'Ray Length', 'Ray Depth',
    'Is Diffuse Ray','Is Glossy Ray','Is Singular Ray','Is Reflection Ray','Is Transmission Ray',
    'Is Volume Scatter Ray','Diffuse Depth','Glossy Depth','Transmission Depth','Portal Depth'}
SPECIAL_NODES = {'LightPath': 'light_path', 'AmbientOcclusion': 'ambient_occlusion', 'Bevel': 'bevel'}
REJECTED_NODES = {
    'BsdfRayPortal': 'ray_portal_bsdf',
    'TexVoronoi':'voronoi_texture','TexMagic':'magic_texture','TexWhiteNoise':'white_noise_texture',
    'TexGabor':'gabor_texture','UVMap':'uv_map','VertexColor':'vertex_color','ObjectInfo':'object_info',
    'Tangent':'tangent','LayerWeight':'layer_weight','CameraData':'camera','ParticleInfo':'particle_info',
    'Wireframe':'wireframe','Script':'script',
}
def effective_deep_settings(scene, layer):
    """Match adapter overrides; older builds have only scene properties."""
    volume_mode = getattr(layer.cycles, 'deep_volume_mode', 'SCENE')
    tolerance_mode = getattr(layer.cycles, 'deep_z_tolerance_mode', 'SCENE')
    volume = scene.cycles.use_deep_volume if volume_mode == 'SCENE' else volume_mode == 'ON'
    tolerance = layer.cycles.deep_z_tolerance if tolerance_mode == 'CUSTOM' else scene.cycles.deep_z_tolerance
    return volume, tolerance


# validate_deep_scene(): settings which RNA exposes without changing scene data.
SETTING_RULES = [
    ('samples', lambda s,l: not 1 <= (l.samples or s.cycles.samples) <= 4096, 'requires 1..4096 samples'),
    ('time_limit', lambda s,l: s.cycles.time_limit != 0, 'time limit unsupported'),
    ('sample_subset', lambda s,l: getattr(s.cycles, 'use_sample_subset', False), 'sample subsets unsupported'),
    ('guiding', lambda s,l: getattr(s.cycles, 'use_guiding', False), 'path guiding unsupported'),
    ('denoise_upscale', lambda s,l: s.cycles.use_denoising and getattr(s.cycles, 'denoising_upscale_factor', 1) != 1, 'denoise upscaling unsupported'),
    ('transparent_glass', lambda s,l: s.cycles.film_transparent_glass, 'transparent glass unsupported'),
    ('multiview', lambda s,l: s.render.use_multiview, 'deep output requires mono rendering'),
    ('border', lambda s,l: s.render.use_border and (s.render.border_min_x != 0 or s.render.border_min_y != 0 or s.render.border_max_x != 1 or s.render.border_max_y != 1), 'camera borders unsupported'),
    ('rolling_shutter', lambda s,l: s.cycles.rolling_shutter_type != 'NONE', 'rolling shutter unsupported'),
    ('filter', lambda s,l: s.cycles.pixel_filter_type not in {'BOX','GAUSSIAN','BLACKMAN_HARRIS'} or not math.isfinite(s.cycles.filter_width) or s.cycles.filter_width <= 0, 'invalid camera filter'),
    ('strict_ids', lambda s,l: s.cycles.use_deep_ids and s.cycles.deep_error == 0, 'deep IDs require numeric error'),
    ('error', lambda s,l: s.cycles.deep_error != 0 and not 1e-6 < s.cycles.deep_error <= .010000001, 'invalid deep error'),
    ('depth_tolerance', lambda s,l: not math.isfinite(effective_deep_settings(s,l)[1]) or effective_deep_settings(s,l)[1] < 0, 'invalid surface depth tolerance'),
    ('deep_samples', lambda s,l: s.cycles.deep_samples < 0, 'invalid deep sample limit'),
    ('memory', lambda s,l: s.cycles.deep_memory_mb <= 0, 'working memory must be positive'),
    ('volume_step', lambda s,l: effective_deep_settings(s,l)[0] and (not math.isfinite(s.cycles.deep_volume_step) or s.cycles.deep_volume_step < 0), 'invalid volume step'),
]
SUPPORT_VOLUME = '| Geometry | Polygon surfaces/rigid instances; static homogeneous boundaries and native scalar NanoVDB density with linear interpolation. |'
UNDECIDABLE = [
    'Cycles optimized/finalized graph, numeric conversions, shader compilation and OSL optimized-group queries are not exposed to Python; constant folding may remove a reported node (documented possible false positive).',
    'RNA depsgraphs use their exposed evaluation mode; render-only modifier/particle differences, Cycles culling, subdivision synchronization, generated procedurals and final shared-geometry assignment require render-time validation.',
    'Compiled volume extinction, grid metadata/interpolation, manifold triangle boundaries, voxel step, closure capacity and NanoVDB build support require Cycles evaluation; volume findings are conservative warnings unless an RNA contradiction proves rejection.',
    'Camera shutter samples, animated FOV and finite rigid motion transforms at all shutter times require temporal Cycles synchronization; animation/deform risks are warned or conservatively blocked.',
    'Device availability, tiling selected by the host, pixel-size overrides, custom pixel jitter, AO bounce overrides, bake state and 30-bit synced object count require session state; render-context restrictions remain authoritative.',
    'Murmur ID collisions and holdout-closure manifests require the final synchronized object names and compiled shader graph.',
]


def node_type(node):
    key = node.bl_idname.removeprefix('ShaderNode')
    if key == 'Mix':
        return {'RGBA':'mix_color','FLOAT':'mix_float','VECTOR': 'mix_vector_non_uniform' if node.factor_mode == 'NON_UNIFORM' else 'mix_vector'}[node.data_type]
    if key == 'MapRange' and node.data_type == 'FLOAT_VECTOR':
        return 'vector_map_range'
    return {**NODE_RULES, **SPECIAL_NODES, **REJECTED_NODES}.get(key, node.bl_idname)


def upstream(socket, contexts=()):
    """Flatten group interfaces and muted-node bypasses without editing the graph."""
    if not socket.is_linked:
        return []
    result = []
    for link in socket.links:
        node, out = link.from_node, link.from_socket
        if node.type == 'REROUTE':
            result += upstream(node.inputs[0], contexts)
        elif node.mute:
            for bypass in node.internal_links:
                if bypass.to_socket == out:
                    result += upstream(bypass.from_socket, contexts)
        elif node.type == 'GROUP' and node.node_tree:
            active = next((n for n in node.node_tree.nodes if n.type == 'GROUP_OUTPUT' and n.is_active_output), None)
            if active:
                inner = next((s for s in active.inputs if s.identifier == out.identifier), None)
                if inner:
                    result += upstream(inner, contexts + (node,))
        elif node.type == 'GROUP_INPUT' and contexts:
            parent = contexts[-1]
            outer = next((s for s in parent.inputs if s.identifier == out.identifier), None)
            if outer:
                result += upstream(outer, contexts[:-1])
        else:
            result.append((node, out, contexts))
    return result


def can_transmit(socket, contexts=(), seen=None):
    seen = set() if seen is None else seen
    for node, out, ctx in upstream(socket, contexts):
        key = (node.as_pointer(), tuple(n.as_pointer() for n in ctx))
        if key in seen: continue
        seen.add(key)
        if node.type == 'BSDF_TRANSPARENT': return True
        if node.type == 'BSDF_PRINCIPLED':
            alpha = node.inputs.get('Alpha')
            if alpha.is_linked or alpha.default_value < 1: return True
        if node.type in {'MIX_SHADER','ADD_SHADER'}:
            if any(can_transmit(s,ctx,seen) for s in node.inputs if s.type == 'SHADER'): return True
    return False


def inspect_material(material, add):
    tree = material.node_tree
    if not tree: return
    output = tree.get_output_node('CYCLES')
    if not output:
        add('BLOCK', 'material_output', 'surface shader must be connected', material=material.name)
        return
    if output.inputs['Displacement'].is_linked:
        add('BLOCK','displacement','material Displacement output is unsupported',material=material.name,node=output.name,node_type='output')
    if output.inputs['Volume'].is_linked:
        add('WARN','volume_shader','Volume shader requires render-time extinction qualification. SUPPORT.md: '+SUPPORT_VOLUME,material=material.name,node=output.name,node_type='output')
    pending = [(n,o,c,False) for n,o,c in upstream(output.inputs['Surface'])]
    seen = set()
    while pending:
        node, out, ctx, opacity = pending.pop()
        identity = (node.as_pointer(),out.identifier,tuple(n.as_pointer() for n in ctx),opacity)
        if identity in seen: continue
        seen.add(identity)
        kind = node_type(node)
        fields = dict(material=material.name,node='/'.join([n.name for n in ctx]+[node.name]),node_type=kind,blender_type=node.bl_idname)
        if kind in {'ambient_occlusion','bevel'}:
            if opacity: add('BLOCK','raytrace_opacity','ray-traced '+kind+' cannot drive deep opacity',**fields)
        elif kind == 'light_path':
            if out.name not in LIGHT_PATH_OUTPUTS:
                add('BLOCK','light_path','unsupported deep surface Light Path output: '+out.name,**fields)
        elif kind not in set(NODE_RULES.values()) | {'mix_float','mix_vector','mix_vector_non_uniform'}:
            add('BLOCK','node','unsupported deep surface node: '+kind,**fields)
        for inp in node.inputs:
            if not inp.enabled or not inp.is_linked: continue
            dependent = opacity
            if node.type == 'BSDF_TRANSPARENT' and inp.name == 'Color': dependent = True
            if node.type == 'BSDF_PRINCIPLED' and inp.name == 'Alpha': dependent = True
            if node.type == 'MIX_SHADER' and inp.type != 'SHADER':
                dependent |= any(can_transmit(s,ctx) for s in node.inputs if s.type == 'SHADER')
            pending += [(n,o,c,dependent) for n,o,c in upstream(inp,ctx)]


# Pinned source/blender/blenkernel/intern/path_templates.cc: general, render, node.
NATIVE_PATH_VARIABLES = {'project_name', 'project_root', 'blend_name', 'blend_dir',
    'blend_name_lib', 'blend_dir_lib', 'resolution_x', 'resolution_y', 'fps',
    'scene_name', 'camera_name', 'node_name'}
NUMERIC_PATH_VARIABLES = {'resolution_x', 'resolution_y', 'fps'}


def template_tokens(text, deep=False):
    """Mirror next_token/parse_format_specifier; yield literal text or (name, spec)."""
    i = 0
    while i < len(text):
        if text[i:i+2] in ('{{', '}}'):
            yield text[i]; i += 2
        elif text[i] == '}':
            raise ValueError('unescaped closing brace in path template')
        elif text[i] == '{':
            end = text.find('}', i+1)
            if end < 0 or '{' in text[i+1:end]:
                raise ValueError('unbalanced or nested brace in path template')
            name, colon, spec = text[i+1:end].partition(':')
            if colon and not re.fullmatch(r'#+|#*\.#+', spec):
                raise ValueError('bad path format specifier {'+text[i+1:end]+'}')
            if name not in NATIVE_PATH_VARIABLES | ({'view_layer'} if deep else set()):
                reason = 'unknown path variable {'+name+'}'
                if name == 'view_layer':
                    reason += '; it is only valid in Output > Deep > Path Override. Use {node_name} and name the node after the layer.'
                raise ValueError(reason)
            if colon and name not in NUMERIC_PATH_VARIABLES:
                raise ValueError('format specifier is not allowed for string/path variable {'+name+'}')
            yield (name, spec)
            i = end+1
        else:
            yield text[i]; i += 1


def inspect_paths(scene):
    findings = []
    def check(text, subject, deep=False):
        try: list(template_tokens(text, deep))
        except ValueError as error:
            findings.append(dict(level='BLOCK', rule='path_template', reason=subject+': '+str(error), path=text))
    check(scene.render.filepath, 'render.filepath')
    # rna_scene_api.cc exposes validation for this property only, without RNA edits.
    if not findings:
        try: scene.render.frame_path()
        except RuntimeError as error:
            findings.append(dict(level='BLOCK', rule='path_template', reason='render.filepath: '+str(error)))
    check(scene.cycles.deep_output_path, 'Output > Deep > Path Override', True)
    seen = set()
    def visit(tree):
        if not tree or tree.as_pointer() in seen: return
        seen.add(tree.as_pointer())
        for node in tree.nodes:
            if node.type == 'OUTPUT_FILE':
                subject = 'File Output node "'+node.name+'"'
                check(node.directory, subject)
                check(node.file_name, subject)
                # In this pinned version per-item names become path components.
                if node.format.file_format != 'OPEN_EXR_MULTILAYER':
                    for item in node.file_output_items:
                        check(item.name, subject+' item "'+item.name+'"')
            if node.type == 'GROUP': visit(node.node_tree)
    visit(scene.compositing_node_group)
    return findings


def resolved_path(scene, layer, frame):
    # BKE_image_path_from_imformat/BLI_path_frame; no temporary RNA writes.
    text = scene.cycles.deep_output_path or scene.render.filepath
    source = Path(bpy.data.filepath)
    library = Path(scene.library.filepath) if scene.library else source
    variables = dict(view_layer=layer.name, blend_name=source.stem if bpy.data.filepath else 'Unsaved',
        blend_dir=str(source.parent)+os.sep if bpy.data.filepath else './',
        blend_name_lib=library.stem if str(library) != '.' else 'Unsaved',
        blend_dir_lib=str(library.parent)+os.sep,scene_name=scene.name,
        camera_name=scene.camera.name if scene.camera else '',
        resolution_x=scene.render.resolution_x*scene.render.resolution_percentage//100,
        resolution_y=scene.render.resolution_y*scene.render.resolution_percentage//100,
        fps=scene.render.fps/scene.render.fps_base)
    def replace(token):
        if isinstance(token, str): return token
        key,spec = token
        if key not in variables: raise ValueError('unresolved path variable '+key)
        value = variables[key]
        if not spec: return str(value)
        if '.' in spec:
            left, right = spec.split('.')
            return format(value, f'0{len(left)+1+len(right) if left else 0}.{len(right)}f')
        rounded = math.floor(value+.5) if value >= 0 else math.ceil(value-.5)
        return format(rounded, f'0{len(spec)}d')
    text = ''.join(replace(token) for token in template_tokens(text, bool(scene.cycles.deep_output_path)))
    text = bpy.path.abspath(text)
    matches = list(re.finditer(r'#+',text))
    if matches:
        match=matches[-1]
        text=text[:match.start()]+f'{frame:0{len(match.group())}d}'+text[match.end():]
    else: text += f'{frame:04d}'
    if scene.render.use_file_extension:
        ext=scene.render.file_extension
        if not text.lower().endswith(ext.lower()): text+=ext
    if not text.endswith('.deep.exr'): text=os.path.splitext(text)[0]+'.deep.exr'
    return os.path.normpath(text)


def scan(scene, layer_name=None, frame=None, device=None):
    original_frame, subframe = scene.frame_current,scene.frame_subframe
    window = bpy.context.window
    original_layer = window.view_layer if window else None
    report = dict(header='Deep scene scanner prediction; render-time validation is the authority.',
                  source=bpy.data.filepath,frame=original_frame if frame is None else frame,device_override=device,layers=[],undecidable=UNDECIDABLE)
    try:
        if frame is not None and frame != original_frame: scene.frame_set(frame)
        if layer_name and layer_name not in scene.view_layers: raise ValueError('Unknown layer '+layer_name)
        report['path_findings'] = inspect_paths(scene)
        destinations={}
        for layer in scene.view_layers:
            entry=dict(layer=layer.name,status='checked' if layer.cycles.use_pass_deep and (not layer_name or layer.name==layer_name) else 'skipped',findings=[])
            report['layers'].append(entry)
            def add(level,rule,reason,**fields):
                row=dict(level=level,rule=rule,reason=reason,**fields)
                if row not in entry['findings']: entry['findings'].append(row)
            if layer.cycles.use_pass_deep:
                try:
                    entry['deep_path']=resolved_path(scene,layer,scene.frame_current)
                    destinations[layer.name]=entry['deep_path']
                except ValueError as error: add('BLOCK','path',str(error))
            if entry['status']=='skipped': continue
            for rule,predicate,reason in SETTING_RULES:
                if predicate(scene,layer): add('BLOCK',rule,reason)
            if scene.render.engine != 'CYCLES': add('BLOCK','engine','Cycles engine required')
            camera=scene.camera
            volume,tolerance=effective_deep_settings(scene,layer)
            shader_eval=volume and scene.cycles.use_deep_volume_shader_eval
            events=layer.cycles.deep_max_events if getattr(layer.cycles,'deep_max_events_mode','SCENE')=='CUSTOM' else scene.cycles.deep_max_events
            entry['effective_volume']=volume
            entry['effective_shader_eval']=shader_eval
            entry['effective_events']=events
            entry['effective_z_tolerance']=0 if scene.cycles.deep_error == 0 else tolerance
            if not camera: add('BLOCK','camera','No active camera')
            else:
                data=camera.data
                if data.type not in ({'PERSP'} if volume else {'PERSP','ORTHO'}): add('BLOCK','camera','requires mono perspective or surface orthographic camera')
                if data.clip_start < 0 or data.clip_end <= data.clip_start or not math.isfinite(data.clip_end): add('BLOCK','clip','invalid camera clipping')
                if volume and (scene.render.use_motion_blur or data.dof.use_dof or data.clip_start<=0): add('BLOCK','volume_camera','volumes require static pinhole camera and positive near clip')
                if camera.animation_data or data.animation_data: add('WARN','camera_motion','Animated camera needs shutter-time rigid-transform/FOV validation')
                if data.dof.use_dof and (data.dof.aperture_ratio<=0 or data.dof.aperture_fstop<=0): add('BLOCK','aperture','invalid aperture')
            if scene.render.use_motion_blur:
                if scene.render.motion_blur_shutter<=0: add('BLOCK','shutter','motion blur requires positive shutter duration')
                add('WARN','shutter_curve','Uniform shutter curve and all shutter-time rigid transforms require render-time confirmation')
            prefs=bpy.context.preferences.addons['cycles'].preferences
            backend=device or ('CPU' if scene.cycles.device=='CPU' else prefs.compute_device_type)
            if backend!='CPU':
                if not prefs.devices:
                    add('WARN','devices','undecidable in background: device enumeration is empty; preferences unchanged')
                else:
                    if backend not in {'CUDA','OPTIX'}: add('BLOCK','device','requires single CPU, CUDA or OptiX')
                    enabled=[d for d in prefs.devices if d.use and d.type in {backend,'CPU'}]
                    if len(enabled)>1: add('BLOCK','devices','more than one enabled Cycles device: '+', '.join(d.name for d in enabled))
                    if not enabled: add('WARN','devices','No enumerated device selected for '+backend+'; preferences unchanged')
            if backend=='CUDA' and scene.cycles.shading_system: add('BLOCK','osl_cuda','CUDA deep requires native SVM; GPU OSL requires OptiX')
            if scene.cycles.shading_system: add('WARN','osl','OSL optimized groups cannot be checked from RNA')
            if window: window.view_layer=layer
            depsgraph=bpy.context.evaluated_depsgraph_get() if window else layer.depsgraph
            depsgraph.update()
            entry['depsgraph_mode']=depsgraph.mode
            observed=set()
            has_grid=False
            has_eval=False
            for instance in depsgraph.object_instances:
                obj=instance.object
                original=obj.original
                parent=instance.parent if instance.is_instance else obj
                parent_original=parent.original
                observed.add(original.name)
                # validate_deep_scene's mode detection includes invisible synced geometry.
                has_grid |= obj.type=='VOLUME'
                for slot in obj.material_slots:
                    mat=layer.material_override or slot.material
                    output=mat.node_tree.get_output_node('CYCLES') if mat and mat.node_tree else None
                    if output and output.inputs['Volume'].is_linked:
                        has_eval |= volume and (shader_eval or scene.cycles.shading_system)
                if obj.type not in {'MESH','CURVE','CURVES','SURFACE','FONT','META','VOLUME','POINTCLOUD','LIGHT'}: continue
                holdout=parent_original.holdout_get(view_layer=layer) or parent.is_holdout
                indirect=not holdout and parent_original.indirect_only_get(view_layer=layer)
                visible=obj.visible_camera and parent.visible_camera and not indirect
                if obj.hide_render or parent.hide_render: visible=False
                if not visible:
                    add('OK-ignored','visibility','indirect-only here' if indirect else 'camera-invisible or hidden for render',object=original.name)
                    continue
                fields=dict(object=original.name,instance_parent=parent_original.name if instance.is_instance else None)
                if obj.type=='LIGHT': continue
                if obj.type in {'CURVES','POINTCLOUD'}: add('BLOCK','geometry','only static polygon meshes and background lighting are supported: '+obj.type,**fields)
                if any(ps.settings.type=='HAIR' for ps in obj.particle_systems): add('BLOCK','particle_hair','particle hair curve geometry is unsupported',**fields)
                if obj.modifiers and obj.modifiers[-1].type=='SUBSURF' and obj.modifiers[-1].show_render and obj.modifiers[-1].use_adaptive_subdivision: add('BLOCK','subdivision','subdivision is unsupported',**fields)
                if obj.is_shadow_catcher or parent.is_shadow_catcher or getattr(obj.cycles,'is_caustics_caster',False) or getattr(obj.cycles,'is_caustics_receiver',False): add('BLOCK','object_flags','shadow catcher and caustics are unsupported',**fields)
                if scene.render.use_motion_blur and getattr(obj.cycles,'use_motion_blur',True) and getattr(obj.cycles,'use_deform_motion',True):
                    if getattr(obj.data,'shape_keys',None) or any(m.type in {'ARMATURE','LATTICE','MESH_DEFORM','SURFACE_DEFORM','CAST','SIMPLE_DEFORM','WAVE','DISPLACE','CLOTH','SOFT_BODY'} and m.show_render for m in obj.modifiers): add('BLOCK','deform_motion','deforming motion is unsupported (conservative before Cycles synchronization)',**fields)
                if volume:
                    determinant=instance.matrix_world.to_3x3().determinant()
                    if not math.isfinite(determinant) or determinant==0: add('BLOCK','transform','deep requires finite nonsingular transforms',**fields)
                if obj.type=='VOLUME':
                    has_grid=True
                    add('WARN','volume','Native grid must satisfy SUPPORT.md: '+SUPPORT_VOLUME,**fields)
                    if not volume: add('BLOCK','volume_disabled','native volume requires deep volume visibility',**fields)
                materials=[slot.material for slot in obj.material_slots if slot.material]
                if layer.material_override: materials=[layer.material_override]
                for material in materials:
                    output=material.node_tree.get_output_node('CYCLES') if material.node_tree else None
                    if output and output.inputs['Volume'].is_linked:
                        has_eval |= volume and (shader_eval or scene.cycles.shading_system)
                        if not volume: add('BLOCK','volume_disabled','volume shaders are unsupported without volume visibility',material=material.name,**fields)
                        elif (shader_eval or scene.cycles.shading_system) and obj.type!='VOLUME' and scene.cycles.deep_volume_step<=0: add('BLOCK','volume_step','shader-evaluated volume without a grid requires volume step',material=material.name,**fields)
                    inspect_material(material,lambda level,rule,reason,**kw:add(level,rule,reason,**fields,**kw))
            for obj in scene.objects:
                if obj.name not in observed and obj.type in {'MESH','CURVES','VOLUME','POINTCLOUD'}: add('OK-ignored','excluded','excluded, hidden, or not present in this layer depsgraph',object=obj.name)
            if scene.world and scene.world.node_tree:
                world_out=next((n for n in scene.world.node_tree.nodes if n.type=='OUTPUT_WORLD' and n.is_active_output),None)
                if world_out:
                    if world_out.inputs['Volume'].is_linked: add('BLOCK','world_volume','volume and displacement shaders are unsupported in the world')
                    if any(n.type!='BACKGROUND' for n,o,c in upstream(world_out.inputs['Surface'])): add('BLOCK','world_surface','deep world requires a background surface closure')
            max_events=8192 if volume and (has_grid or has_eval) else 64
            if not 1<=events<=max_events: add('BLOCK','events',f'invalid deep traversal limit (maximum {max_events}); use View Layer > Deep > Events Per Sample: Custom')
            if volume and has_eval and scene.cycles.deep_error<=2e-6: add('BLOCK','volume_error','shader-evaluated volumes require numeric error greater than 2e-6')
        for entry in report['layers']:
            path=destinations.get(entry['layer'])
            if not path: continue
            aliases=[name for name,p in destinations.items() if os.path.normcase(os.path.normpath(p))==os.path.normcase(os.path.normpath(path))]
            if len(aliases)>1:
                entry['findings'].append(dict(level='BLOCK',rule='collision',reason=f'Deep layers {", ".join(aliases)} would all write {path}; add {{view_layer}} to Output > Deep > Path Override'))
        report['blocks']=len(report['path_findings'])+sum(f['level']=='BLOCK' for l in report['layers'] for f in l['findings'])
        return report
    finally:
        if scene.frame_current!=original_frame or scene.frame_subframe!=subframe: scene.frame_set(original_frame,subframe=subframe)
        if window and original_layer: window.view_layer=original_layer


def format_report(report):
    lines=[report['header'],f"Source: {report['source']} | frame {report['frame']}"]
    lines.extend(f"  {item['level']}: {item['reason']}" for item in report.get('path_findings', []))
    for layer in report['layers']:
        lines.append(f"Layer {layer['layer']}: {layer['status']} | deep path: {layer.get('deep_path','-')}")
        for item in layer['findings']:
            subject=' / '.join(str(item[k]) for k in ('object','material','node','node_type') if item.get(k))
            lines.append(f"  {item['level']}: {subject + ': ' if subject else ''}{item['reason']}")
    lines.append('Not decidable from RNA:')
    lines.extend('  WARN: '+text for text in report['undecidable'])
    lines.append(f"BLOCK findings: {report['blocks']}")
    return '\n'.join(lines)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path)
    parser.add_argument('--layer')
    parser.add_argument('--frame',type=int)
    parser.add_argument('--device',choices=('CPU','CUDA','OPTIX'),help='Predict against the specified execution backend without changing preferences')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    report=scan(bpy.context.scene,args.layer,args.frame,args.device)
    print(format_report(report),flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2),encoding='utf8')
    return 2 if report['blocks'] else 0


if __name__=='__main__':
    try: code=main()
    except Exception:
        traceback.print_exc(); code=1
    # Blender otherwise swallows SystemExit; preserve the documented process code.
    sys.stdout.flush(); sys.stderr.flush()
    os._exit(code)
