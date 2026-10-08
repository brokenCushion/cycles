"""Guarded deep changes may differ; unguarded beauty changes must not disappear."""
from record_beauty_build import without_deep_blocks

base = b'beauty();\n#ifdef WITH_CYCLES_DEEP_OPAQUE\ndeep();\n#if NESTED\na();\n#endif\n#endif\nbeauty2();\n'
assert without_deep_blocks(base) == b'beauty();\nbeauty2();\n'
assert without_deep_blocks(base.replace(b'deep();', b'other_deep();')) == without_deep_blocks(base)
assert without_deep_blocks(base.replace(b'beauty();', b'changed_beauty();')) != without_deep_blocks(base)
print('PASS beauty source boundary')

# Shared queue additions and the deep-only header list cannot hide build flags.
from record_beauty_build import beauty_identity, git
import hashlib, re
source = git('show', 'HEAD:src/kernel/CMakeLists.txt')
normalized = re.sub(rb'(?ms)^set\(SRC_KERNEL_DEEP_HEADERS\n.*?^\)\n', b'', source)
assert b'deep/write.h' not in normalized
assert b'set(SRC_KERNEL_DEVICE_GPU_HEADERS' in normalized
assert hashlib.sha256(normalized).hexdigest() == beauty_identity('HEAD')[1]['src/kernel/CMakeLists.txt']
print('PASS deep-only kernel header list; other build contents retained')

# New OSL metadata is guarded; Session excludes only its exact validation hook.
for name in ('src/scene/osl.cpp', 'src/scene/osl.h', 'src/scene/shader.h', 'src/session/session.cpp'):
    assert beauty_identity('67faae68c')[1][name] == beauty_identity('HEAD')[1][name], name
session = git('show', 'HEAD:src/session/session.cpp')
assert b'validate_deep_osl(scene.get());' in session
assert b'scene->device_update' in session
print('PASS OSL metadata and exact Session validation boundary')
