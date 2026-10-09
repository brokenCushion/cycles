# SPDX-License-Identifier: Apache-2.0
"""Experimental live deep-image to point-cloud adapter for Gaffer 1.7."""
import math
import Gaffer
import GafferImage
import GafferScene
import IECore
import IECoreScene
import imath


class DeepToPointCloud(Gaffer.ComputeNode):
    _controls = ('view', 'verticalFieldOfView', 'forwardAxis', 'pixelStride',
                 'alphaThreshold', 'maxPoints', 'color', 'useImageColor', 'depthChannel',
                 'cameraPath', 'cameraTransform')

    def __init__(self, name='DeepToPointCloud'):
        Gaffer.ComputeNode.__init__(self, name)
        self['in'] = GafferImage.ImagePlug()
        self['camera'] = GafferScene.ScenePlug()
        self['cameraPath'] = Gaffer.StringPlug(defaultValue='/camera')
        self['view'] = Gaffer.StringPlug(defaultValue='default')
        self['verticalFieldOfView'] = Gaffer.FloatPlug(defaultValue=math.degrees(.9), minValue=.01, maxValue=179)
        self['forwardAxis'] = Gaffer.IntPlug(defaultValue=1, minValue=-1, maxValue=1)
        self['cameraTransform'] = Gaffer.TransformPlug()
        self['pixelStride'] = Gaffer.IntPlug(defaultValue=1, minValue=1)
        self['alphaThreshold'] = Gaffer.FloatPlug(defaultValue=0, minValue=0, maxValue=1)
        self['maxPoints'] = Gaffer.IntPlug(defaultValue=5000000, minValue=1)
        self['depthChannel'] = Gaffer.StringPlug(defaultValue='Z')
        self['color'] = Gaffer.Color3fPlug(defaultValue=imath.Color3f(.7))
        self['useImageColor'] = Gaffer.BoolPlug(defaultValue=True)
        self['pointWidth'] = Gaffer.FloatPlug(defaultValue=1, minValue=.1, maxValue=20)
        self['__points'] = Gaffer.ObjectPlug(direction=Gaffer.Plug.Direction.Out, defaultValue=IECore.NullObject())
        self['__scene'] = GafferScene.ObjectToScene()
        self['__scene']['name'].setValue('deepPoints')
        self['__scene']['object'].setInput(self['__points'])
        self['__display'] = GafferScene.OpenGLAttributes()
        self['__display']['in'].setInput(self['__scene']['out'])
        self['__display']['global'].setValue(True)
        for key in ('gl:pointsPrimitive:useGLPoints', 'gl:pointsPrimitive:glPointWidth'):
            self['__display']['attributes'][key]['enabled'].setValue(True)
        self['__display']['attributes']['gl:pointsPrimitive:useGLPoints']['value'].setValue('forAll')
        self['__display']['attributes']['gl:pointsPrimitive:glPointWidth']['value'].setInput(self['pointWidth'])
        self['out'] = GafferScene.ScenePlug(direction=Gaffer.Plug.Direction.Out)
        self['out'].setInput(self['__display']['out'])

    def affects(self, plug):
        result = Gaffer.ComputeNode.affects(self, plug)
        if (self['in'].isAncestorOf(plug) or self['camera'].isAncestorOf(plug) or
            any(plug.isSame(self[n]) or self[n].isAncestorOf(plug) for n in self._controls)):
            result.append(self['__points'])
        return result

    def _context(self, context):
        c = Gaffer.Context(context)
        for key in ('scene:path', 'image:channelName', 'image:tileOrigin'):
            c.remove(key)
        c['image:viewName'] = self['view'].getValue()
        return c

    def _image(self):
        image = self['in']
        if self['view'].getValue() not in image['viewNames'].getValue():
            raise RuntimeError('DeepToPointCloud: selected image view does not exist')
        if not image['deep'].getValue():
            raise RuntimeError('DeepToPointCloud requires a deep image input; do not flatten it first')
        names = image['channelNames'].getValue()
        z = self['depthChannel'].getValue()
        if z not in names or 'A' not in names:
            raise RuntimeError('DeepToPointCloud requires depth and A channels')
        channels = [z, 'A']
        if 'ZBack' in names and 'ZBack' not in channels:
            channels.append('ZBack')
        if self['useImageColor'].getValue() and all(n in names for n in ('R','G','B')):
            channels += ['R','G','B']
        return image, channels

    @staticmethod
    def _tiles(window):
        size = GafferImage.ImagePlug.tileSize()
        for y in range((window.min().y//size)*size, window.max().y, size):
            for x in range((window.min().x//size)*size, window.max().x, size):
                yield imath.V2i(x,y)

    def hash(self, output, context, h):
        if not output.isSame(self['__points']):
            return
        h.append('CyclesDeep.DeepToPointCloud.v2')
        for n in self._controls:
            self[n].hash(h)
        if not self['in'].getInput():
            h.append('disconnected')
            return
        with self._context(context):
            image, channels = self._image()
            for n in ('format','dataWindow','channelNames','deep','viewNames'):
                image[n].hash(h)
            for origin in self._tiles(image['dataWindow'].getValue()):
                IECore.Canceller.check(context.canceller())
                h.append(image.sampleOffsetsHash(origin))
                for channel in channels:
                    h.append(image.channelDataHash(channel, origin))
            h.append(bool(self['camera'].getInput()))
            if self['camera'].getInput():
                path = self['cameraPath'].getValue()
                h.append(self['camera'].objectHash(path))
                h.append(self['camera'].fullTransformHash(path))

    def _projection(self, fmt):
        aspect = fmt.width()*fmt.getPixelAspect()/fmt.height()
        if self['camera'].getInput():
            path = self['cameraPath'].getValue()
            camera = self['camera'].object(path)
            if not isinstance(camera, IECoreScene.Camera):
                raise RuntimeError('DeepToPointCloud: cameraPath must identify a camera')
            projection = camera.getProjection()
            if projection not in ('perspective','orthographic'):
                raise RuntimeError('DeepToPointCloud supports perspective and orthographic cameras')
            return (camera.frustum(camera.getFilmFit(), aspect), projection,
                    -1, self['camera'].fullTransform(path))
        axis = self['forwardAxis'].getValue()
        if axis not in (-1,1):
            raise RuntimeError('DeepToPointCloud: forwardAxis must be +1 or -1')
        top = math.tan(math.radians(self['verticalFieldOfView'].getValue())/2)
        return (imath.Box2f(imath.V2f(-aspect*top,-top), imath.V2f(aspect*top,top)),
                'perspective', axis, self['cameraTransform'].matrix())

    def _eligible(self, image, channels, window, display, stride, threshold, context):
        size = GafferImage.ImagePlug.tileSize()
        for origin in self._tiles(window):
            offsets = image.sampleOffsets(origin)
            data = {n:image.channelData(n,origin) for n in channels}
            for y in range(max(origin.y,window.min().y), min(origin.y+size,window.max().y)):
                IECore.Canceller.check(context.canceller())
                if (y-display.min().y)%stride: continue
                for x in range(max(origin.x,window.min().x), min(origin.x+size,window.max().x)):
                    if (x-display.min().x)%stride: continue
                    pixel = (y-origin.y)*size+x-origin.x
                    for i in range(offsets[pixel-1] if pixel else 0, offsets[pixel]):
                        a = float(data['A'][i]); z = float(data[channels[0]][i])
                        if not math.isfinite(a) or not 0<=a<=1:
                            raise RuntimeError('DeepToPointCloud: invalid alpha')
                        if a<=threshold: continue
                        if not math.isfinite(z) or z<=0:
                            raise RuntimeError('DeepToPointCloud: depth must be finite and positive axial Z')
                        yield x,y,i,data,a,z

    @staticmethod
    def _sample_index(k, total, count):
        # One reproducible selection per disjoint stratum. Hash jitter avoids
        # locking onto every nth depth layer in regularly ordered deep stacks.
        start = k*total//count
        end = (k+1)*total//count
        seed = (k+0x9e3779b9) & 0xffffffff
        seed = ((seed ^ (seed >> 16))*0x85ebca6b) & 0xffffffff
        seed = ((seed ^ (seed >> 13))*0xc2b2ae35) & 0xffffffff
        seed ^= seed >> 16
        return start + seed % (end-start)

    def compute(self, output, context):
        if not output.isSame(self['__points']):
            return
        if not self['in'].getInput():
            output.setValue(IECoreScene.PointsPrimitive(IECore.V3fVectorData()))
            return
        with self._context(context):
            image, channels = self._image()
            fmt = image['format'].getValue(); window = image['dataWindow'].getValue()
            if fmt.width()<=0 or fmt.height()<=0:
                raise RuntimeError('DeepToPointCloud: invalid image format')
            frustum, projection, axis, matrix = self._projection(fmt)
            display = fmt.getDisplayWindow()
            positions = IECore.V3fVectorData(); colors = IECore.Color3fVectorData()
            alphas = IECore.FloatVectorData(); depths = IECore.FloatVectorData(); backs = IECore.FloatVectorData()
            stride = self['pixelStride'].getValue(); threshold = self['alphaThreshold'].getValue()
            limit = self['maxPoints'].getValue(); fallback = self['color'].getValue()
            args = (image, channels, window, display, stride, threshold, context)
            total = sum(1 for _ in self._eligible(*args))
            count = min(total, limit)
            target = self._sample_index(0,total,count) if count else -1
            for index,(x,y,i,data,a,z) in enumerate(self._eligible(*args)):
                if index != target: continue
                u=(x+.5-display.min().x)/fmt.width(); v=(y+.5-display.min().y)/fmt.height()
                px=frustum.min().x+u*frustum.size().x
                py=frustum.min().y+v*frustum.size().y
                if projection=='perspective': px*=z; py*=z
                positions.append(imath.V3f(px,py,axis*z)*matrix)
                colors.append(imath.Color3f(*(float(data[n][i])/a for n in ('R','G','B'))) if 'R' in data else fallback)
                alphas.append(a); depths.append(z)
                backs.append(float(data['ZBack'][i]) if 'ZBack' in data else z)
                if len(positions)==count: break
                target = self._sample_index(len(positions),total,count)
            points = IECoreScene.PointsPrimitive(positions)
            pv=IECoreScene.PrimitiveVariable
            for name,values in [('Cs',colors),('deepAlpha',alphas),('deepZ',depths),('deepZBack',backs)]:
                points[name]=pv(pv.Interpolation.Vertex,values)
            points['type']=pv(pv.Interpolation.Constant,IECore.StringData('gl:point'))
            points['width']=pv(pv.Interpolation.Constant,IECore.FloatData(.001))
            output.setValue(points)


IECore.registerRunTimeTyped(DeepToPointCloud, 'CyclesDeep::DeepToPointCloud')
Gaffer.Metadata.registerNode(DeepToPointCloud,
    'description', 'Live deep image to 3D points. Samples are reconstructed at pixel centers, then deterministically thinned to Max Points if needed. Positive axial depth only; original subpixel positions are unavailable. RGB is unpremultiplied for display. No mesh or scene cache is read. ZBack volumes are represented by a selected boundary, not filled.',
    'nodeGadget:color', imath.Color3f(.2,.45,.3),
    plugs={
        'in':['description','Connect a deep ImageReader, DeepMerge, or DeepSlice output.','nodule:type','GafferUI::StandardNodule'],
        'camera':['description','Optional camera scene; uses cameraPath, film fit and world transform. Gaffer cameras look down -Z. Scene globals are not applied.','nodule:type','GafferUI::StandardNodule'],
        'out':['nodule:type','GafferUI::StandardNodule'],
        'verticalFieldOfView':['description','Vertical FOV in degrees. Used when camera is unconnected.'],
        'forwardAxis':['description','Manual camera forward axis. Cycles standalone test uses +Z.','preset:+Z',1,'preset:-Z',-1,'plugValueWidget:type','GafferUI.PresetsPlugValueWidget'],
        'cameraTransform':['description','Manual camera-to-world transform; unused with a connected camera.'],
        'pixelStride':['description','1 keeps all pixels. 2 keeps every second pixel in X and Y. All deep samples at retained pixels are preserved.'],
        'alphaThreshold':['description','Discard samples whose effective deep alpha is at or below this value.'],
        'maxPoints':['description','Maximum displayed points. When exceeded, select a deterministic sample spread across the full input; lowering this reduces density without cutting off the end of the image.'],
        'pointWidth':['description','Viewport point width in pixels.'],
        'depthChannel':['description','Depth channel to reconstruct, normally Z. Select ZBack to inspect back boundaries.'],
        'useImageColor':['description','Use unpremultiplied deep RGB when all three channels exist; otherwise use the fallback color.'],
        'color':['description','Fallback point color for opacity/depth-only images.'],
    })

# Only the image, optional camera and scene output belong on the graph gadget.
for _name in DeepToPointCloud._controls + ('pointWidth',):
    Gaffer.Metadata.registerValue(DeepToPointCloud, _name, 'nodule:type', '')
