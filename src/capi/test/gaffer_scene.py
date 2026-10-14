"""Identical small host scene construction for the native and C-only backends.

Run inside the local Gaffer Python, with an explicit guarded output directory.
This is a fixture producer; the separate comparator owns acceptance criteria.
"""
import ctypes
import json
import os
import sys
import time
import faulthandler
faulthandler.dump_traceback_later(90)
sys.dont_write_bytecode = True
from workspace_guard import output, environment, disk
root = output(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
os.environ.update(environment(root/'temp'))
disk()
dll_directories = [os.add_dll_directory(str(output(os.environ['GAFFER_ROOT'])/part))
                   for part in ('bin','lib')]
import imath
import IECore
import IECoreScene
import IECoreImage
import GafferScene
import GafferCycles

backend, fixture, device = sys.argv[2:5]
if backend == 'CyclesC':
    plugin = ctypes.WinDLL(str(output(os.environ['CYCLES_C_GAFFER_PLUGIN'])))
Renderer = GafferScene.Private.IECoreScenePreview.Renderer
interactive = fixture == 'interactive'
renderer = Renderer.create(backend, Renderer.RenderType.Interactive if interactive else Renderer.RenderType.Batch)
for name, value in {
    'cycles:session:samples': IECore.IntData(16),
    'cycles:session:threads': IECore.IntData(4),
    'cycles:integrator:seed': IECore.IntData(int(os.environ.get('CAPI_TEST_SEED','1'))),
    'cycles:shadingsystem': IECore.StringData('SVM'),
    'cycles:device': IECore.StringData(device),
}.items():
    renderer.option(name, value)
if backend == 'Cycles':
    actual_device = renderer.command('cycles:querySession', {})['device'].value
    actual_backend = 'OPTIX' if actual_device.endswith('_OptiX') else actual_device.split('_')[0].upper()
    assert actual_backend==device, (device,actual_device)
    output(root/'native-session.txt').write_text(str(renderer.command('cycles:querySession', {})))
destination = output(root/'rgba.exr')
def configure_outputs():
    for name in ('rgba','sample_count','normal','depth'):
        path = destination if name=='rgba' else output(root/(name+'.exr'))
        if interactive and backend=='Cycles':
            renderer.output(name, IECoreScene.Output(str(path),'ieDisplay',name,
                {'driverType':'ImageDisplayDriver','handle':name}))
        else:
            renderer.output(name, IECoreScene.Output(str(path),'exr',name,{}))
configure_outputs()
camera = IECoreScene.Camera()
camera.setResolution(imath.V2i(64, 48))
camera.setProjection('perspective')
cameraHandle = renderer.camera('/camera', camera)
cameraHandle.transform(imath.M44f())
renderer.option('camera', IECore.StringData('/camera'))

def attributes(color, diffuse=False):
    shader = IECoreScene.Shader('diffuse_bsdf' if diffuse else 'emission',
                                'cycles:surface', {'color': color})
    if not diffuse:
        shader.parameters['strength'] = IECore.FloatData(1)
    return renderer.attributes(IECore.CompoundObject({
        'cycles:surface': IECoreScene.ShaderNetwork(shaders={'surface': shader}, output='surface')
    }))

handles = []
mesh = IECoreScene.MeshPrimitive.createPlane(imath.Box2f(imath.V2f(-1), imath.V2f(1)))
if fixture in ('emission','interactive'):
    settings = [('plane', imath.Color3f(.25, .5, 1), imath.V3f(0, 0, -3))]
elif fixture == 'meshes':
    settings = [('left', imath.Color3f(1, .25, .125), imath.V3f(-.8, 0, -3)),
                ('right', imath.Color3f(.125, 1, .25), imath.V3f(.8, .2, -4))]
elif fixture == 'point-light':
    settings = [('plane', imath.Color3f(.5), imath.V3f(0, 0, -3))]
    lightAttributes = renderer.attributes(IECore.CompoundObject({
        'cycles:light': IECoreScene.ShaderNetwork(shaders={
            'light': IECoreScene.Shader('point_light', 'cycles:light', {
                'color': imath.Color3f(1), 'intensity': 10.0, 'size': 0.0,
            })}, output='light')
    }))
    light = renderer.light('/light', None, lightAttributes)
    light.transform(imath.M44f().translate(imath.V3f(1, 1, -1)))
    handles.append(light)
else:
    raise ValueError(fixture)
for name, color, position in settings:
    handle = renderer.object('/'+name, mesh, attributes(color, fixture == 'point-light'))
    handle.transform(imath.M44f().translate(position))
    handles.append(handle)
renderer.render()
def finish_interactive():
    deadline = time.monotonic()+120
    if backend == 'CyclesC':
        while renderer.command('capi:progress',{}) < 1:
            assert time.monotonic()<deadline, 'Interactive render timed out'
            time.sleep(.05)
        renderer.pause()
        renderer.command('capi:save',{})
    else:
        while True:
            count_image=IECoreImage.ImageDisplayDriver.storedImage('sample_count')
            if count_image is not None and min(count_image['sample_count'])==1.0:
                break
            assert time.monotonic()<deadline, 'Interactive native output timed out'
            time.sleep(.05)
        renderer.pause()
        for name in ('rgba','sample_count','normal','depth'):
            image = IECoreImage.ImageDisplayDriver.storedImage(name)
            assert image is not None, name
            path = destination if name=='rgba' else output(root/(name+'.exr'))
            writer = IECoreImage.ImageWriter(image,str(path))
            writer['formatSettings']['openexr']['dataType'].setTypedValue('float')
            writer.write()
if interactive:
    finish_interactive()
    destination = output(root/'edited.exr')
    if backend=='Cycles':
        # Do not mistake the previous display's complete count buffer for the
        # next render. updateOutputs installs fresh drivers for the new output.
        for name in ('rgba','sample_count','normal','depth'):
            renderer.output(name,None)
            IECoreImage.ImageDisplayDriver.removeStoredImage(name)
    configure_outputs()
    handles[-1].transform(imath.M44f().translate(imath.V3f(.5,.25,-3)))
    assert handles[-1].attributes(attributes(imath.Color3f(.5,.125,.25)))
    renderer.render()
    finish_interactive()
assert destination.is_file(), destination
features={}
if backend=='CyclesC':
    front=ctypes.WinDLL(str(output(os.environ['CYCLES_C_LIBRARY'])))
    front.ccl_has_feature.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_int32)]
    front.ccl_has_feature.restype=ctypes.c_int
    supported=ctypes.c_int32()
    assert front.ccl_has_feature(b'deep',ctypes.byref(supported))==0
    features['deep']=bool(supported.value)
output(root/'fixture.json').write_text(json.dumps(dict(
    backend=backend, fixture=fixture, device=device, samples=16, resolution=[64,48],
    output=str(destination),seed=int(os.environ.get('CAPI_TEST_SEED','1')),interactive=interactive,features=features), indent=2))
print('Scene rendered:', backend, fixture, device)
faulthandler.cancel_dump_traceback_later()
