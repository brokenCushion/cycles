# SPDX-License-Identifier: Apache-2.0
"""Compare M6 CUDA with CPU and validate CUDA EXRs against their raw ledgers.
Run in Gaffer's Python with EXE OUTPUT_DIR; CUDA compiler must be on PATH.
"""
import csv
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time
import Gaffer
import GafferImage
import imath
sys.path.insert(0,str(Path(__file__).resolve().parent))
from validate_transparency_gaffer import material, plane, scene, reader
from validate_gaffer import check, deep_pixel, tile_index

exe,out=(Path(p).resolve() for p in sys.argv[1:3])
out.mkdir(parents=True,exist_ok=True)
stack=material('near',.75)+material('far',.5)
checker='''<texture_coordinate name="uv"/><checker_texture name="c" scale="7"/>
<connect from="uv generated" to="c vector"/><connect from="c fac" to="mix fac"/>'''
(out/'opacity.ppm').write_bytes(b'P6\n2 2\n255\n'+bytes([64]*3+[192]*3+[192]*3+[64]*3))
texture='''<image_texture name="c" filename="opacity.ppm" colorspace="raw" interpolation="closest"/>
<connect from="c color" to="t color"/>'''
fixtures={
    'stack':scene(stack,plane('near',2)+plane('far',8)),
    'diffuse':scene(stack.replace('<emission name="e"','<diffuse_bsdf name="e"').replace('from="e emission"','from="e bsdf"'),plane('near',2)+plane('far',8)),
    'opaque_back':scene(stack+material('back',0),plane('near',2)+plane('far',8)+plane('back',10)),
    'miss':scene(material('near',.5),''),
    'clear':scene(material('near',1),plane('near',2)),
    'cutoff':scene(material('near',1e-8),plane('near',2)),
    'beauty_limit':scene(stack,plane('near',2)+plane('far',8)).replace('seed="123"','seed="123" transparent_max_bounce="1"'),
    'opaque':scene('<shader name="near"><diffuse_bsdf name="d"/><connect from="d bsdf" to="output surface"/></shader>',plane('near',2)),
    'cutout':scene(material('near',.5,checker)+material('far',.5),plane('near',2,'translate="0.3 0.2 0" scale="0.7 0.6 1"')+plane('far',8)),
    'texture':scene(material('near',1,texture),plane('near',2)),
    'layers64':scene(material('layer',.99),''.join(plane('layer',2+i*.1) for i in range(64))),
}
report={'device':'CUDA','scenes':{},'rejections':[]}

def render(name,xml,device='CUDA',deep=True,extra=(),failure=False):
    src=out/(name+'.xml'); src.write_text(xml)
    target=out/(name+'.deep.exr'); beauty=out/(name+'.beauty.exr'); records=out/(name+'.csv')
    command=[str(exe),'--background','--quiet','--device',device,'--shadingsys','svm',
             '--samples','16','--threads','4','--width','16','--height','12','--output',str(beauty)]
    if deep:
        command+=['--deep-output',str(target),'--deep-records',str(records)]
        if not name.startswith('opaque_') or name.startswith('opaque_back_'):
            command+=['--deep-transparent']
        if name.startswith('layers64_'):
            command+=['--deep-max-events','64']
    command+=list(extra)+[str(src)]
    if failure: target.write_bytes(b'previous-complete-frame')
    start=time.monotonic()
    p=subprocess.run(command,capture_output=True,text=True,timeout=900,cwd=out)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    elapsed=time.monotonic()-start
    if failure:
        check(p.returncode!=0 and target.read_bytes()==b'previous-complete-frame', name+' unsafe failure')
        report['rejections'].append(name)
    else:
        check(p.returncode==0 and 'ERROR:' not in p.stderr, name+': '+p.stderr[-4000:])
    return beauty,target,records,elapsed

