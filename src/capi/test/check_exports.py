"""Check the actual dynamic symbol table, including accidental C++/data exports."""
import re
import subprocess
import sys

EXPECTED = {
    'ccl_mesh_resize',
    'ccl_mesh_resize_subdivision',
    'ccl_hair_resize',
    'ccl_pointcloud_resize',
    'ccl_geometry_set_attribute',
    'ccl_volume_set_grid',

    'ccl_api_version',
    'ccl_has_feature',
    'ccl_init',
    'ccl_last_error',
    'ccl_set_log_callback',
    'ccl_device_count',
    'ccl_device_info',
    'ccl_node_type_count',
    'ccl_node_type_info',
    'ccl_socket_info',
    'ccl_socket_default_value',
    'ccl_socket_enum_value',
    'ccl_session_params_default',
    'ccl_session_create',
    'ccl_shader_graph_create',
    'ccl_shader_graph_destroy',
    'ccl_shader_graph_add',
    'ccl_shader_graph_output',
    'ccl_shader_graph_connect',
    'ccl_shader_graph_disconnect',
    'ccl_shader_set_graph',
    'ccl_shader_graph_add_osl',
    'ccl_node_socket_count',
    'ccl_node_socket_info',
    'ccl_node_socket_find',
    'ccl_session_scene',
    'ccl_session_destroy',
    'ccl_scene_singleton',
    'ccl_node_create',
    'ccl_node_delete',
    'ccl_node_tag_update',
    'ccl_socket_find',
    'ccl_node_set_bool',
    'ccl_node_get_bool',
    'ccl_node_set_bool_id',
    'ccl_node_get_bool_id',
    'ccl_node_set_float',
    'ccl_node_get_float',
    'ccl_node_set_float_id',
    'ccl_node_get_float_id',
    'ccl_node_set_int',
    'ccl_node_get_int',
    'ccl_node_set_int_id',
    'ccl_node_get_int_id',
    'ccl_node_set_uint',
    'ccl_node_get_uint',
    'ccl_node_set_uint_id',
    'ccl_node_get_uint_id',
    'ccl_node_set_uint64',
    'ccl_node_get_uint64',
    'ccl_node_set_uint64_id',
    'ccl_node_get_uint64_id',
    'ccl_node_set_float2',
    'ccl_node_get_float2',
    'ccl_node_set_float2_id',
    'ccl_node_get_float2_id',
    'ccl_node_set_float3',
    'ccl_node_get_float3',
    'ccl_node_set_float3_id',
    'ccl_node_get_float3_id',
    'ccl_node_set_string',
    'ccl_node_get_string',
    'ccl_node_set_string_id',
    'ccl_node_get_string_id',
    'ccl_node_set_enum',
    'ccl_node_get_enum',
    'ccl_node_set_enum_id',
    'ccl_node_get_enum_id',
    'ccl_node_set_transform',
    'ccl_node_get_transform',
    'ccl_node_set_transform_id',
    'ccl_node_get_transform_id',
    'ccl_node_set_node',
    'ccl_node_get_node',
    'ccl_node_set_node_id',
    'ccl_node_get_node_id',
    'ccl_node_set_array',
    'ccl_node_get_array',
    'ccl_node_set_array_id',
    'ccl_node_get_array_id',
}


def exported(text, platform):
    if platform == 'win32':
        return re.findall(r'^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)', text, re.M)
    return [line.split()[-1].removeprefix('_') if platform == 'darwin'
            else line.split()[-1] for line in text.splitlines() if line.strip()]


def check(names):
    assert len(names) == len(EXPECTED) and set(names) == EXPECTED, (
        'Unexpected/missing exports', sorted(set(names) - EXPECTED), sorted(EXPECTED - set(names)))


def self_check():
    sample = '\n'.join(f' {i} {i:X} 00001000 {n}' for i, n in enumerate(sorted(EXPECTED)))
    check(exported(sample, 'win32'))
    for names in [list(EXPECTED) + ['?native@@'], list(EXPECTED)[:-1], list(EXPECTED) + ['ccl_data']]:
        try:
            check(names)
        except AssertionError:
            continue
        raise AssertionError('Export checker accepted a broken table')


if __name__ == '__main__':
    self_check()
    tool, library = sys.argv[1:]
    args = ['/exports'] if sys.platform == 'win32' else (['-gUj'] if sys.platform == 'darwin' else ['-D', '--defined-only', '-j'])
    text = subprocess.check_output([tool, *args, library], text=True)
    names = exported(text, sys.platform)
    check(names)
    print('PASS: exact public C function exports:', ', '.join(sorted(names)))
