"""Check --deep-error and surface reduction against the raw camera ledger.
Run with gaffer env python tools/validate_deep_error.py CYCLES_EXE OUTPUT.
"""
import csv,json,math,os,subprocess,sys,struct
from pathlib import Path
import GafferImage,imath
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/deep'))
from validate_gaffer import deep_error,deep_pixel
def captured_float(value):
    # CSV uses max_digits10: restore FLOAT before querying either side of a step.
    return struct.unpack('f',struct.pack('f',float(value)))[0]

exe,out=Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
scene=out/'weak-surfaces.xml'
text='''<cycles><camera camera_type="perspective" fov="0.9" nearclip="0.125" farclip="20"/>
<film filter_type="box" filter_width="1"/><integrator seed="123" use_adaptive_sampling="false"/>
<background transparent="true"><background name="b" color="0.1 0.1 0.1"/><connect from="b background" to="output surface"/></background><shader name="weak"><emission name="e" color="0.3 0.5 0.7"/>
<transparent_bsdf name="t" color="1 1 1"/><mix_closure name="m" fac="0.99995"/>
<connect from="e emission" to="m closure1"/><connect from="t bsdf" to="m closure2"/>
<connect from="m closure" to="output surface"/></shader>'''
for i in range(32):
 z=1+i*.25;text+=f'<state shader="weak"><mesh P="-10 -10 {z} 10 -10 {z} 10 10 {z} -10 10 {z}" nverts="4" verts="0 3 2 1"/></state>'
scene.write_text(text+'</cycles>');report={}
env=dict(os.environ);env.pop('CYCLES_KERNEL_PATH',None);env.pop('OCIO',None)
for mode in ('strict','1e-4','1e-3'):
 target=out/(mode+'.deep.exr');ledger=out/(mode+'.csv')
 command=[str(exe),'--device','CPU','--threads','4','--samples','4','--width','8','--height','6','--output',str(out/(mode+'.beauty.exr')),'--deep-error',mode,'--deep-transparent','--deep-reduce','--deep-max-events','64','--deep-output',str(target),'--deep-records',str(ledger),str(scene)]
 with (out/(mode+'.log')).open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 reader=GafferImage.ImageReader();reader['fileName'].setValue(target.as_posix());bound=deep_error(reader['out']);raw={}
 for r in csv.DictReader(ledger.open()):
  key=tuple(int(r[k]) for k in ('file_x','file_y','sample'));raw.setdefault(key,[])
  if int(r['event'])>=0:raw[key].append((captured_float(r['depth']),captured_float(r['alpha'])))
 maximum,count=0,0
 for y in range(6):
  for x in range(8):
   pixel=deep_pixel(reader['out'],reader['out']['format'].getValue().fromEXRSpace(imath.V2i(x,y)));count+=len(pixel)
   boundaries=sorted({z for i in range(4) for z,a in raw[x,y,i]}|{z for z,back,a in pixel})
   for z in [0,20]+[v for b in boundaries for v in (math.nextafter(b,-math.inf),b,math.nextafter(b,math.inf))]:
    expected=sum(math.prod(1-a for depth,a in raw[x,y,i] if depth<=z) for i in range(4))/4
    observed=math.prod(1-a for depth,back,a in pixel if depth<=z);maximum=max(maximum,abs(expected-observed))
 assert maximum<=bound,(mode,maximum,bound)
 report[mode]=dict(passed=True,deep_error=bound,max_error=maximum,samples=count)
assert report['1e-4']['samples']>report['1e-3']['samples'],'Shared setting did not affect surface reduction'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
