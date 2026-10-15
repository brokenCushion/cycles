/* SPDX-License-Identifier: Apache-2.0 */
/* Native XML loader is the independent reference; no renderer implementation edits. */
#include "../cycles_c.cpp"
#include "app/cycles_xml.cpp"
#include <fstream>
#include <sstream>
#undef CHECK
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      abort(); \
    } \
  } while (0)
#include "session_helpers.h"

static void attributes(ccl_node *n, ccl::xml_node xml)
{
  for (auto a : xml.attributes()) {
    if (!strcmp(a.name(), "name") || !strcmp(a.name(), "src"))
      continue;
    const ccl::SocketType *s = nullptr;
    for (const auto &candidate : n->native->type->inputs)
      if (ccl::string_iequals(candidate.name.string(), a.name()))
        s = &candidate;
    /* The native XML reader ignores legacy/unknown attributes (e.g. math type). */
    if (!s)
      continue;
    const char *key = text(s->name);
    std::string value = a.value();
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream input(value);
    if (s->type == ccl::SocketType::FLOAT) {
      float f;
      CHECK(bool(input >> f));
      CHECK(ccl_node_set_float(n, key, f) == CCL_SUCCESS);
    }
    else if (s->type == ccl::SocketType::COLOR || s->type == ccl::SocketType::VECTOR ||
             s->type == ccl::SocketType::POINT || s->type == ccl::SocketType::NORMAL)
    {
      ccl_float3 f;
      CHECK(bool(input >> f.x >> f.y >> f.z));
      CHECK(ccl_node_set_float3(n, key, f) == CCL_SUCCESS);
    }
    else if (s->type == ccl::SocketType::ENUM) {
      CHECK(s->enum_values && s->enum_values->exists(ccl::ustring(value)));
      CHECK(ccl_node_set_enum(n, key, (*s->enum_values)[ccl::ustring(value)]) == CCL_SUCCESS);
    }
    else {
      fprintf(stderr, "Unhandled fixture attribute %s\n", key);
      abort();
    }
  }
}
static void equal_graphs(ccl::ShaderGraph *a, ccl::ShaderGraph *b)
{
  CHECK(a->nodes.size() == b->nodes.size());
  for (size_t i = 0; i < a->nodes.size(); ++i) {
    auto *x = a->nodes[i], *y = b->nodes[i];
    CHECK(x->type->name == y->type->name);
    CHECK(x->type->inputs.size() == y->type->inputs.size());
    for (size_t j = 0; j < x->type->inputs.size(); ++j) {
      const auto &s = x->type->inputs[j], &t = y->type->inputs[j];
      CHECK(s.name == t.name && s.type == t.type);
      if (s.type != ccl::SocketType::CLOSURE)
        CHECK(x->equals_value(*y, s));
    }
    CHECK(x->inputs.size() == y->inputs.size() && x->outputs.size() == y->outputs.size());
    for (size_t j = 0; j < x->outputs.size(); ++j)
      CHECK(x->outputs[j]->socket_type.name == y->outputs[j]->socket_type.name &&
            x->outputs[j]->type() == y->outputs[j]->type());
    for (size_t j = 0; j < x->inputs.size(); ++j) {
      auto *p = x->inputs[j]->link, *q = y->inputs[j]->link;
      CHECK(bool(p) == bool(q));
      if (p)
        CHECK(p->parent->id == q->parent->id && p->socket_type.name == q->socket_type.name);
    }
  }
}
static std::string canonical(ccl_node *n,
                             ccl_socket_direction direction,
                             const std::string &xml_name)
{
  uint32_t count;
  CHECK(ccl_node_socket_count(n, direction, &count) == CCL_SUCCESS);
  for (uint32_t i = 0; i < count; ++i) {
    ccl_socket_desc d{};
    d.size = sizeof(d);
    CHECK(ccl_node_socket_info(n, direction, i, &d) == CCL_SUCCESS);
    if (ccl::string_iequals(d.name, xml_name))
      return d.name;
  }
  CHECK(false);
  return "";
}
static int xml_fixture(ccl_scene *scene, const std::filesystem::path &path)
{
  ccl::xml_document doc;
  CHECK(doc.load_file(path.string().c_str()));
  /* Legacy examples use comma-separated volume colors. The current XML volume
   * validator requires whitespace; normalize separators in the test DOM only. */
  for (auto graph : doc.child("cycles").children())
    for (auto node : graph.children())
      for (auto a : node.attributes()) {
        if (!strcmp(a.name(), "color") || !strcmp(a.name(), "color1") ||
            !strcmp(a.name(), "color2"))
        {
          std::string value = a.value();
          std::replace(value.begin(), value.end(), ',', ' ');
          a.set_value(value.c_str());
        }
      }
  int checked = 0;
  for (auto xml : doc.child("cycles").children()) {
    if (strcmp(xml.name(), "shader") && strcmp(xml.name(), "background"))
      continue;
    auto *reference = scene->native->create_node<ccl::Shader>();
    ccl::XMLReadState state;
    state.scene = scene->native;
    state.base = path.parent_path().string();
    ccl::xml_read_shader_graph(state, reference, xml);
    ccl_shader_graph *g;
    CHECK(ccl_shader_graph_create(scene, &g) == CCL_SUCCESS);
    std::unordered_map<std::string, ccl_node *> nodes;
    CHECK(ccl_shader_graph_output(g, &nodes["output"]) == CCL_SUCCESS);
    for (auto item : xml.children()) {
      std::string type = item.name();
      if (type.empty())
        continue;
      if (type == "connect") {
        std::istringstream f(item.attribute("from").value()), t(item.attribute("to").value());
        std::string fn, fs, tn, ts;
        CHECK(bool(f >> fn >> fs) && bool(t >> tn >> ts));
        CHECK(nodes.count(fn) && nodes.count(tn));
        fs = canonical(nodes[fn], CCL_SOCKET_OUTPUT, fs);
        ts = canonical(nodes[tn], CCL_SOCKET_INPUT, ts);
        CHECK(ccl_shader_graph_connect(g, nodes[fn], fs.c_str(), nodes[tn], ts.c_str()) ==
              CCL_SUCCESS);
      }
      else {
        ccl_node *n;
        const char *name = item.attribute("name").value();
        if (type == "osl_shader") {
          auto file = path.parent_path() / item.attribute("src").value();
          CHECK(ccl_shader_graph_add_osl(g, file.string().c_str(), name, &n) == CCL_SUCCESS);
        }
        else {
          if (type == "background")
            type = "background_shader";
          CHECK(ccl_shader_graph_add(g, type.c_str(), name, &n) == CCL_SUCCESS);
        }
        attributes(n, item);
        nodes.emplace(name, n);
      }
    }
    ccl_node *shader;
    CHECK(ccl_node_create(scene, "shader", "fixture", &shader) == CCL_SUCCESS);
    CHECK(ccl_shader_set_graph(shader, g) == CCL_SUCCESS);
    equal_graphs(static_cast<ccl::Shader *>(shader->native)->graph.get(), reference->graph.get());
    CHECK(ccl_node_delete(scene, shader) == CCL_SUCCESS);
    ++checked;
  }
  printf(
      "PASS XML graph equivalence: %s (%d graphs)\n", path.filename().string().c_str(), checked);
  return checked;
}
int main(int argc, char **argv)
{
  CHECK(argc == 4 || argc == 5);
  setvbuf(stdout, nullptr, _IONBF, 0);
  CHECK(ccl_set_log_callback(
            [](ccl_log_level, const char *m, void *) { fprintf(stderr, "%s\n", m); }, nullptr) ==
        CCL_SUCCESS);
  ccl_init_params init{sizeof(init), argv[1], argv[3]};
  CHECK(ccl_init(&init) == CCL_SUCCESS);
  ccl_session_params p{};
  p.size = sizeof(p);
  CHECK(ccl_session_params_default(&p) == CCL_SUCCESS);
  p.device_index = test_device(argc == 5 ? argv[4] : "CPU");
  p.use_osl = 1;
  ccl_session *session;
  CHECK(ccl_session_create(&p, &session) == CCL_SUCCESS);
  ccl_scene *scene;
  CHECK(ccl_session_scene(session, &scene) == CCL_SUCCESS);
  auto examples = std::filesystem::path(argv[2]);
  auto temp = std::filesystem::path(argv[3]);
  std::filesystem::create_directories(temp / "osl");
  std::filesystem::copy_file(examples / "osl/stripes.osl",
                             temp / "osl/stripes.osl",
                             std::filesystem::copy_options::overwrite_existing);
  int total = 0;
  for (auto file : {"scene_cube_surface.xml",
                    "scene_cube_volume.xml",
                    "scene_world_volume.xml",
                    "scene_sphere_bump.xml",
                    "scene_caustics.xml",
                    "scene_osl_stripes.xml"})
  {
    std::filesystem::copy_file(
        examples / file, temp / file, std::filesystem::copy_options::overwrite_existing);
    total += xml_fixture(scene, temp / file);
  }
  CHECK(total == 16);
  ccl_shader_graph *g, *other;
  CHECK(ccl_shader_graph_create(scene, &g) == CCL_SUCCESS);
  CHECK(ccl_shader_graph_create(scene, &other) == CCL_SUCCESS);
  ccl_node *osl, *second;
  auto path = (temp / "osl/stripes.osl").string();
  CHECK(ccl_shader_graph_add_osl(g, path.c_str(), "first", &osl) == CCL_SUCCESS);
  CHECK(ccl_shader_graph_add_osl(other, path.c_str(), "second", &second) == CCL_SUCCESS);
  ccl_socket_id id;
  CHECK(ccl_node_socket_find(osl, "Scale", &id) == CCL_SUCCESS);
  float v;
  CHECK(ccl_node_get_float_id(osl, id, &v) == CCL_SUCCESS && v == 0.5f);
  CHECK(ccl_node_set_float_id(osl, id, 2.0f) == CCL_SUCCESS);
  CHECK(ccl_node_get_float(osl, "Scale", &v) == CCL_SUCCESS && v == 2.0f);
  CHECK(ccl_node_set_float_id(second, id, 1.0f) == CCL_ERROR_INVALID_ARGUMENT);
  uint32_t count;
  CHECK(ccl_node_socket_count(osl, CCL_SOCKET_INPUT, &count) == CCL_SUCCESS && count == 6);
  ccl_socket_desc desc{};
  desc.size = sizeof(desc);
  CHECK(ccl_node_socket_info(osl, CCL_SOCKET_OUTPUT, 0, &desc) == CCL_SUCCESS &&
        desc.type == CCL_SOCKET_CLOSURE);
  CHECK(ccl_shader_graph_add_osl(g, "relative.osl", nullptr, &second) ==
        CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_shader_graph_add_osl(g, (temp / "absent.osl").string().c_str(), nullptr, &second) ==
        CCL_ERROR_NOT_FOUND);
  std::ofstream(temp / "broken.osl") << "this is invalid OSL";
  CHECK(ccl_shader_graph_add_osl(g, (temp / "broken.osl").string().c_str(), nullptr, &second) ==
        CCL_ERROR_INTERNAL);
  CHECK(ccl_shader_graph_destroy(g) == CCL_SUCCESS);
  CHECK(socket_ids[id - 1].type == nullptr);
  CHECK(ccl_node_get_float_id(osl, id, &v) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_session_destroy(session) == CCL_SUCCESS);
  CHECK(graphs.empty() && live_nodes.empty() && scenes.empty() && sessions.empty());
  printf(
      "PASS %d native XML graphs, OSL loading/reflection/ID lifetime/errors, session graph "
      "cleanup\n",
      total);
}
