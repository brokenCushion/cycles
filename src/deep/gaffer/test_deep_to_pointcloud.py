# SPDX-License-Identifier: Apache-2.0
"""Run using the installed Gaffer's `env python` command."""
import math
import Gaffer
import GafferImage
import GafferScene
import IECore
import imath
import CyclesDeep

s=Gaffer.ScriptNode()
for name,z in [('near',2),('far',6)]:
    s[name+'Color']=GafferImage.Constant()
    s[name+'Color']['format'].setValue(GafferImage.Format(2,2,1))
    s[name+'Color']['color'].setValue(imath.Color4f(.2,.1,.05,.5))
    s[name]=GafferImage.FlatToDeep()
    s[name]['in'].setInput(s[name+'Color']['out'])
    s[name]['depth'].setValue(z)
s['merge']=GafferImage.DeepMerge()
for i,name in enumerate(('near','far')): s['merge']['in'][i].setInput(s[name]['out'])
s['cloud']=CyclesDeep.DeepToPointCloud()
n=s['cloud']; n['in'].setInput(s['merge']['out'])
n['verticalFieldOfView'].setValue(90)

def points(node=n): return node['out'].object('/deepPoints')
def reject(f,text):
    try: f()
    except Exception as e: assert text in str(e),str(e)
    else: raise AssertionError('Expected failure: '+text)

p=points(); assert p.numPoints==8 and p.arePrimitiveVariablesValid()
assert p['P'].data[0].equalWithAbsError(imath.V3f(-1,-1,2),1e-6)
assert p['P'].data[1].equalWithAbsError(imath.V3f(-3,-3,6),1e-6)
assert p['Cs'].data[0].equalWithAbsError(imath.Color3f(.4,.2,.1),1e-6)
assert list(p['deepAlpha'].data)==[.5]*8
s['nearColor']['color'].setValue(imath.Color4f(.4,.2,.1,.5))
assert points()['Cs'].data[0].equalWithAbsError(imath.Color3f(.8,.4,.2),1e-6)
s['nearColor']['color'].setValue(imath.Color4f(.2,.1,.05,.5))
for name in ('nearColor','farColor'):
    s[name]['format'].setValue(GafferImage.Format(imath.Box2i(imath.V2i(-2,-1),imath.V2i(0,1)),2))
p=points(); assert p.numPoints==8
assert p['P'].data[0].equalWithAbsError(imath.V3f(-2,-1,2),1e-6)
for name in ('nearColor','farColor'): s[name]['format'].setValue(GafferImage.Format(2,2,1))
n['pixelStride'].setValue(2); assert points().numPoints==2
n['pixelStride'].setValue(1)
s['far']['depth'].setValue(9); assert points()['P'].data[1].z==9
n['alphaThreshold'].setValue(.5); assert points().numPoints==0
n['alphaThreshold'].setValue(0)
full=points()
for limit in (1,4,7,8,20):
    n['maxPoints'].setValue(limit)
    reduced=points()
    assert reduced.numPoints==min(limit,8) and reduced.arePrimitiveVariablesValid()
    for i,p in enumerate(reduced['P'].data):
        j=list(full['P'].data).index(p)
        for key in ('Cs','deepAlpha','deepZ','deepZBack'):
            assert reduced[key].data[i]==full[key].data[j]
    assert points()==reduced
    if limit>=8: assert reduced==full
# Sampling spans the full input and does not repeatedly choose one depth layer.
selected=[n._sample_index(i,1000,100) for i in range(100)]
assert selected==sorted(set(selected)) and selected[0]<10 and selected[-1]>=990
assert 30<sum(i%2==0 for i in selected)<70
n['maxPoints'].setValue(20)
n['forwardAxis'].setValue(-1); assert points()['P'].data[0].z==-2
n['forwardAxis'].setValue(1)
n['cameraTransform']['translate'].setValue(imath.V3f(10,20,30))
assert points()['P'].data[0].equalWithAbsError(imath.V3f(9,19,32),1e-6)
n['cameraTransform']['translate'].setValue(imath.V3f(0))
n['useImageColor'].setValue(False); n['color'].setValue(imath.Color3f(.8,.3,.2))
assert points()['Cs'].data[0]==imath.Color3f(.8,.3,.2)
n['useImageColor'].setValue(True)
n['in'].setInput(s['nearColor']['out']); reject(points,'requires a deep image')
n['in'].setInput(s['merge']['out'])
n['depthChannel'].setValue('missing'); reject(points,'requires depth'); n['depthChannel'].setValue('Z')
s['camera']=GafferScene.Camera(); s['camera']['name'].setValue('camera')
n['camera'].setInput(s['camera']['out'])
assert points()['P'].data[0].z==-2
s['camera']['transform']['translate'].setValue(imath.V3f(0,0,10))
assert points()['P'].data[0].z==8
s['camera']['projection'].setValue('orthographic')
p=points(); assert p['P'].data[0].x==p['P'].data[1].x
n['camera'].setInput(None)
s['animation']=Gaffer.Expression()
s['animation'].setExpression('parent["near"]["depth"] = context.getFrame()','python')
with Gaffer.Context() as c:
    c.setFrame(3); assert points()['P'].data[0].z==3
    c.setFrame(4); assert points()['P'].data[0].z==4
copy=Gaffer.ScriptNode(); copy.execute(s.serialise())
assert points(copy['cloud'])==points()
copy['cloud']['pixelStride'].setValue(2); assert points(copy['cloud']).numPoints==2
copy['cloud']['in'].setInput(None); assert points(copy['cloud']).numPoints==0
print('PASS: deep sample preservation, projection, RGB, live updates, frame changes, camera transforms, orthographic, filtering, limits, invalid input and serialization')
