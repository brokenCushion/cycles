"""Scanner/render drift checks. Blender -b --disable-autoexec --python SCRIPT -- FRESH_ROOT."""
import json
from pathlib import Path
import sys
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from scan_blender_deep_scene import scan, NODE_RULES, LIGHT_PATH_OUTPUTS


def fixture():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s=bpy.context.scene
    s.render.engine='CYCLES'
    s.render.resolution_x=s.render.resolution_y=32
    s.render.resolution_percentage=100
    s.render.film_transparent=True
    s.render.use_compositing=s.render.use_sequencer=False
    s.render.threads_mode='FIXED'; s.render.threads=4
    s.cycles.samples=1
    s.cycles.use_adaptive_sampling=s.cycles.use_denoising=False
    s.cycles.deep_error=.001; s.cycles.deep_max_events=64
    s.world=bpy.data.worlds.new('World'); s.world.use_nodes=True
    bpy.ops.object.camera_add(location=(0,0,8))
    s.camera=bpy.context.object
    s.camera.data.type='ORTHO'; s.camera.data.ortho_scale=4
    collection=bpy.data.collections.new('Feature'); s.collection.children.link(collection)
    bpy.ops.mesh.primitive_plane_add(size=4)
    obj=bpy.context.object; obj.name='Feature'
    for owner in list(obj.users_collection):owner.objects.unlink(obj)
    collection.objects.link(obj)
    mat=bpy.data.materials.new('Feature');mat.use_nodes=True;obj.data.materials.append(mat)
    nodes=mat.node_tree.nodes;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial');bsdf=nodes.new('ShaderNodeBsdfDiffuse')
    mat.node_tree.links.new(bsdf.outputs[0],out.inputs['Surface'])
    layer=s.view_layers[0];layer.name='Visible';layer.cycles.use_pass_deep=True
    return s,obj,mat,bsdf,out


def fcurves(owner):
    action=owner.animation_data.action
    if len(getattr(action,'fcurves',[])):return list(action.fcurves)
    return [fc for layer in getattr(action,'layers',[]) for strip in layer.strips for bag in strip.channelbags for fc in bag.fcurves]


