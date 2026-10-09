# SPDX-License-Identifier: Apache-2.0
"""Add rigid shutter motion to the existing three-primitives XML fixture.

Usage: python create_motion_primitives.py INPUT_XML OUTPUT_XML
Then run create_cuda_review.py with the resulting XML in Gaffer Python.
"""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
root.find('integrator').set('motion_blur', 'true')
root.find('camera').set('shuttertime', '1')

def transform(x, z):
    return f'1 0 0 {x} 0 1 0 0 0 0 1 {z}'

# Absolute instance transforms; original mesh coordinates are already baked.
motions = [((-0.6, -0.3), (0.6, 0.3)),
           ((0.4, 0.4), (-0.4, -0.4)),
           ((-0.2, -0.5), (0.2, 0.5))]
states = [node for node in root.findall('state') if node.find('mesh') is not None]
assert len(states) == 3, 'Expected sphere, cube and cylinder'
for i, (state, endpoints) in enumerate(zip(states, motions)):
    name = f'moving_primitive_{i}'
    node = ET.Element('object', name=name,
                      motion=' '.join(transform(*p) for p in endpoints))
    root.insert(list(root).index(state), node)
    state.set('object', name)
target = Path(sys.argv[2]); target.parent.mkdir(parents=True, exist_ok=True)
ET.ElementTree(root).write(target, encoding='unicode')
print(target)
