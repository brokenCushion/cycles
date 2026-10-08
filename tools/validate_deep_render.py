# SPDX-License-Identifier: Apache-2.0
"""Shared numerical alpha/accepted-population checks, independent of Gaffer."""
import csv
import json
import math
from pathlib import Path
import struct
import sys
from deep_exr import bound, check_deep, pixel, read

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/deep'))
from sample_csv import open_samples
from validate_volume_camera_curves import validate as camera_oracle


def validate(directory, samples=None, adaptive=None, ledger=None, output=None):
    directory=Path(directory)
    output=Path(output) if output is not None else directory
    output.mkdir(parents=True,exist_ok=True)
    image=read(directory/'scene.deep.exr')
    result=check_deep(image)
    settings=json.loads((directory/'render.json').read_text()) if (directory/'render.json').exists() else {}
    samples=samples if samples is not None else settings['samples']
    adaptive=adaptive if adaptive is not None else settings['adaptive']
    header=image.header()
    cap=settings.get('deep_samples') or 0
    effective=min(cap,samples) if cap else samples
    if bool(cap)!=('cycles:deepSamples' in header) or (cap and header['cycles:deepSamples']!=effective):
        raise ValueError('Deep sample count header mismatch')
    if bool(settings.get('deep_ids'))!=('id' in image.channels()):
        raise ValueError('Deep ID channel mismatch')
    requested=settings.get('deep_error',0)
    if bool(requested)!=('cycles:deepError' in header) or (requested and not math.isclose(result['deep_error'],requested,rel_tol=1e-7)):
        raise ValueError('Deep error header mismatch')
    source=Path(ledger) if ledger is not None else directory/'scene.deep.exr.samples.csv'
    normalized=None
    # Standalone surface ledgers encode FLOAT depths/alphas with max_digits10.
    with open_samples(source) as stream:
        fields=csv.DictReader(stream).fieldnames
    if ledger is not None:
        normalized=output/'normalized.samples.csv'
        with open_samples(source) as stream, normalized.open('w',newline='') as target:
            writer=csv.writer(target);writer.writerow(('file_x','file_y','sample','event','front','back','value','kind'))
            for row in csv.DictReader(stream):
                f32=lambda value:struct.unpack('f',struct.pack('f',float(value)))[0]
                if 'depth' in fields:
                    z,a=[f32(row[k]) for k in ('depth','alpha')]
                    values=[z,z,a,'miss' if int(row['event'])<0 else 'surface']
                elif row['kind']=='surface':
                    values=[f32(row[k]) for k in ('front','back','value')]+['surface']
                else:
                    values=[row[k] for k in ('front','back','value','kind')]
                writer.writerow([row[k] for k in ('file_x','file_y','sample','event')]+values)
        source=normalized
    w,h=result['width'],result['height']
    if ledger is None:
        coordinates={(i*(w-1)//8,j*(h-1)//8) for i in range(9) for j in range(9)}
    else:
        coordinates={(x,y) for y in range(h) for x in range(w)}
    stored=output/'stored_diagnostic_curves.json'
    stored.write_text(json.dumps(dict(samples=effective,adaptive=adaptive,deep_error=result['deep_error'],
        pixels=[dict(x=x,y=y,samples=pixel(image,x,y)) for x,y in sorted(coordinates)])))
    oracle=output/'accepted_camera_oracle.json'
    camera_oracle(source,stored,oracle)
    result['oracle']=json.loads(oracle.read_text())
    if settings and (settings['resolution'][0]*settings['percentage']//100,
                     settings['resolution'][1]*settings['percentage']//100)!=(w,h):
        raise ValueError('Deep image resolution mismatch')
    if settings.get('adaptive') and not all(0<count<=effective for count in result['oracle']['accepted_populations']):
        raise ValueError('Invalid adaptive population')
    # The camera oracle checks boundaries and midpoint cuts on both sides of steps.
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def boundary_analytic(directory,case):
    """Retain the boundary suite's independent geometry gate and its exact limit."""
    directory=Path(directory);maximum=0.
    offset=.5+1/(4*511)
    with open_samples(directory/'scene.csv') as stream:
        rows=csv.DictReader(stream)
        events={}
        for row in rows:
            if int(row['sample'])==0:
                key=(int(row['file_x']),int(row['file_y']))
                events.setdefault(key,[])
                if row['kind']!='miss':
                    events[key].append(tuple(float(row[k]) for k in ('front','back','value'))+(row['kind'],))
    w,h=case['width'],case['height']
    for (x,y),records in events.items():
        dx=(2*(x+offset)-w)/h*math.tan(.45)
        dy=(2*(h-1-y+offset)-h)/h*math.tan(.45)
        norm=math.sqrt(1+dx*dx+dy*dy)
        for z in (.25,1.125,2.125,3.125,4.125,4.875,5.125,6.125,7.125,8.125,10):
            tau=0.
            for bounds,density in case['media']:
                lo,hi=case['near'],min(case['far'],z)
                for direction,lower,upper in zip((dx,dy,1),bounds[:3],bounds[3:]):
                    if direction==0:
                        if not lower<=0<=upper:hi=lo
                    else:
                        a,b=sorted((lower/direction,upper/direction));lo,hi=max(lo,a),min(hi,b)
                tau+=max(0,hi-lo)*norm*density
            expected=math.exp(-tau)*(1-float(case['surface']) if 5<z and case['near']<5<=case['far'] else 1)
            actual=math.prod((1-v if a<z else 1) if kind=='surface' else
                             math.exp(-v*max(0,min(1,(z-a)/(b-a)))) for a,b,v,kind in records)
            maximum=max(maximum,abs(expected-actual))
    if maximum>2*bound(read(directory/'scene.deep.exr')):
        raise ValueError('Independent boundary geometry/extinction gate failed')
    return maximum
