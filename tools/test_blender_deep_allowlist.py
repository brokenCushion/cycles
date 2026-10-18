"""Qualify new deep surface nodes. Blender --python SCRIPT -- ROOT [CPU|OPTIX|CUDA].
Validate saved EXRs with ordinary Python: SCRIPT ROOT --validate.
"""
import csv
from collections import defaultdict
import math
import json
from pathlib import Path
import sys

CLOSURES = {
    'glossy_bsdf':'BsdfAnisotropic','metallic_bsdf':'BsdfMetallic',
    'refraction_bsdf':'BsdfRefraction','sheen_bsdf':'BsdfSheen','toon_bsdf':'BsdfToon',
    'subsurface_scattering':'SubsurfaceScattering','hair_bsdf':'BsdfHair',
    'principled_hair_bsdf':'BsdfHairPrincipled',
}
UTILITIES = {
    'vector_math':'VectorMath','map_range':'MapRange','separate_xyz':'SeparateXYZ',
    'combine_xyz':'CombineXYZ','mix_float':'Mix','mix_vector':'Mix',
    'mix_vector_non_uniform':'Mix','vector_rotate':'VectorRotate','float_curve':'FloatCurve',
    'vector_curves':'VectorCurve','normal':'Normal','blackbody':'Blackbody',
    'wavelength':'Wavelength','vector_transform':'VectorTransform',
}
LIGHT = ['Is Diffuse Ray','Is Glossy Ray','Is Singular Ray','Is Reflection Ray',
         'Is Transmission Ray','Is Volume Scatter Ray','Diffuse Depth','Glossy Depth',
         'Transmission Depth','Portal Depth']


def validate_hair(root):
    from test_blender_deep_hair import flat_identity
    report = json.loads((root / 'results.json').read_text())
    checks = {}
    for case in report.get('positive_hair', []):
        checks[case] = flat_identity(root / case / 'beauty_####.exr',
                                     root / case / 'off_####.exr', ['Visible'])
    (root / 'hair-flat-identity.json').write_text(json.dumps(checks, indent=2))
    print('ALLOWLIST_HAIR_FLAT_PASS', len(checks), flush=True)


