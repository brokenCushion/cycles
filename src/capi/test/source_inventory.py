"""Independent NODE_DEFINE inventory versus the loaded C DLL, not a linked replica."""
import ctypes as C
import json
import pathlib
import re
import sys


def definitions(text, macro):
    # Mask comments but retain string spelling; use a second mask for brace counting.
    text = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), text, flags=re.S)
    text = re.sub(r'^[ \t]*#[ \t]*define[^\n]*', lambda m: ' ' * len(m[0]), text, flags=re.M)
    braces = re.sub(r'"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), text)
    rows = []
    for match in re.finditer(r'\b' + macro + r'\s*\(\s*(\w+)\s*\)\s*\{', text):
        end = match.end()
        depth = 1
        while depth and end < len(text):
            depth += (braces[end] == '{') - (braces[end] == '}')
            end += 1
        assert depth == 0, 'Unbalanced registration body'
        body = text[match.end():end]
        calls = re.findall(r'NodeType::add\s*\(\s*"([^"\n]+)"', body)
        assert len(calls) == 1, ('Unrecognized registration; update inventory parser', match[1])
        rows.append({'class': match[1], 'name': calls[0]})
    assert len(re.findall(r'\b' + macro + r'\s*\(', text)) == len(rows), 'Unparsed registration'
    return rows


def inventory(source):
    regular, abstract = [], []
    for path in sorted(source.rglob('*')):
        if path.suffix not in {'.cpp', '.cc', '.cxx', '.h', '.hpp'}:
            continue
        if 'capi' in path.relative_to(source).parts:
            continue
        text = path.read_text(encoding='utf-8')
        for macro, target in [('NODE_DEFINE', regular), ('NODE_ABSTRACT_DEFINE', abstract)]:
            target.extend(dict(row, file=path.relative_to(source).as_posix())
                          for row in definitions(text, macro))
    text = (source/'scene/shader_nodes.cpp').read_text(encoding='utf-8')
    match = re.search(r'const SocketType::Type types\[num_types\] = \{([^}]+)\}', text)
    assert match and 'ConvertNode::register_on_init = NodeType::register_on_init(' in text
    types = re.findall(r'SocketType::(\w+)', match[1])
    assert types and len(types) == len(set(types))
    assert '"convert_" + from_name.string() + "_to_" + to_name.string()' in text
    generated = ['convert_' + a.lower() + '_to_' + b.lower() for a in types for b in types]
    names = [x['name'] for x in regular] + generated
    assert len(names) == len(set(names)), 'Duplicate source registration'
    return regular, abstract, generated


class Init(C.Structure):
    _fields_ = [('size', C.c_uint32), ('resource_path', C.c_char_p), ('user_path', C.c_char_p)]


class Node(C.Structure):
    _fields_ = [('size', C.c_uint32), ('name', C.c_char_p), ('base', C.c_char_p),
                ('is_shader', C.c_int32), ('is_abstract', C.c_int32),
                ('input_count', C.c_uint32), ('output_count', C.c_uint32)]


def check(expected, optional, actual):
    missing = sorted(expected - actual)
    extra = sorted(actual - expected - optional)
    assert not missing and not extra, {'missing': missing, 'unexpected': extra}


def self_check():
    assert definitions('/* NODE_DEFINE(Fake) {} */ NODE_DEFINE(A) { NodeType *t = NodeType::add("a", create); return t; }', 'NODE_DEFINE')[0]['name'] == 'a'
    assert definitions('#define NODE_DEFINE(x) ignored\nNODE_DEFINE(A) { NodeType::add("a", create); }', 'NODE_DEFINE')[0]['name'] == 'a'
    for source in ['NODE_DEFINE(A)', 'NODE_DEFINE(A) { NodeType::add(variable, create); }']:
        try:
            definitions(source, 'NODE_DEFINE')
        except AssertionError:
            continue
        raise AssertionError('Unrecognized registration was silently skipped')
    for actual in [{'a'}, {'a', 'b', 'bad'}]:
        try:
            check({'a', 'b'}, set(), actual)
        except AssertionError:
            continue
        raise AssertionError('Inventory test missed dropped or unexpected registration')
    check({'a', 'b'}, {'base'}, {'a', 'b', 'base'})


if __name__ == '__main__':
    self_check()
    source, library, output = map(pathlib.Path, sys.argv[1:])
    regular, abstract, generated = inventory(source)
    dll = C.CDLL(str(library.resolve()), **({"winmode": 8} if sys.platform == "win32" else {}))
    dll.ccl_init.argtypes = [C.POINTER(Init)]
    dll.ccl_node_type_count.argtypes = [C.POINTER(C.c_uint32)]
    dll.ccl_node_type_info.argtypes = [C.c_uint32, C.POINTER(Node)]
    init = Init(C.sizeof(Init), str(library.parent.resolve()).encode(), str(output.parent.resolve()).encode())
    assert dll.ccl_init(C.byref(init)) == 0
    count = C.c_uint32()
    assert dll.ccl_node_type_count(C.byref(count)) == 0
    rows = []
    for i in range(count.value):
        n = Node(); n.size = C.sizeof(n)
        assert dll.ccl_node_type_info(i, C.byref(n)) == 0
        rows.append({'name': n.name.decode(), 'base': n.base.decode(), 'abstract': bool(n.is_abstract),
                     'inputs': n.input_count, 'outputs': n.output_count})
    actual = {x['name'] for x in rows}
    expected = {x['name'] for x in regular} | set(generated)
    optional = {x['name'] for x in abstract}
    report = dict(source_definitions=regular, abstract_definitions=abstract,
                  generated_types=generated, dll_types=rows,
                  missing=sorted(expected-actual), unexpected=sorted(actual-expected-optional),
                  lazy_abstract_not_registered=sorted(optional-actual))
    output.write_text(json.dumps(report, indent=2)+'\n')
    assert len(actual) == len(rows) and [x['name'] for x in rows] == sorted(actual)
    check(expected, optional, actual)
    print(f'PASS source inventory: {len(regular)} NODE_DEFINE, {len(generated)} generated; {len(actual)} DLL types')
