/* SPDX-License-Identifier: Apache-2.0 */
/* Included after scene_api.inc; native graph operations stay behind the same lock. */
#include "scene/osl.h"
#include "scene/shader_nodes.h"

namespace {
bool valid(ccl_shader_graph *g)
{
  return graphs.find(g) != graphs.end();
}
void invalidate_graph_nodes(ccl_shader_graph *g)
{
  for (auto &h : g->scene->handles) {
    if (h->graph != g || !h->native)
      continue;
    auto *node = static_cast<ccl::ShaderNode *>(h->native);
    if (node->special_type == ccl::SHADER_SPECIAL_TYPE_OSL)
      for (auto &id : socket_ids)
        if (id.type == node->type)
          id = {nullptr, nullptr};
    g->scene->handles_by_node.erase(h->native);
    live_nodes.erase(h.get());
    h->native = nullptr;
    h->graph = nullptr;
  }
}
void forget_graph(ccl_shader_graph *g)
{
  invalidate_graph_nodes(g);
  graphs.erase(g);
}
ccl_node *graph_node(ccl_shader_graph *g, ccl::ShaderNode *n)
{
  auto *h = wrap_node(g->scene, n);
  h->graph = g;
  return h;
}
template<typename F> ccl_status graph_access(ccl_shader_graph *g, F fn)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(g))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid shader graph");
    return fn();
  });
}
bool endpoint(ccl_shader_graph *g, ccl_node *n)
{
  return valid(n) && n->graph == g;
}
ccl::ShaderInput *graph_input(ccl_node *n, const char *name)
{
  if (name)
    for (auto *s : static_cast<ccl::ShaderNode *>(n->native)->inputs)
      if (!strcmp(text(s->socket_type.name), name))
        return s;
  return nullptr;
}
/* Iterative traversal also bounds work for large host-generated graphs. */
bool upstream(ccl::ShaderNode *from, ccl::ShaderNode *target)
{
  std::vector<ccl::ShaderNode *> todo{from};
  std::unordered_set<ccl::ShaderNode *> seen;
  while (!todo.empty()) {
    auto *n = todo.back();
    todo.pop_back();
    if (n == target)
      return true;
    if (!seen.insert(n).second)
      continue;
    for (auto *s : n->inputs)
      if (s->link)
        todo.push_back(s->link->parent);
  }
  return false;
}
bool convertible(ccl::SocketType::Type type)
{
  using T = ccl::SocketType;
  return type == T::FLOAT || type == T::INT || type == T::COLOR || type == T::VECTOR ||
         type == T::POINT || type == T::NORMAL || type == T::STRING;
}
}  // namespace
ccl_status CCL_CALL ccl_shader_graph_create(ccl_scene *s, ccl_shader_graph **out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(s) || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid scene/graph output");
    auto g = std::make_unique<ccl_shader_graph>();
    g->scene = s;
    g->native = ccl::make_unique<ccl::ShaderGraph>();
    auto *p = g.get();
    graphs.emplace(p, std::move(g));
    *out = p;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_graph_destroy(ccl_shader_graph *g)
{
  return graph_access(g, [&] {
    forget_graph(g);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_graph_add(ccl_shader_graph *g,
                                         const char *type,
                                         const char *name,
                                         ccl_node **out)
{
  return graph_access(g, [&] {
    if (!type || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL type/output");
    const auto *t = ccl::NodeType::find(ccl::ustring(type));
    if (!t)
      return fail(CCL_ERROR_NOT_FOUND, "Unknown shader type");
    if (t->type != ccl::NodeType::SHADER || !t->create || t == ccl::OutputNode::get_node_type())
      return fail(CCL_ERROR_UNSUPPORTED, "Not a creatable shader node; output is borrowed");
    auto *n = g->native->create_node(t);
    n->name = ccl::ustring(name ? name : type);
    *out = graph_node(g, n);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_graph_output(ccl_shader_graph *g, ccl_node **out)
{
  return graph_access(g, [&] {
    if (!out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
    *out = graph_node(g, g->native->output());
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_graph_connect(
    ccl_shader_graph *g, ccl_node *from, const char *output, ccl_node *to, const char *input)
{
  return graph_access(g, [&] {
    if (!endpoint(g, from) || !endpoint(g, to) || !output || !input)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid graph endpoints");
    auto *a = static_cast<ccl::ShaderNode *>(from->native);
    auto *b = static_cast<ccl::ShaderNode *>(to->native);
    ccl::ShaderOutput *o = nullptr;
    for (auto *s : a->outputs)
      if (!strcmp(text(s->socket_type.name), output))
        o = s;
    auto *i = graph_input(to, input);
    if (!o || !i)
      return fail(CCL_ERROR_NOT_FOUND, "Unknown or unlinkable graph socket");
    if (i->link)
      return fail(CCL_ERROR_IN_USE, "Input already linked; disconnect first");
    if (upstream(a, b))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Connection would create a cycle");
    if (o->type() != i->type() &&
        (!convertible(o->type()) ||
         !(convertible(i->type()) || i->type() == ccl::SocketType::CLOSURE)))
      return fail(CCL_ERROR_TYPE_MISMATCH, "Unsupported socket conversion");
    g->native->connect(o, i);
    return i->link ? CCL_SUCCESS : fail(CCL_ERROR_INTERNAL, "Native connection failed");
  });
}
ccl_status CCL_CALL ccl_shader_graph_disconnect(ccl_shader_graph *g,
                                                ccl_node *n,
                                                const char *input)
{
  return graph_access(g, [&] {
    if (!endpoint(g, n) || !input)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid graph endpoint");
    auto *i = graph_input(n, input);
    if (!i)
      return fail(CCL_ERROR_NOT_FOUND, "Unknown or unlinkable input");
    if (i->link)
      g->native->disconnect(i);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_set_graph(ccl_node *shader, ccl_shader_graph *g)
{
  return graph_access(g, [&] {
    if (!valid(shader) || shader->scene != g->scene)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Shader and graph must share a scene");
    if (!shader->native->is_a(ccl::Shader::get_node_type()))
      return fail(CCL_ERROR_TYPE_MISMATCH, "Expected a scene shader");
    auto *s = static_cast<ccl::Shader *>(shader->native);
    /* Native attachment removes proxy nodes; never retain their pointers. */
    invalidate_graph_nodes(g);
    s->set_graph(std::move(g->native));
    s->tag_update(g->scene->native);
    graphs.erase(g);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_shader_graph_add_osl(ccl_shader_graph *g,
                                             const char *path,
                                             const char *name,
                                             ccl_node **out)
{
  return graph_access(g, [&] {
    if (!path || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL OSL path/output");
#ifdef WITH_OSL
    if (!g->scene->native->shader_manager->use_osl())
      return fail(CCL_ERROR_UNSUPPORTED, "OSL session required");
    std::filesystem::path p = std::filesystem::u8path(path);
    if (!p.is_absolute() || (p.extension() != ".osl" && p.extension() != ".oso"))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "OSL requires absolute .osl/.oso path");
    if (!std::filesystem::is_regular_file(p))
      return fail(CCL_ERROR_NOT_FOUND, "OSL file missing");
    auto *n = ccl::OSLShaderManager::osl_node(g->native.get(), g->scene->native, path, "");
    if (!n)
      return fail(CCL_ERROR_INTERNAL, "Native OSL compilation/query failed");
    n->name = ccl::ustring(name ? name : path);
    *out = graph_node(g, n);
    return CCL_SUCCESS;
#else
    (void)name;
    return fail(CCL_ERROR_UNSUPPORTED, "OSL not compiled in");
#endif
  });
}
ccl_status CCL_CALL ccl_node_socket_count(ccl_node *n, ccl_socket_direction d, uint32_t *out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n) || !out || (d != CCL_SOCKET_INPUT && d != CCL_SOCKET_OUTPUT))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node/socket direction/output");
    *out = uint32_t(d == CCL_SOCKET_INPUT ? n->native->type->inputs.size() :
                                            n->native->type->outputs.size());
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_node_socket_info(ccl_node *n,
                                         ccl_socket_direction d,
                                         uint32_t index,
                                         ccl_socket_desc *out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n) || !out || out->size < sizeof(*out) ||
        (d != CCL_SOCKET_INPUT && d != CCL_SOCKET_OUTPUT))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node/socket description");
    const auto &v = d == CCL_SOCKET_INPUT ? n->native->type->inputs : n->native->type->outputs;
    if (index >= v.size())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Socket index out of range");
    *out = describe_socket(&v[index], out->size);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_node_socket_find(ccl_node *n, const char *name, ccl_socket_id *out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n) || !name || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node/socket lookup");
    const auto *s = resolve(n, name);
    if (!s)
      return CCL_ERROR_NOT_FOUND;
    for (size_t i = 0; i < socket_ids.size(); ++i)
      if (socket_ids[i].type == n->native->type && socket_ids[i].socket == s) {
        *out = i + 1;
        return CCL_SUCCESS;
      }
    socket_ids.push_back({n->native->type, s});
    *out = socket_ids.size();
    return CCL_SUCCESS;
  });
}
