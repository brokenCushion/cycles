# SPDX-License-Identifier: Apache-2.0
"""Convert the primitive review's exported deep samples to a Gaffer scene cache.

Run with gaffer env python. Every nonzero-alpha EXR sample becomes a point.
Uses pixel centers, not the unavailable original subpixel camera coordinates.
The camera is the review's static +Z perspective, vertical FOV .9 radians.
"""
import json
import math
from pathlib import Path
import Gaffer
import GafferImage
import GafferScene
import IECore
import IECoreScene
import imath

directory = Path.cwd() / 'build-m3/primitives-review'
scene_path = directory / 'deep_samples.scc'
scene = IECoreScene.SceneInterface.create(str(scene_path), IECore.IndexedIO.OpenMode.Write)
tile = GafferImage.ImagePlug.tileSize()
colors = [(1,.22,.08),(.08,.65,1),(.3,1,.15)]
names = ['01_near_sphere','02_middle_cube','03_far_cylinder']
report = {'projection':'pixel centers; camera +Z; vertical FOV .9 radians', 'objects':{}}

for name,color in zip(names,colors):
    reader = GafferImage.ImageReader()
    reader['fileName'].setValue((directory/(name+'.deep.exr')).as_posix())
    plug=reader['out']; fmt=plug['format'].getValue()
    assert plug['deep'].getValue()
    w,h=fmt.width(),fmt.height()
    assert (w,h)==(640,480) and fmt.getDisplayWindow().min()==imath.V2i(0)
    positions=IECore.V3fVectorData(); alphas=IECore.FloatVectorData()
    depths=IECore.FloatVectorData()
    factor=2*math.tan(.9/2)/h
    for ty in range(0,h,tile):
        for tx in range(0,w,tile):
            origin=imath.V2i(tx,ty)
            offsets=plug.sampleOffsets(origin)
            z=plug.channelData('Z',origin); a=plug.channelData('A',origin)
            for y in range(ty,min(ty+tile,h)):
                for x in range(tx,min(tx+tile,w)):
                    pixel=(y-ty)*tile+x-tx
                    first=offsets[pixel-1] if pixel else 0
                    for i in range(first,offsets[pixel]):
                        depth=float(z[i]); alpha=float(a[i])
                        if alpha<=0: continue
                        assert math.isfinite(depth) and depth>0 and math.isfinite(alpha)
                        positions.append(imath.V3f((x+.5-w/2)*factor*depth,
                                                  (y+.5-h/2)*factor*depth,depth))
                        alphas.append(alpha); depths.append(depth)
    points=IECoreScene.PointsPrimitive(positions)
    pv=IECoreScene.PrimitiveVariable
    points['Cs']=pv(pv.Interpolation.Constant,IECore.Color3fData(imath.Color3f(*color)))
    points['deepAlpha']=pv(pv.Interpolation.Vertex,alphas)
    points['deepZ']=pv(pv.Interpolation.Vertex,depths)
    points['width']=pv(pv.Interpolation.Constant,IECore.FloatData(.008))
    points['type']=pv(pv.Interpolation.Constant,IECore.StringData('gl:point'))
    assert points.arePrimitiveVariablesValid()
    child=scene.createChild(name); child.writeObject(points,0)
    child.writeBound(imath.Box3d(points.bound()),0)
    report['objects'][name]={'points':len(positions),'bound':str(points.bound())}
    print(name,report['objects'][name],flush=True)
    del child
del scene

script=Gaffer.ScriptNode()
script['DEEP_POINTCLOUD_samples']=GafferScene.SceneReader()
script['DEEP_POINTCLOUD_samples']['fileName'].setValue(scene_path.as_posix())
script['VIEW_DEEP_POINTCLOUD']=GafferScene.OpenGLAttributes()
display=script['VIEW_DEEP_POINTCLOUD']
display['in'].setInput(script['DEEP_POINTCLOUD_samples']['out'])
display['global'].setValue(True)
for key,value in [('gl:pointsPrimitive:useGLPoints','forAll'),('gl:pointsPrimitive:glPointWidth',1.)]:
    display['attributes'][key]['enabled'].setValue(True)
    display['attributes'][key]['value'].setValue(value)
for n,pos in [(script['DEEP_POINTCLOUD_samples'],(-25,-65)),(display,(-25,-80))]:
    n.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(*pos),flags=Gaffer.Plug.Flags.Default|Gaffer.Plug.Flags.Dynamic))
    Gaffer.Metadata.registerValue(n,'description','All nonzero-alpha exported deep samples reconstructed at pixel centers. +Z is forward. Colors identify objects. deepAlpha and deepZ are point primitive variables. This is a static cache; rerun create_deep_pointcloud.py after rendering changes.')
Gaffer.Metadata.registerValue(display,'nodeGadget:color',imath.Color3f(.2,.45,.3))
for name in names:
    obj=display['out'].object('/'+name)
    assert obj.numPoints==report['objects'][name]['points']
    assert obj.arePrimitiveVariablesValid()
script['fileName'].setValue((directory/'deep_pointcloud_nodes.gfr').as_posix()); script.save()
report['total_points']=sum(x['points'] for x in report['objects'].values())
(directory/'pointcloud_report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