def validate(root):
    import numpy as np
    import OpenEXR
    from deep_exr import read,check_deep
    report=json.loads((root/'results.json').read_text())
    checks={}
    for case in report['opacity']:
        deep=read(root/case/'node_0001.deep.exr')
        checked=check_deep(deep)
        values=deep.channels()['A'].pixels
        flat=np.array([[1-np.prod(1-np.asarray(a,dtype=float)) if a is not None else 0 for a in row] for row in values])
        beauty=OpenEXR.File(str(root/case/'beauty_####.exr'),separate_channels=True).channels()['A'].pixels
        beauty_error=float(np.max(np.abs(flat-beauty)))
        # Match validate_transparency_gaffer: mix raw camera chains, not noisy beauty alpha.
        ledger=defaultdict(list)
        with (root/case/'node_0001.deep.exr.samples.csv').open() as stream:
            for row in csv.DictReader(stream):
                key=tuple(int(row[k]) for k in ('file_x','file_y','sample'))
                event=int(row['event'])
                if event < 0:
                    assert not ledger[key] and float(row['alpha']) == 0
                else:
                    assert event == len(ledger[key]), (case,key,event)
                    ledger[key].append(float(np.float32(row['alpha'])))
        pixels=defaultdict(dict)
        for (x,y,sample),events in ledger.items():
            pixels[x,y][sample]=math.prod(1-a for a in events)
        height,width=flat.shape
        expected_grid={(x,y) for y in range(height) for x in range(width)
                       if (y % max(1,height//8)==0 or y==height-1)
                       and (x % max(1,width//8)==0 or x==width-1)}
        assert set(pixels)==expected_grid,case
        errors=[]
        oracle_beauty=[]
        for (x,y),samples in pixels.items():
            assert set(samples)==set(range(16)),(case,x,y)
            oracle=1-sum(samples.values())/16
            errors.append(abs(flat[y,x]-oracle))
            oracle_beauty.append(abs(float(beauty[y,x])-oracle))
        error=float(max(errors))
        assert error<=checked['deep_error'],(case,error,checked['deep_error'])
        if report['device']=='CPU':
            before=OpenEXR.File(str(root/case/'beauty_####.exr'),separate_channels=True).channels()
            after=OpenEXR.File(str(root/case/'off_####.exr'),separate_channels=True).channels()
            assert all(np.array_equal(before[k].pixels,after[k].pixels) for k in before),case
        assert all(sum(a is not None and len(a)>=2 for a in row)==len(row) for row in values),case
        checks[case]=dict(maximum_oracle_error=error,deep_error=checked['deep_error'],grid_pixels=len(pixels),native_beauty_alpha_difference=beauty_error,oracle_beauty_difference=max(oracle_beauty),cpu_beauty_exact=report['device']=='CPU')
    validate_hair(root)
    report['validation']=checks
    (root/'validation.json').write_text(json.dumps(report,indent=2))
    print('DEEP_ALLOWLIST_VALIDATION_PASS',report['device'],len(checks),'opacity cases; max error',max(v['maximum_oracle_error'] for v in checks.values()),flush=True)


def utility(kind,mat):
    nodes=mat.node_tree.nodes;link=mat.node_tree.links.new
    if kind.startswith('lp_'):
        node=nodes.new('ShaderNodeLightPath');return node.outputs[kind[3:]]
    node=nodes.new('ShaderNode'+UTILITIES[kind])
    noise=nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=.4
    geometry=nodes.new('ShaderNodeNewGeometry');link(geometry.outputs['Position'],noise.inputs['Vector'])
    fac,vector=noise.outputs['Fac'],noise.outputs['Color']
    if kind.startswith('mix_'):
        node.data_type='FLOAT' if kind=='mix_float' else 'VECTOR'
        if kind=='mix_vector_non_uniform':node.factor_mode='NON_UNIFORM'
        enabled=[s for s in node.inputs if s.enabled]
        factor=next(s for s in enabled if s.name=='Factor');link(vector if factor.type=='VECTOR' else fac,factor)
        for inp in enabled:
            if inp.name in {'A','B'}:inp.default_value=(.1,.2,.3) if inp.type=='VECTOR' and inp.name=='A' else ((.7,.8,.9) if inp.type=='VECTOR' else (.2 if inp.name=='A' else .8))
    elif kind in {'blackbody','wavelength'}:
        scale=nodes.new('ShaderNodeMath');scale.operation='MULTIPLY_ADD'
        link(fac,scale.inputs[0]);scale.inputs[1].default_value=2500 if kind=='blackbody' else 300
        scale.inputs[2].default_value=3500 if kind=='blackbody' else 380
        link(scale.outputs[0],node.inputs[0])
    elif kind=='combine_xyz':
        for inp in node.inputs:link(fac,inp)
    elif kind=='normal':link(vector,node.inputs[0])
    elif kind=='float_curve':
        node.mapping.initialize();node.mapping.curves[0].points.new(.5,.35);node.mapping.update()
        link(fac,node.inputs['Value'])
    elif kind=='vector_curves':
        node.mapping.initialize();node.mapping.curves[0].points.new(.5,.35);node.mapping.update()
        link(vector,node.inputs['Vector'])
    elif kind=='vector_rotate':
        link(vector,node.inputs['Vector']);node.inputs['Angle'].default_value=.4
    elif kind=='vector_transform':
        node.convert_from='WORLD';node.convert_to='CAMERA';node.vector_type='VECTOR';link(vector,node.inputs['Vector'])
    elif kind=='map_range':
        link(fac,node.inputs['Value']);node.inputs['To Min'].default_value=.1;node.inputs['To Max'].default_value=.9
    elif kind=='vector_math':
        node.operation='ADD';link(vector,node.inputs[0]);node.inputs[1].default_value=(.1,.2,.3)
    else:link(vector,node.inputs[0])
    return node.outputs['Dot'] if kind=='normal' else next(o for o in node.outputs if o.enabled)


def render_suite(root,device):
    import bpy
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    from test_scan_blender_deep_scene import fixture,apply_case
    from scan_blender_deep_scene import scan
    root.mkdir(parents=True,exist_ok=False)
    report=dict(candidate=True,device=device,opaque=[],opacity=[],positive_hair=[],rejections={})

    def setup():
        s,obj,mat,bsdf,out=fixture()
        s.cycles.samples=16;s.cycles.seed=123
        s.cycles.pixel_filter_type='BOX';s.cycles.filter_width=1
        s.cycles.deep_z_tolerance=0
        s.render.image_settings.file_format='OPEN_EXR'
        s.render.image_settings.color_mode='RGBA';s.render.image_settings.color_depth='32'
        if device!='CPU':
            prefs=bpy.context.preferences.addons['cycles'].preferences
            prefs.compute_device_type=device;prefs.refresh_devices()
            selected=next(d.id for d in prefs.devices if d.type==device)
            for d in prefs.devices:d.use=d.id==selected
            s.cycles.device='GPU'
        return s,obj,mat,bsdf,out


    def draw(s,case,tag='node',write=False):
        s.cycles.deep_output_path=str(root/case/(tag+'_####'))
        s.render.filepath=str(root/case/('beauty_####' if tag=='node' else tag+'_####'))
        prediction=scan(s)
        assert not prediction['blocks'],(case,prediction)
        bpy.ops.render.render(write_still=write)
        path=root/case/(tag+'_0001.deep.exr');assert path.is_file(),path
        return path

    for kind in [*CLOSURES,*UTILITIES,*('lp_'+x for x in LIGHT)]:
        s,obj,mat,bsdf,out=setup();nodes=mat.node_tree.nodes;link=mat.node_tree.links.new
        t=nodes.new('ShaderNodeBsdfTransparent');mix=nodes.new('ShaderNodeMixShader');mix.inputs[0].default_value=.37
        link(t.outputs[0],mix.inputs[1]);link(bsdf.outputs[0],mix.inputs[2]);link(mix.outputs[0],out.inputs['Surface'])
        if kind in CLOSURES:
            closure=nodes.new('ShaderNode'+CLOSURES[kind]);link(closure.outputs[0],mix.inputs[2])
            noise=nodes.new('ShaderNodeTexNoise')
            color=closure.inputs.get('Color')
            if color:link(noise.outputs['Color'],color)
        else:link(utility(kind,mat),bsdf.inputs['Color'])
        case='opaque_'+kind.replace(' ','_')
        actual=draw(s,case)
        link(bsdf.outputs[0],mix.inputs[2])
        for l in list(bsdf.inputs['Color'].links):mat.node_tree.links.remove(l)
        reference=draw(s,case,'constant')
        if device=='CPU':assert actual.read_bytes()==reference.read_bytes(),kind
        report['opaque'].append(case)
        print('ALLOWLIST_OPAQUE_PASS',device,kind,flush=True)
    for kind in [*UTILITIES,*('lp_'+x for x in LIGHT)]:
        s,obj,mat,bsdf,out=setup();nodes=mat.node_tree.nodes;link=mat.node_tree.links.new
        t=nodes.new('ShaderNodeBsdfTransparent');mix=nodes.new('ShaderNodeMixShader')
        link(t.outputs[0],mix.inputs[1]);link(bsdf.outputs[0],mix.inputs[2]);link(mix.outputs[0],out.inputs['Surface'])
        bounded=nodes.new('ShaderNodeMath');bounded.operation='MULTIPLY_ADD';bounded.use_clamp=True
        link(utility(kind,mat),bounded.inputs[0]);bounded.inputs[1].default_value=.2;bounded.inputs[2].default_value=.3
        link(bounded.outputs[0],mix.inputs[0])
        front=bpy.data.objects.new('Front',obj.data);s.collection.objects.link(front);front.location.z=2
        case='opacity_'+kind.replace(' ','_')
        s.cycles.deep_write_diagnostics=True
        draw(s,case,write=True)
        if device=='CPU':
            s.view_layers[0].cycles.use_pass_deep=False
            s.render.filepath=str(root/case/'off_####');bpy.ops.render.render(write_still=True)
        report['opacity'].append(case)
        print('ALLOWLIST_OPACITY_RENDER_PASS',device,kind,flush=True)
    if device == 'CPU':
        s,obj,mat,bsdf,out=setup();apply_case('particle_hair',s,obj,mat,bsdf,out)
        draw(s,'particle_hair',write=True)
        s.view_layers[0].cycles.use_pass_deep=False
        s.render.filepath=str(root/'particle_hair'/'off_####')
        bpy.ops.render.render(write_still=True)
        report['positive_hair'].append('particle_hair')
    for case in ('ray_portal','ao_opacity','bevel_opacity','light_transparent_depth','particle_hair_tolerance'):
        s,obj,mat,bsdf,out=setup()
        apply_case('particle_hair' if case=='particle_hair_tolerance' else case,s,obj,mat,bsdf,out)
        if case=='particle_hair_tolerance': s.cycles.deep_z_tolerance=1e-4
        s.cycles.deep_output_path=str(root/case/'node_####')
        prediction=scan(s);assert prediction['blocks'],case
        if case=='particle_hair_tolerance':
            from test_blender_deep_hair import rejection_isolation
            report['hair_rejection_isolation']=rejection_isolation(prediction,'tolerance',obj.name)
        try:bpy.ops.render.render()
        except RuntimeError as exc:
            expected={'ray_portal':'ray_portal_bsdf','ao_opacity':'ambient occlusion','bevel_opacity':'bevel','light_transparent_depth':'Transparent Depth','particle_hair_tolerance':'hair requires Surface Depth Tolerance 0 for this layer'}[case]
            assert expected in str(exc),(case,str(exc))
            report['rejections'][case]=str(exc)
        else:raise AssertionError('Rejection did not happen: '+case)
        assert not list((root/case).glob('*.deep.exr')),case
        print('ALLOWLIST_REJECTION_PASS',device,case,flush=True)
    (root/'results.json').write_text(json.dumps(report,indent=2))
    print('DEEP_ALLOWLIST_RENDER_PASS',device,len(report['opaque']),'opaque',len(report['opacity']),'opacity',len(report['rejections']),'rejections',flush=True)


if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else sys.argv[1:]
    root=Path(args[0])
    if '--validate-hair' in args:validate_hair(root)
    elif '--validate' in args:validate(root)
    else:render_suite(root,args[1] if len(args)>1 else 'CPU')
