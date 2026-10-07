# SPDX-License-Identifier: Apache-2.0
"""Numerical OpenEXR readers. Gaffer is only needed for interactive review."""
import json
import math
from pathlib import Path
from collections import namedtuple


class FlatFormat(namedtuple('FlatFormat', 'w h aspect')):
    def width(self):return self.w
    def height(self):return self.h


def image_reader(path, backend='openexr'):
    if backend == 'gaffer':
        import GafferImage
        node=GafferImage.ImageReader();node['fileName'].setValue(Path(path).as_posix())
        return node
    if backend != 'openexr':raise ValueError('Unknown image reader backend')
    import numpy as np
    image=read(path)
    return {'channels':{name:np.flipud(c.pixels) for name,c in image.channels().items()},'header':image.header()}


def image_format(node):
    if not isinstance(node,dict):return node['out']['format'].getValue()
    (h,w)=next(iter(node['channels'].values())).shape
    return FlatFormat(w,h,node['header']['pixelAspectRatio'])


def image_channels(node):
    if not isinstance(node,dict):return list(node['out']['channelNames'].getValue())
    def order(name):
        layer,_,channel=name.rpartition('.')
        return layer,'RGBAXYZ'.index(channel) if channel in tuple('RGBAXYZ') else 7,channel
    return sorted(node['channels'],key=order)


def image_tile_size(backend='openexr'):
    if backend=='gaffer':
        import GafferImage
        return GafferImage.ImagePlug.tileSize()
    return 128  # Match the established review/policy pixel order, without importing Gaffer.


def image_tile(node,channel,origin):
    if not isinstance(node,dict):
        import imath
        return node['out'].channelData(channel,imath.V2i(*origin))
    x,y=origin;values=node['channels'][channel];h,w=values.shape
    import numpy as np
    size=image_tile_size();tile=np.zeros((size,size),dtype=values.dtype)
    tile[:min(size,h-y),:min(size,w-x)]=values[y:y+size,x:x+size]
    return tile.ravel()


def read(path):
    import OpenEXR
    return OpenEXR.File(str(path), separate_channels=True)


def bound(image):
    import numpy as np
    header = image.header()
    value = float(np.asarray(header['cycles:maxTransmittanceError']).item())
    if not math.isfinite(value) or not 0 < value <= .01:
        raise ValueError('Invalid EXR transmittance bound')
    if 'cycles:deepError' in header and float(np.asarray(header['cycles:deepError']).item()) != value:
        raise ValueError('Inconsistent EXR error attributes')
    return value


def pixel(image, x, y):
    values = [image.channels()[c].pixels[y, x] for c in ('Z', 'ZBack', 'A')]
    if values[0] is None:
        if any(v is not None and len(v) for v in values):
            raise ValueError('Inconsistent empty deep channels')
        return []
    if len({len(v) for v in values}) != 1:
        raise ValueError('Inconsistent deep channel lengths')
    return list(zip(*(v.astype(float) for v in values)))


def check_deep(image):
    import numpy as np
    import OpenEXR
    if image.header()['type'] != OpenEXR.deepscanline:
        raise ValueError('Expected deep scanline EXR')
    tolerance = bound(image)
    total = 0
    channels = image.channels()
    h, w = channels['A'].pixels.shape
    # Vectorize each pixel's records: no per-record Python objects for large goldens.
    for y in range(h):
        for x in range(w):
            z, back, alpha = [channels[c].pixels[y, x] for c in ('Z', 'ZBack', 'A')]
            if z is None:
                continue
            if not (len(z) == len(back) == len(alpha) and np.all(np.isfinite(z)) and
                    np.all(np.isfinite(back)) and np.all(np.isfinite(alpha)) and
                    np.all(z > 0) and np.all(back >= z) and np.all((alpha >= 0) & (alpha <= 1))):
                raise ValueError(f'Invalid deep pixel ({x},{y})')
            if 'id' in channels and channels['id'].pixels[y,x].dtype != np.uint32:
                raise ValueError('Deep IDs are not UINT')
            total += len(z)
    header = image.header()
    if 'cycles:deepIDHoldoutManifest' in header:
        all_ids = json.loads(header['cycles:deepIDManifest'])
        marked = json.loads(header['cycles:deepIDHoldoutManifest'])
        if not marked or any(all_ids.get(k) != v for k,v in marked.items()):
            raise ValueError('Invalid holdout manifest subset')
    return dict(passed=True,deep_error=tolerance,total_deep_samples=total,width=w,height=h)


def exact_flat(a, b):
    import numpy as np
    a, b = read(a), read(b)
    if set(a.channels()) != set(b.channels()):
        raise ValueError('Beauty channel mismatch')
    for name in a.channels():
        x, y = a.channels()[name].pixels, b.channels()[name].pixels
        if not (x.shape == y.shape and np.all(np.isfinite(x)) and np.all(np.isfinite(y)) and np.array_equal(x,y)):
            raise ValueError('CPU beauty differs: ' + name)
    return dict(passed=True,maximum_error=0,channels=sorted(a.channels()))
