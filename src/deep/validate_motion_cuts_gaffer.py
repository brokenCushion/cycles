# SPDX-License-Identifier: Apache-2.0
"""Verify 640x480 motion EXRs against each backend's complete raw ledger.

Gaffer Python: REVIEW_DIRECTORY. Eight partial-depth transmittance probes,
16 samples per pixel, including explicit misses. Does not waive CPU/CUDA
intersection differences or use one backend as the other's geometric oracle.
"""
from array import array
import csv
import json
import math
from pathlib import Path
import sys
import GafferImage
import imath

directory = Path(sys.argv[1]).resolve()
W,H,S=640,480,16
cuts=(3.,4.,5.2,6.5,8.8,10.,12.,14.)
report={}
tile=GafferImage.ImagePlug.tileSize()
for device in ('CPU','CUDA'):
    reader=GafferImage.ImageReader()
    reader['fileName'].setValue((directory/(device+'.deep.exr')).as_posix())
    slicer=GafferImage.DeepSlice();slicer['in'].setInput(reader['out'])
    slicer['farClip']['enabled'].setValue(True);slicer['flatten'].setValue(True)
    actual=[]
    for cut in cuts:
        slicer['farClip']['value'].setValue(cut)
        pixels=array('f',[0.])*(W*H)
        for y in range(0,H,tile):
            for x in range(0,W,tile):
                data=slicer['out'].channelData('A',imath.V2i(x,y))
                for j in range(min(tile,H-y)):
                    for i in range(min(tile,W-x)):
                        pixels[(H-1-y-j)*W+x+i]=data[j*tile+i]
        actual.append(pixels)
    maximum=[0.]*len(cuts)
    pixel_count=0
    def verify(index,population):
        global pixel_count
        assert index==pixel_count and len(population)==S, 'Incomplete pixel population'
        for k,cut in enumerate(cuts):
            expected=1-sum(math.prod(1-a for z,a in e if z<cut) for e in population)/S
            maximum[k]=max(maximum[k],abs(expected-actual[k][index]))
        pixel_count+=1
    previous=-1;population=[];last_miss=False
    with (directory/(device+'-records.csv')).open() as stream:
        for x,y,s,z,a,e in csv.reader(stream):
            if x=='file_x':continue
            assert 0<=int(x)<W and 0<=int(y)<H, 'Invalid pixel identity'
            index=int(y)*W+int(x);sample=int(s);event=int(e)
            if index!=previous:
                if previous>=0:verify(previous,population)
                previous=index;population=[]
            if sample==len(population):
                population.append([]);last_miss=False
            assert sample==len(population)-1 and not last_miss, 'Invalid sample sequence'
            if event<0:
                assert event==-1 and not population[-1] and float(z)==float(a)==0, 'Invalid miss'
                last_miss=True
            else:
                assert event==len(population[-1]), 'Invalid event sequence'
                population[-1].append((float(z),float(a)))
    verify(previous,population)
    assert pixel_count==W*H and max(maximum)<1e-6, 'Deep cuts disagree with raw ledger'
    report[device]={'pixels':pixel_count,'samples':pixel_count*S,
                    'max_alpha_error':max(maximum),'cuts':dict(zip(map(str,cuts),maximum))}
    print(device,report[device],flush=True)
(directory/'raw_cut_validation.json').write_text(json.dumps(report,indent=2))
