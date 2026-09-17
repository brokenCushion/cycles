# SPDX-License-Identifier: Apache-2.0
"""M5: gaffer env python validate_production_gaffer.py CYCLES OUTPUT_DIR.
Uses a 64-layer scene whose 101 MiB raw capture exceeds its 32 MiB deep budget.
Also renders the existing 640x480 primitive review geometry when available.
"""
import json
import math
from pathlib import Path
import subprocess
import sys
import time
import Gaffer
import GafferImage
import IECore
import imath
import CyclesDeep
sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_transparency_gaffer import material, plane, scene, reader
from validate_gaffer import check, deep_pixel, tile_index

exe, out = (Path(p).resolve() for p in sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
report = {'gaffer': Gaffer.About.versionString(), 'renders': {}}
xml = scene(material('layer', .9995), ''.join(plane('layer', 2+i*.125) for i in range(64)))
xml = xml.replace('width="16" height="12"', 'width="128" height="96"')
source = out/'layers.xml'
source.write_text(xml)

def render(name, src, width, height, events, reduce=False, budget=32, failure=False, deep=True):
    target = out/(name+'.deep.exr')
    cmd = [str(exe), '--background', '--quiet', '--device', 'CPU', '--threads', '8',
           '--samples','16','--width',str(width),'--height',str(height),
           '--output',str(out/(name+'.beauty.exr'))]
    if deep:
        cmd += ['--deep-output',str(target),'--deep-transparent','--deep-max-events',str(events),
                '--deep-memory-mb',str(budget)]
    if reduce: cmd += ['--deep-reduce']
    cmd += [str(src)]
    if failure: target.write_bytes(b'previous-complete-frame')
    start=time.monotonic()
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=900)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    report['renders'][name]={'seconds':time.monotonic()-start,'exit_code':p.returncode}
    if failure:
        check(p.returncode != 0 and target.read_bytes()==b'previous-complete-frame', 'unsafe memory failure')
    else:
        check(p.returncode==0 and 'ERROR:' not in p.stderr, p.stderr)
        if deep: report['renders'][name]['bytes']=target.stat().st_size
    return target

reference=reader(render('layers_unreduced',source,128,96,64))
reduced=reader(render('layers_reduced',source,128,96,64,True))
render('layers_beauty_only',source,128,96,64,deep=False)
render('memory_pressure',source,128,96,64,budget=1,failure=True)
stats={'pixels':0,'unreduced_samples':0,'reduced_samples':0,'max_curve_error':0.,'max_final_error':0.}
beauties=[reader(out/(n+'.beauty.exr')) for n in ('layers_unreduced','layers_reduced','layers_beauty_only')]
fmt=reference['out']['format'].getValue()
for y in range(96):
    for x in range(128):
        point=fmt.fromEXRSpace(imath.V2i(x,y))
        origin,index=tile_index(point)
        for channel in ('R','G','B','A'):
            values=[float(n['out'].channelData(channel,origin)[index]) for n in beauties]
            check(values[0]==values[1]==values[2], 'M5 changed beauty')
        curves=[deep_pixel(n['out'],point) for n in (reference,reduced)]
        stats['unreduced_samples']+=len(curves[0]); stats['reduced_samples']+=len(curves[1])
        # Sweep the union of boundaries; every interval probes both sides.
        indices=[0,0]; trans=[1.,1.]
        for z in sorted({sample[0] for curve in curves for sample in curve}):
            stats['max_curve_error']=max(stats['max_curve_error'],abs(trans[0]-trans[1]))
            for k in (0,1):
                while indices[k]<len(curves[k]) and curves[k][indices[k]][0]==z:
                    trans[k]*=1-curves[k][indices[k]][2]; indices[k]+=1
            stats['max_curve_error']=max(stats['max_curve_error'],abs(trans[0]-trans[1]))
        stats['max_final_error']=max(stats['max_final_error'],abs(trans[0]-trans[1]))
        stats['pixels']+=1
check(stats['max_curve_error']<=.001001,'reduced curve outside combined FLOAT budgets')
check(stats['max_final_error']<1e-6,'reduction changed final opacity')
check(stats['reduced_samples']<stats['unreduced_samples'],'no reduction')
report['layers']=stats
report['raw_capture_bytes']=128*96*16*(1+2*64)*4
report['deep_budget_bytes']=32*1024*1024

# Actual geometry review, alongside the high-depth-complexity fixture.
geometry=Path.cwd()/'build-m3/primitives-review/04_all_objects_reference.xml'
geometry_paths=[]
if geometry.exists():
    for flag in (False,True):
        geometry_paths.append(render('primitives_'+('reduced' if flag else 'unreduced'),geometry,640,480,8,flag))

script=Gaffer.ScriptNode()
def position(node,x,y):
    node.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(x,y),flags=Gaffer.Plug.Flags.Default|Gaffer.Plug.Flags.Dynamic))
for index,(name,path) in enumerate([('Layers_unreduced',out/'layers_unreduced.deep.exr'),
                                   ('Layers_reduced',out/'layers_reduced.deep.exr')]+
                                  [('Primitives_'+('reduced' if i else 'unreduced'),p) for i,p in enumerate(geometry_paths)]):
    n=reader(path); script[name]=n; position(n,index*35,0)
    flat=GafferImage.DeepToFlat(); script[name+'_Flat']=flat; flat['in'].setInput(n['out']); position(flat,index*35,-12)
    pc=CyclesDeep.DeepToPointCloud(); script[name+'_Points']=pc; pc['in'].setInput(n['out']); pc['maxPoints'].setValue(1000000)
    pc['color'].setValue(imath.Color3f(.12,.65,1) if 'unreduced' in name else imath.Color3f(1,.4,.08)); position(pc,index*35,-24)
    clip=GafferImage.DeepSlice(); script[name+'_Holdout']=clip; clip['in'].setInput(n['out'])
    clip['farClip']['enabled'].setValue(True); clip['farClip']['value'].setValue(5.2); clip['flatten'].setValue(True)
    position(clip,index*35,-36)
    # Force live node computation, including the 1M cap behavior.
    obj=pc['out'].object('/deepPoints')
    report.setdefault('pointclouds',{})[name]=len(obj['P'].data)
script['fileName'].setValue((out/'m5_review.gfr').as_posix()); script.save()
(out/'report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
