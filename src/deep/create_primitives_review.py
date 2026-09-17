# SPDX-License-Identifier: Apache-2.0
"""Render three polygon primitives separately and together; assemble a Gaffer review."""
import json
import math
from pathlib import Path
import subprocess
import sys
import Gaffer
import GafferImage
import imath

ROOT = Path.cwd()
OUT = ROOT / 'build-m3' / 'primitives-review'
OUT.mkdir(parents=True, exist_ok=True)
W, H, S = 640, 480, 16
objects = [
    ('01_near_sphere', (-.75, 0, 4), (.9, .9, .9), .55, (1,.22,.08)),
    ('02_middle_cube', (0, 0, 7.1), (1.1,1.1,1.1), .7, (.08,.65,1)),
    ('03_far_cylinder', (1.8,0,11), (1.8,2,1.8), 1, (.3,1,.15)),
]


def geometry(kind, center, scale):
    points, faces = [], []
    if 'sphere' in kind:
        n,m=48,24
        points=[(0,-1,0)]
        for j in range(1,m):
            v=-math.pi/2+math.pi*j/m
            points += [(math.cos(v)*math.cos(2*math.pi*i/n),math.sin(v),math.cos(v)*math.sin(2*math.pi*i/n)) for i in range(n)]
        points.append((0,1,0)); top=len(points)-1
        for i in range(n):
            k=(i+1)%n
            faces.append((0,1+k,1+i))
            for j in range(m-2):
                a=1+j*n; b=a+n
                faces.append((a+i,a+k,b+k,b+i))
            faces.append((top,1+(m-2)*n+i,1+(m-2)*n+k))
    elif 'cube' in kind:
        points=[(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]
        faces=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(3,7,6,2),(0,4,7,3),(1,2,6,5)]
        a=math.radians(25)
        points=[(x*math.cos(a)+z*math.sin(a), y, z*math.cos(a)-x*math.sin(a)) for x,y,z in points]
    else:
        n=64
        points=[(math.cos(2*math.pi*i/n),y,math.sin(2*math.pi*i/n)) for y in (-1,1) for i in range(n)]
        faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
        faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    # Explicit triangles, including caps, make the source mesh inspectable.
    triangles=[(f[0],f[i],f[i+1]) for f in faces for i in range(1,len(f)-1)]
    points=[tuple(c+s*v for c,s,v in zip(center,scale,p)) for p in points]
    return '<state shader="'+kind+'"><mesh P="'+' '.join(f'{v:.9g}' for p in points for v in p)+'" nverts="'+' '.join('3' for f in triangles)+'" verts="'+' '.join(str(i) for f in triangles for i in f)+'"/></state>'


def material(name,alpha,color):
    c=' '.join(str(v) for v in color); dark=' '.join(str(v*.3) for v in color)
    return f'''<shader name="{name}"><diffuse_bsdf name="d"/>
<checker_texture name="check" color1="{c}" color2="{dark}" scale="5"/>
<connect from="check color" to="d color"/>
<transparent_bsdf name="t" color="1 1 1"/><mix_closure name="m" fac="{1-alpha}"/>
<connect from="d bsdf" to="m closure1"/><connect from="t bsdf" to="m closure2"/>
<connect from="m closure" to="output surface"/></shader>'''


def render(name, selected):
    xml=f'''<cycles><camera width="{W}" height="{H}" camera_type="perspective" fov="0.9"/>
<film filter_type="box" filter_width="1"/><integrator use_adaptive_sampling="false" seed="123" transparent_max_bounce="16"/>
<background transparent="true"><background name="bg" color="0.8 0.8 0.8" strength="1"/>
<connect from="bg background" to="output surface"/></background>'''
    for obj in selected:
        key,center,scale,alpha,color=obj
        xml+=material(key,alpha,color)+geometry(key,center,scale)
    source=OUT/(name+'.xml'); source.write_text(xml+'</cycles>')
    cmd=[str(ROOT/'install/cycles-m4.exe'),'--background','--quiet','--device','CPU',
         '--width',str(W),'--height',str(H),'--samples',str(S),'--threads','8',
         '--output',str(OUT/(name+'.beauty.exr')),'--deep-output',str(OUT/(name+'.deep.exr')),
         '--deep-transparent','--deep-max-events','8','--deep-memory-mb','512',str(source)]
    result=subprocess.run(cmd,capture_output=True,text=True,timeout=600)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode or 'ERROR:' in result.stderr:
        raise RuntimeError(result.stdout+result.stderr)
    print('Rendered '+name,flush=True)


if '--assemble-only' not in sys.argv:
    for obj in objects: render(obj[0],[obj])
    render('04_all_objects_reference',objects)

script=Gaffer.ScriptNode()


def add(name,node,x,y,description=''):
    script[name]=node
    node.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(x,y),flags=Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic))
    if description: Gaffer.Metadata.registerValue(node,'description',description)
    return node


def read(name,file,x,y):
    n=add(name,GafferImage.ImageReader(),x,y)
    n['fileName'].setValue((OUT/file).as_posix())
    return n


colored=[]; readers=[]
for i,(name,center,scale,alpha,color) in enumerate(objects):
    x=(i-1)*30
    r=read(name+'_DEPTH_OPACITY',name+'.deep.exr',x,35); readers.append(r)
    Gaffer.Metadata.registerValue(r,'description',f'Rendered polygon mesh. Center {center}. Local surface opacity {alpha}. Channels A, Z, ZBack; no renderer deep RGB.')
    shuffle=add(name+'_AlphaToRGB',GafferImage.Shuffle(),x,22)
    shuffle['in'].setInput(r['out'])
    for c in ('R','G','B'): shuffle['shuffles'].addChild(Gaffer.ShufflePlug('A',c))
    tint=add(name+'_REVIEW_COLOR',GafferImage.Grade(),x,10,'Constant identification color multiplied by each deep sample alpha; visualization only, not rendered deep radiance.')
    tint['in'].setInput(shuffle['out']); tint['multiply'].setValue(imath.Color4f(*color,1))
    colored.append(tint)
    beauty=read(name+'_BEAUTY',name+'.beauty.exr',x+100,35)

