"""Guarded deep changes may differ; unguarded beauty changes must not disappear."""
from record_beauty_build import without_deep_blocks

base = b'beauty();\n#ifdef WITH_CYCLES_DEEP_OPAQUE\ndeep();\n#if NESTED\na();\n#endif\n#endif\nbeauty2();\n'
assert without_deep_blocks(base) == b'beauty();\nbeauty2();\n'
assert without_deep_blocks(base.replace(b'deep();', b'other_deep();')) == without_deep_blocks(base)
assert without_deep_blocks(base.replace(b'beauty();', b'changed_beauty();')) != without_deep_blocks(base)
print('PASS beauty source boundary')