def ledger(path):
    raw={(x,y,s):[] for y in range(12) for x in range(16) for s in range(16)}; seen=set(); misses=set()
    for row in csv.DictReader(path.open()):
        key=tuple(int(row[k]) for k in ('file_x','file_y','sample'))
        check(key in raw,'out-of-range identity')
        z,a=(struct.unpack('f',struct.pack('f',float(row[k])))[0] for k in ('depth','alpha'))
        i=int(row['event'])
        if i<0:
            check(key not in seen and z==a==0,'duplicate miss')
            misses.add(key)
        else:
            check(key not in misses,'event after miss')
            check(i==len(raw[key]),'duplicate or incomplete event')
            raw[key].append((z,a))
        seen.add(key)
    check(len(seen)==len(raw),'incomplete camera ledger')
    return raw

for name,xml in fixtures.items():
    if len(sys.argv)>3 and name not in sys.argv[3:]:
        continue
    cpu=render(name+'_cpu',xml,'CPU'); gpu=render(name+'_cuda',xml); off=render(name+'_off',xml,deep=False)
    a,b=ledger(cpu[2]),ledger(gpu[2])
    stats={'cpu_seconds':cpu[3],'cuda_seconds':gpu[3],'cuda_off_seconds':off[3],
           'max_depth_error':0.,'max_local_alpha_error':0.,'max_export_curve_error':0.,'max_beauty_error':0.}
    for key,events in a.items():
        check(len(events)==len(b[key]),name+' CPU/CUDA chain lengths differ')
        for (z,alpha),(gz,ga) in zip(events,b[key]):
            stats['max_depth_error']=max(stats['max_depth_error'],abs(z-gz))
            stats['max_local_alpha_error']=max(stats['max_local_alpha_error'],abs(alpha-ga))
    check(stats['max_depth_error']<2e-5 and stats['max_local_alpha_error']<1e-6,name+' CPU/CUDA mismatch')
    deep=reader(gpu[1]); onbeauty=reader(gpu[0]); offbeauty=reader(off[0]); fmt=deep['out']['format'].getValue()
    for y in range(12):
        for x in range(16):
            point=fmt.fromEXRSpace(imath.V2i(x,y)); origin,index=tile_index(point)
            for channel in ('R','G','B','A'):
                values=[float(n['out'].channelData(channel,origin)[index]) for n in (onbeauty,offbeauty)]
                stats['max_beauty_error']=max(stats['max_beauty_error'],abs(values[0]-values[1]))
            actual=deep_pixel(deep['out'],point)
            depths={z for s in range(16) for z,alpha in b[x,y,s]}|{e[0] for e in actual}
            for depth in depths:
                for inclusive in (False,True):
                    def include(z): return z<=depth if inclusive else z<depth
                    expected=sum(math.prod(1-alpha for z,alpha in b[x,y,s] if include(z)) for s in range(16))/16
                    observed=math.prod(1-alpha for z,back,alpha in actual if include(z))
                    stats['max_export_curve_error']=max(stats['max_export_curve_error'],abs(expected-observed))
    check(stats['max_export_curve_error']<1e-6,name+' CUDA exported curve mismatch')
    # GPU beauty sums can arrive in different orders; allow small FLOAT accumulation noise.
    check(stats['max_beauty_error']<2e-6,name+' beauty changed')
    report['scenes'][name]=stats
    print(name,stats,flush=True)
render('event_overflow',fixtures['stack'],extra=('--deep-max-events','1'),failure=True)
render('exact_capacity',fixtures['stack'],extra=('--deep-max-events','2'))
render('clear_overflow',scene(material('near',1),plane('near',2)+plane('near',8)),extra=('--deep-max-events','1'),failure=True)
render('colored',scene(material('near',1).replace('color="1 1 1"','color="0.2 0.5 0.8"'),plane('near',2)),failure=True)
render('gpu_osl',fixtures['stack'],extra=('--shadingsys','osl'),failure=True)
(out/'report.json').write_text(json.dumps(report,indent=2))
print('CUDA acceptance passed',flush=True)