merge=add('MERGE_separate_deep_objects',GafferImage.DeepMerge(),0,-8)
merge['in'].resize(3)
for i,n in enumerate(colored): merge['in'][i].setInput(n['out'])
flat=add('VIEW_colored_deep_composite',GafferImage.DeepToFlat(),0,-24)
flat['in'].setInput(merge['out'])
slice=add('EDIT_depth_cut',GafferImage.DeepSlice(),-35,-24,'Change farClip value: 5.2 keeps the sphere; 8.8 adds the cube; 14 includes all three. Disable farClip for the complete scene.')
slice['in'].setInput(merge['out']); slice['nearClip']['enabled'].setValue(False)
slice['farClip']['enabled'].setValue(True); slice['farClip']['value'].setValue(5.2)
slice['flatten'].setValue(True)
reference=read('ALL_TOGETHER_rendered_deep_reference','04_all_objects_reference.deep.exr',65,0)
referenceFlat=add('VIEW_reference_alpha',GafferImage.DeepToFlat(),65,-24)
referenceFlat['in'].setInput(reference['out'])
beautyReference=read('ALL_TOGETHER_rendered_BEAUTY','04_all_objects_reference.beauty.exr',115,0)

def write_image(plug,name,channels):
    writer=GafferImage.ImageWriter(); writer['in'].setInput(plug)
    writer['fileName'].setValue((OUT/name).as_posix()); writer['channels'].setValue(channels)
    writer['task'].execute()

write_image(flat['out'],'review_composite.png','R G B A')
write_image(slice['out'],'review_near_only.png','R G B A')
write_image(beautyReference['out'],'rendered_beauty.png','R G B A')
write_image(merge['out'],'review_colored_merged.deep.exr','R G B A Z ZBack')

# Measure the expected scalar-coverage limitation rather than hiding it.
maximum=0.; changed=0; total=0
tile=GafferImage.ImagePlug.tileSize()
for y in range(0,H,tile):
    for x in range(0,W,tile):
        p=imath.V2i(x,y)
        a=flat['out'].channelData('A',p); b=referenceFlat['out'].channelData('A',p)
        for j in range(min(tile,H-y)):
            for i in range(min(tile,W-x)):
                d=abs(float(a[j*tile+i])-float(b[j*tile+i]))
                maximum=max(maximum,d); changed+=d>1e-5; total+=1
report={'resolution':[W,H],'samples':S,'objects':[{'name':n,'center':c,'scale':s,'surface_alpha':a} for n,c,s,a,col in objects],
        'max_merged_vs_joint_alpha_error':maximum,'pixels_with_alpha_difference_over_1e-5':changed,'pixels':total,
        'note':'Independent scalar deep passes lose subpixel coverage correlation. Edge differences from the joint render are expected; RGB is an identification tint added in Gaffer.'}
(OUT/'report.json').write_text(json.dumps(report,indent=2))
(OUT/'README.md').write_text(f'''# Three-primitives deep review

Open `three_primitives_review.gfr` in Gaffer.

1. Select `VIEW_colored_deep_composite` and press V: the three separately rendered deep files are merged by depth.
2. Select `EDIT_depth_cut` and press V. Change **farClip / value**: 5.2 shows the near sphere; 8.8 adds the middle cube; 14 includes the far cylinder.
3. Select an individual `DEPTH_OPACITY` reader to inspect its original A, Z and ZBack channels.
4. `ALL_TOGETHER_rendered_BEAUTY` is the actual checker-material render of all three meshes together. Each object also has its own BEAUTY reader.
5. Compare alpha on `VIEW_colored_deep_composite` and `VIEW_reference_alpha` (a joint render).

Camera: 640 x 480, 16 samples, static perspective looking along +Z.
Sphere: center depth 4, orange, local surface opacity .55.
Cube: center depth 7.1, blue, local surface opacity .70, rotated 25 degrees.
Cylinder: center depth 11, green, opaque.
These are closed polygon meshes: transparent objects have front and back surface events. Their overall opacity therefore exceeds the local surface opacity.

M4 renders deep opacity and depth only. AlphaToRGB and REVIEW_COLOR nodes add constant identification colors in Gaffer. These are not deep lighting/radiance passes.

Separate scalar deep passes cannot retain correlated subpixel coverage. In this test, {changed} of {total} pixels differ in alpha from the joint render by more than 1e-5, with a maximum difference of {maximum:.6f}. This is an existing edge-coverage limitation, not an exact reconstruction claim.

All original per-object beauty/deep EXRs, the joint reference, XML geometry sources, and a machine-readable report are in this folder. `review_colored_merged.deep.exr` is the Gaffer-generated tinted deep composite.
''')
script['fileName'].setValue((OUT/'three_primitives_review.gfr').as_posix()); script.save()
check=Gaffer.ScriptNode(); check['fileName'].setValue(script['fileName'].getValue()); check.load()
assert check['VIEW_colored_deep_composite']['out'].channelData('A',imath.V2i(0,0))==flat['out'].channelData('A',imath.V2i(0,0))
print(json.dumps(report,indent=2),flush=True)
