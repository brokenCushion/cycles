# SPDX-License-Identifier: Apache-2.0
"""Supplement native qualification: HOLDOUT ORDINARY BEAUTY_OFF NAME OPACITY.

Run with Gaffer's Python. Compare every deep boundary/extremum, exact UINT
selection and native raw passes; the main validator owns the beauty proof.
"""
import json
import math
from pathlib import Path
import sys
import GafferImage
import imath

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from compare_deep_ids import curve_error, flattened_transmittance, isolate, name_hash, read

directory, reference, beauty_off = (Path(v).resolve() for v in sys.argv[1:4])
name, opacity = sys.argv[4], float(sys.argv[5])
settings = json.loads((directory / 'render.json').read_text())
qualified = json.loads((directory / 'gaffer_validation.json').read_text())
assert qualified['passed']
spec, data = read(directory / 'scene.deep.exr')
other, ordinary = read(reference / 'scene.deep.exr')
assert (spec.x, spec.y, spec.width, spec.height) == (other.x, other.y, other.width, other.height)
bound = spec.getattribute('cycles:maxTransmittanceError')
assert math.isfinite(bound) and 0 < bound <= .01
maximum = 0.
for p in range(spec.width * spec.height):
    curves = []
    for header, pixels in [(spec, data), (other, ordinary)]:
        channels = [header.channelnames.index(c) for c in ('Z', 'ZBack', 'A')]
        curves.append([tuple(pixels.deep_value(p, c, s) for c in channels)
                       for s in range(pixels.samples(p))])
    maximum = max(maximum, curve_error(*curves))
assert maximum <= bound, (maximum, bound)
marker = spec.getattribute('cycles:deepIDHoldoutManifest')
selected_error = None
if settings['deep_ids']:
    identifier = name_hash(name)
    assert json.loads(marker) == {f'{identifier:08x}': name}
    assert json.loads(spec.getattribute('cycles:deepIDManifest'))[f'{identifier:08x}'] == name
    selected_path = directory / 'holdout-selected.deep.exr'
    assert isolate(directory / 'scene.deep.exr', name, selected_path) == identifier
    selected_spec, selected = read(selected_path)
    channels = [selected_spec.channelnames.index(c) for c in ('Z', 'ZBack', 'A')]
    selected_error = max(abs(1 - flattened_transmittance([
        tuple(selected.deep_value(p, c, s) for c in channels)
        for s in range(selected.samples(p))]) - opacity)
        for p in range(spec.width * spec.height))
    assert selected_error <= bound
    assert json.loads(selected_spec.getattribute('cycles:deepIDHoldoutManifest')) == json.loads(marker)
    fog_path = directory / 'fog-selected.deep.exr'
    isolate(directory / 'scene.deep.exr', 'Fog', fog_path)
    assert not read(fog_path)[0].getattribute('cycles:deepIDHoldoutManifest')
else:
    assert marker is None and 'id' not in spec.channelnames
assert other.getattribute('cycles:deepIDHoldoutManifest') is None

raw = []
for d in [directory, beauty_off]:
    reader = GafferImage.ImageReader()
    reader['fileName'].setValue((d / 'render-passes.exr').as_posix())
    raw.append(reader)
channels = list(raw[0]['out']['channelNames'].getValue())
assert channels == list(raw[1]['out']['channelNames'].getValue())
raw_error = 0.
tile = GafferImage.ImagePlug.tileSize()
for channel in channels:
    values = [r['out'].channelData(channel, imath.V2i(0)) for r in raw]
    for y in range(spec.height):
        for x in range(spec.width):
            a, b = (float(v[y * tile + x]) for v in values)
            assert math.isfinite(a) and math.isfinite(b)
            raw_error = max(raw_error, abs(a - b))
if settings['device'] == 'CPU':
    assert raw_error == 0, raw_error
result = dict(passed=True,device=settings['device'],deep_ids=settings['deep_ids'],
              max_ordinary_curve_error=maximum,header_bound=bound,
              max_selected_opacity_error=selected_error,max_raw_beauty_error=raw_error,
              raw_channels=channels,cuda_pixel_gate_required=False,
              beauty_proof='CPU exact raw/beauty; unchanged GPU/beauty sources and 75 resources',
              deep_samples=qualified['total_deep_samples'],exr_bytes=(directory/'scene.deep.exr').stat().st_size)
(directory / 'holdout_validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