def apply_case(case,s,obj,mat,bsdf,out):
    n=mat.node_tree.nodes;link=mat.node_tree.links.new
    if case.startswith('candidate_'):
        from test_blender_deep_allowlist import CLOSURES,utility
        kind=case.removeprefix('candidate_')
        if kind in CLOSURES:
            node=n.new('ShaderNode'+CLOSURES[kind]);link(node.outputs[0],out.inputs['Surface'])
        else:
            value=utility(kind,mat)
            transparent=n.new('ShaderNodeBsdfTransparent');mix=n.new('ShaderNodeMixShader')
            link(transparent.outputs[0],mix.inputs[1]);link(bsdf.outputs[0],mix.inputs[2])
            link(value,mix.inputs[0]);link(mix.outputs[0],out.inputs['Surface'])
        return
    if case in {'hair_mesh','particle_hair','holdout_hair','shared_hair'}:
        node=n.new('ShaderNodeBsdfHair');link(node.outputs[0],out.inputs['Surface'])
        if case=='particle_hair':
            bpy.ops.object.particle_system_add();p=obj.particle_systems[-1].settings;p.type='HAIR';p.count=4
        if case=='holdout_hair':s.view_layers[0].layer_collection.children['Feature'].holdout=True
        if case=='shared_hair':
            obj.visible_camera=False
            shared=bpy.data.objects.new('Visible shared',obj.data);s.collection.objects.link(shared)
    elif case in {'light_reflection','light_transparent_depth','mix_float','mix_vector','ao_opacity','bevel_opacity','ao_color','bevel_color','ray_portal','displacement','muted','unreachable','group'}:
        if case.startswith('light_'):
            node=n.new('ShaderNodeLightPath');value=node.outputs['Is Reflection Ray' if case=='light_reflection' else 'Transparent Depth']
        elif case.startswith('mix_'):
            node=n.new('ShaderNodeMix');node.data_type='FLOAT' if case=='mix_float' else 'VECTOR'
            coord=n.new('ShaderNodeTexNoise');link(coord.outputs['Fac'],next(i for i in node.inputs if i.enabled and i.name=='Factor'))
            for inp in node.inputs:
                if inp.enabled and inp.name in {'A','B'}:
                    inp.default_value=(.2,.3,.4) if inp.type=='VECTOR' else (.2 if inp.name=='A' else .8)
            value=next(o for o in node.outputs if o.enabled)
        elif case.startswith('ao_') or case.startswith('bevel_'):
            node=n.new('ShaderNodeAmbientOcclusion' if case.startswith('ao') else 'ShaderNodeBevel');value=node.outputs[0]
        elif case=='ray_portal':
            node=n.new('ShaderNodeBsdfRayPortal');link(node.outputs[0],out.inputs['Surface']);return
        elif case=='displacement':
            node=n.new('ShaderNodeTexNoise');link(node.outputs['Fac'],out.inputs['Displacement']);return
        elif case in {'unreachable','muted'}:
            node=n.new('ShaderNodeVectorMath')
            if case=='muted':node.mute=True;link(node.outputs[0],bsdf.inputs['Color'])
            return
        else:
            tree=bpy.data.node_groups.new('Group','ShaderNodeTree')
            tree.interface.new_socket(name='Shader',in_out='OUTPUT',socket_type='NodeSocketShader')
            hair=tree.nodes.new('ShaderNodeBsdfHair');group_out=tree.nodes.new('NodeGroupOutput')
            tree.links.new(hair.outputs[0],group_out.inputs[0])
            node=n.new('ShaderNodeGroup');node.node_tree=tree;link(node.outputs[0],out.inputs['Surface']);return
        if case.endswith('_color'):link(value,bsdf.inputs['Color'])
        else:
            transparent=n.new('ShaderNodeBsdfTransparent');mix=n.new('ShaderNodeMixShader')
            link(transparent.outputs[0],mix.inputs[1]);link(bsdf.outputs[0],mix.inputs[2]);link(value,mix.inputs[0]);link(mix.outputs[0],out.inputs['Surface'])
    elif case=='adaptive':
        obj.modifiers.new('Adaptive','SUBSURF').use_adaptive_subdivision=True
    elif case in {'curves','pointcloud'}:
        data=bpy.data.hair_curves.new('Curves') if case=='curves' else bpy.data.pointclouds.new('Points')
        replacement=bpy.data.objects.new('Non-mesh',data)
        obj.users_collection[0].objects.link(replacement)
    elif case=='camera_off':
        node=n.new('ShaderNodeBsdfHair');link(node.outputs[0],out.inputs['Surface']);obj.visible_camera=False
    elif case=='instance':
        node=n.new('ShaderNodeBsdfHair');link(node.outputs[0],out.inputs['Surface'])
        collection=obj.users_collection[0]
        s.collection.children.unlink(collection)
        instance=bpy.data.objects.new('Instancer',None)
        wrapper=bpy.data.collections.new('Instance layer');s.collection.children.link(wrapper)
        wrapper.objects.link(instance)
        instance.instance_type='COLLECTION';instance.instance_collection=collection
    elif case=='shadow_catcher':obj.is_shadow_catcher=True
    elif case=='caustics':obj.cycles.is_caustics_caster=True
    elif case=='volume':
        node=n.new('ShaderNodeVolumeScatter');link(node.outputs[0],out.inputs['Volume'])
    elif case=='panoramic':s.camera.data.type='PANO'
    elif case=='border':s.render.use_border=True;s.render.border_max_x=.5
    elif case=='multiview':s.render.use_multiview=True
    elif case=='samples':s.cycles.samples=4097
    elif case=='time_limit':s.cycles.time_limit=.1
    elif case=='transparent_glass':s.cycles.film_transparent_glass=True
    elif case=='rolling_shutter':s.cycles.rolling_shutter_type='TOP'
    elif case=='guiding':s.cycles.use_guiding=True
    elif case=='strict_ids':s.cycles.deep_error=0;s.cycles.use_deep_ids=True
    elif case=='deform':
        s.render.use_motion_blur=True
        obj.modifiers.new('Deform','WAVE')
    elif case.startswith('camera_') and case!='camera_off':
        # Blender cameras are reflected in Cycles (Z flip); deep keeps each step's handedness.
        cam=s.camera
        def key(owner,path,values,interpolation=None):
            for frame,value in values:
                setattr(owner,path,value);owner.keyframe_insert(path,frame=frame)
            if interpolation:
                for fc in fcurves(owner):
                    for k in fc.keyframe_points:k.interpolation=interpolation
        s.render.use_motion_blur=case!='camera_fov_vector'
        if case in {'camera_motion','camera_motion_deform','camera_motion_reflected','camera_volume_motion'}:
            key(cam,'location',((0,(0,0,9)),(2,(.4,0,7))))
        if case=='camera_motion_deform':obj.modifiers.new('Deform','WAVE')
        if case=='camera_motion_reflected':cam.scale=(-1,1,1)
        if case=='camera_handedness_flip':
            key(cam,'scale',((1,(1,1,1)),(1.2,(-1,1,1))),'CONSTANT')
        if case=='camera_shear':
            parent=bpy.data.objects.new('Rig',None);s.collection.objects.link(parent)
            # Non-uniform parent scale over a rotated child shears the camera axes.
            cam.rotation_euler=(0,0,.7);cam.parent=parent
            key(parent,'scale',((0,(1,1,1)),(2,(3,1,1))))
        if case in {'camera_fov','camera_fov_vector'}:
            cam.data.type='PERSP'
            key(cam.data,'lens',((0,50),(2,80)))
            if case=='camera_fov_vector':s.view_layers[0].use_pass_vector=True
        if case=='camera_volume_motion':
            s.cycles.use_deep_volume=True
            node=n.new('ShaderNodeVolumeScatter');link(node.outputs[0],out.inputs['Volume'])
    elif case!='diffuse':raise ValueError(case)


