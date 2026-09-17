# SPDX-License-Identifier: Apache-2.0
"""Additional M5 pairing, output failures and Gaffer holdout checks."""
import json
from pathlib import Path
import subprocess
import sys
import GafferImage
import imath
sys.path.insert(0,str(Path(__file__).resolve().parent))
from validate_gaffer import check, flat_alpha, deep_pixel
from validate_transparency_gaffer import reader
exe,out=(Path(p).resolve() for p in sys.argv[1:3])
source=out/'layers.xml'
results={}
for case in ('beauty_failure','deep_failure'):
    beauty=out/(case+('.unsupportedformat' if case=='beauty_failure' else '.beauty.exr'))
    target=out/(case+'.deep.exr')
    if case=='beauty_failure':
        beauty.write_bytes(b'old-beauty'); target.write_bytes(b'old-deep')
    else:
        target.mkdir(exist_ok=True)
    cmd=[str(exe),'--background','--quiet','--device','CPU','--samples','1','--threads','2',
         '--width','4','--height','4','--deep-transparent','--deep-max-events','64',
         '--output',str(beauty),'--deep-output',str(target),str(source)]
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
    check(p.returncode!=0,case+' falsely succeeded')
    if case=='beauty_failure':
        check(target.read_bytes()==b'old-deep','failed beauty published deep')
    else: check(target.is_dir() and beauty.is_file(),'deep failure damaged destination')
    results[case]=p.stderr.strip()
nodes=[reader(out/(n+'.deep.exr')) for n in ('layers_unreduced','layers_reduced')]
md=nodes[1]['out']['metadata'].getValue()
identity=str(md['cycles:beautyIdentity'])
beauty=out/'layers_reduced.beauty.exr'
data=beauty.read_bytes(); h=14695981039346656037
for b in data: h=((h^b)*1099511628211)&((1<<64)-1)
check(identity.startswith(f'fnv1a64:{h}:bytes:{len(data)}:path:'),'beauty/deep identity mismatch')
clips=[]
for node in nodes:
    clip=GafferImage.DeepSlice(); clip['in'].setInput(node['out'])
    clip['nearClip']['enabled'].setValue(False); clip['farClip']['enabled'].setValue(True)
    clip['flatten'].setValue(True); clips.append(clip)
maximum=0.
for depth in (1.,2.,2.0625,3.,5.2,7.4,9.9375,12.):
    for clip in clips: clip['farClip']['value'].setValue(depth)
    for x,y in ((0,0),(64,48),(127,95),(23,61)):
        values=[flat_alpha(c['out'],imath.V2i(x,y)) for c in clips]
        maximum=max(maximum,abs(values[0]-values[1]))
check(maximum<.001001,'Gaffer depth holdout exceeded contract')
results['max_gaffer_holdout_error']=maximum
results['beauty_identity_verified']=True
(out/'lifecycle_report.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