def main():
    root=Path(sys.argv[sys.argv.index('--')+1]);root.mkdir(parents=True,exist_ok=False)
    cases=['diffuse','hair_mesh','particle_hair','holdout_hair','shared_hair','light_reflection','light_transparent_depth','mix_float','mix_vector','ao_opacity','bevel_opacity','ao_color','bevel_color','ray_portal','displacement','muted','unreachable','group','adaptive','shadow_catcher','caustics','volume','panoramic','border','multiview','samples','time_limit','transparent_glass','rolling_shutter','guiding','strict_ids','deform','camera_off','instance','camera_motion','camera_motion_deform','camera_motion_reflected','camera_handedness_flip','camera_shear','camera_fov','camera_fov_vector','camera_volume_motion']
    from test_blender_deep_allowlist import CLOSURES,UTILITIES,LIGHT
    cases += ['candidate_'+name for name in [*CLOSURES,*UTILITIES,*('lp_'+x for x in LIGHT)]]
    results=[]
    for case in cases:
        for indirect in (False,True):
            s,obj,mat,bsdf,out=fixture();apply_case(case,s,obj,mat,bsdf,out)
            layer=s.view_layers[0]
            layer.layer_collection.children['Instance layer' if case=='instance' else 'Feature'].indirect_only=indirect
            name=case+('-indirect' if indirect else '-visible')
            s.render.filepath=str(root/name/'beauty_####')
            s.cycles.deep_output_path=str(root/name/'{view_layer}_####')
            before=(s.frame_current,bpy.context.window.view_layer.name,s.render.filepath,len(bpy.data.objects),len(bpy.data.materials))
            report=scan(s)
            assert before==(s.frame_current,bpy.context.window.view_layer.name,s.render.filepath,len(bpy.data.objects),len(bpy.data.materials))
            blocked=bool(report['blocks']);error=None
            try:bpy.ops.render.render()
            except RuntimeError as exc:error=str(exc)
            passed=error is None
            if passed:assert list((root/name).glob('*.deep.exr'))
            mismatch=blocked==passed
            row=dict(case=name,scanner_block=blocked,render_pass=passed,error=error,mismatch=mismatch,findings=[f for l in report['layers'] for f in l['findings'] if f['level']=='BLOCK'])
            results.append(row)
            (root/'results.json').write_text(json.dumps(results,indent=2))
            print('SCANNER_DRIFT',name,'BLOCK' if blocked else 'OK','REJECT' if error else 'PASS','MISMATCH' if mismatch else 'MATCH',flush=True)
    false_negatives=[r for r in results if r['mismatch'] and not r['scanner_block']]
    false_positives=[r for r in results if r['mismatch'] and r['scanner_block']]
    print('SCANNER_DRIFT_SUMMARY',len(results),'cases',len(false_negatives),'false negatives',len(false_positives),'false positives',flush=True)
    assert not false_negatives,false_negatives
    # Camera motion: reflected Blender cameras render; scale/shear/handedness/FOV/volume cases reject.
    for r in results:
        base=r['case'].rsplit('-',1)[0]
        if base in {'camera_motion','camera_motion_deform','camera_motion_reflected'}:
            assert r['render_pass'] and not r['scanner_block'],r
        if base in {'camera_handedness_flip','camera_shear','camera_fov','camera_volume_motion'}:
            assert not r['render_pass'] and r['scanner_block'],r
    print('SCANNER_CAMERA_MOTION_PASS',flush=True)
    s,obj,mat,bsdf,out=fixture()
    s.cycles.device='GPU'
    s.cycles.deep_output_path=str(root/'device-check'/'{view_layer}_####')
    prefs=bpy.context.preferences.addons['cycles'].preferences
    before=[(d.id,d.use) for d in prefs.devices]
    prediction=scan(s,device='CPU')
    assert not prediction['blocks'],prediction
    if not prefs.devices:
        prediction=scan(s)
        assert not prediction['blocks'],prediction
        assert any(f['level']=='WARN' and 'undecidable in background' in f['reason'] for l in prediction['layers'] for f in l['findings'])
    assert before==[(d.id,d.use) for d in prefs.devices]
    print('SCANNER_DEVICE_OVERRIDE_READ_ONLY_PASS',flush=True)
    paths=[]
    for index,template in enumerate(('shot_####','shot','{view_layer}_####','{blend_name}_{view_layer}_####','shot_####.exr')):
        s,obj,mat,bsdf,out=fixture()
        s.render.image_settings.file_format='OPEN_EXR'
        s.cycles.deep_output_path=str(root/f'path-{index}'/template)
        s.frame_set(2)
        predicted=scan(s)['layers'][0]['deep_path']
        bpy.ops.render.render()
        assert Path(predicted).is_file(),predicted
        paths.append(predicted)
    (root/'paths.json').write_text(json.dumps(paths,indent=2))
    print('SCANNER_PATHS_PASS',len(paths),flush=True)
    # Only graph folding may justify a conservative false positive; keep every finding.
    assert all(all(f['rule'] in {'node','light_path','displacement'} for f in r['findings']) for r in false_positives),false_positives


if __name__=='__main__':main()
