/* SPDX-License-Identifier: Apache-2.0 */
/* The reference uses native XML/Session; the subject uses the DLL's public header only. */
#include "../cycles_c.h"
#include "app/cycles_xml.cpp"
#include "scene/pass.h"
#include "session/output_driver.h"
#include "session/session.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>
#undef CHECK
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      abort(); \
    } \
  } while (0)
#include "session_helpers.h"
#define OK(x) CHECK((x) == CCL_SUCCESS)
using Map = std::map<const ccl::Node *, ccl_node *>;
static void values(const ccl::Node &n, ccl_node *out, const Map &nodes)
{
  using T = ccl::SocketType;
  for (const auto &s : n.type->inputs) {
    if (n.has_default_value(s) || s.type == T::CLOSURE || s.type == T::UNDEFINED)
      continue;
    const char *key = s.name.c_str();
    switch (s.type) {
      case T::BOOLEAN:
        OK(ccl_node_set_bool(out, key, n.get_bool(s)));
        break;
      case T::INT:
        OK(ccl_node_set_int(out, key, n.get_int(s)));
        break;
      case T::ENUM:
        OK(ccl_node_set_enum(out, key, n.get_int(s)));
        break;
      case T::UINT:
        OK(ccl_node_set_uint(out, key, n.get_uint(s)));
        break;
      case T::UINT64:
        OK(ccl_node_set_uint64(out, key, n.get_uint64(s)));
        break;
      case T::FLOAT:
        OK(ccl_node_set_float(out, key, n.get_float(s)));
        break;
      case T::POINT2: {
        auto v = n.get_float2(s);
        OK(ccl_node_set_float2(out, key, {v.x, v.y}));
        break;
      }
      case T::COLOR:
      case T::VECTOR:
      case T::POINT:
      case T::NORMAL: {
        auto v = n.get_float3(s);
        OK(ccl_node_set_float3(out, key, {v.x, v.y, v.z}));
        break;
      }
      case T::STRING:
        OK(ccl_node_set_string(out, key, n.get_string(s).c_str()));
        break;
      case T::TRANSFORM: {
        auto v = n.get_transform(s);
        ccl_transform t{
            {v.x.x, v.x.y, v.x.z, v.x.w, v.y.x, v.y.y, v.y.z, v.y.w, v.z.x, v.z.y, v.z.z, v.z.w}};
        OK(ccl_node_set_transform(out, key, t));
        break;
      }
      case T::NODE: {
        auto *v = n.get_node(s);
        CHECK(!v || nodes.count(v));
        OK(ccl_node_set_node(out, key, v ? nodes.at(v) : nullptr));
        break;
      }
      case T::INT_ARRAY: {
        auto &v = n.get_int_array(s);
        OK(ccl_node_set_array(out, key, CCL_SOCKET_INT_ARRAY, v.data(), v.size()));
        break;
      }
      case T::FLOAT_ARRAY: {
        auto &v = n.get_float_array(s);
        OK(ccl_node_set_array(out, key, CCL_SOCKET_FLOAT_ARRAY, v.data(), v.size()));
        break;
      }
      case T::BOOLEAN_ARRAY: {
        std::vector<int32_t> v;
        for (bool b : n.get_bool_array(s))
          v.push_back(b);
        OK(ccl_node_set_array(out, key, CCL_SOCKET_BOOLEAN_ARRAY, v.data(), v.size()));
        break;
      }
      case T::NODE_ARRAY: {
        std::vector<ccl_node *> v;
        for (auto *p : n.get_node_array(s)) {
          CHECK(nodes.count(p));
          v.push_back(nodes.at(p));
        }
        OK(ccl_node_set_array(out, key, CCL_SOCKET_NODE_ARRAY, v.data(), v.size()));
        break;
      }
      default:
        fprintf(stderr,
                "Unhandled nondefault fixture socket %s.%s type %d\n",
                n.type->name.c_str(),
                key,
                int(s.type));
        CHECK(false);
    }
  }
}
static void clone_scene(ccl::Scene &ref, ccl_scene *out)
{
  Map nodes;
  for (auto *n : {static_cast<ccl::Node *>(ref.camera),
                  static_cast<ccl::Node *>(ref.film),
                  static_cast<ccl::Node *>(ref.integrator),
                  static_cast<ccl::Node *>(ref.background)})
    OK(ccl_scene_singleton(out, n->type->name.c_str(), &nodes[n]));
  for (auto *n : ref.shaders)
    OK(ccl_node_create(out, "shader", n->name.c_str(), &nodes[n]));
  for (auto *n : ref.geometry) {
    CHECK(n->is_mesh());
    auto *mesh = static_cast<ccl::Mesh *>(n);
    OK(ccl_node_create(out, "mesh", n->name.c_str(), &nodes[n]));
    OK(ccl_mesh_resize(nodes[n], mesh->num_verts(), mesh->num_triangles()));
    for (const auto &a : mesh->attributes.attributes) {
      CHECK(a.std == ccl::ATTR_STD_POSITION || a.std == ccl::ATTR_STD_GENERATED);
      ccl_attribute_desc d{sizeof(d),
                           nullptr,
                           a.std == ccl::ATTR_STD_POSITION ? "POSITION" : "GENERATED",
                           CCL_ATTRIBUTE_POINT,
                           CCL_ATTRIBUTE_VERTEX,
                           0,
                           0};
      std::vector<ccl_float3> p;
      for (int i = 0; i < a.size; ++i) {
        auto v = a.data<ccl::packed_float3>()[i];
        p.push_back({v.x, v.y, v.z});
      }
      OK(ccl_geometry_set_attribute(nodes[n], &d, 0, p.data(), p.size()));
    }
  }
  for (auto *n : ref.objects)
    OK(ccl_node_create(out, "object", n->name.c_str(), &nodes[n]));
  for (auto *n : ref.passes)
    OK(ccl_node_create(out, "pass", n->get_name().c_str(), &nodes[n]));
  for (auto *shader : ref.shaders) {
    if (!shader->graph)
      continue;
    ccl_shader_graph *g;
    OK(ccl_shader_graph_create(out, &g));
    Map shader_nodes;
    for (auto *n : shader->graph->nodes) {
      if (n == shader->graph->output())
        OK(ccl_shader_graph_output(g, &shader_nodes[n]));
      else if (n->special_type == ccl::SHADER_SPECIAL_TYPE_OSL) {
        auto *osl = static_cast<ccl::OSLNode *>(n);
        OK(ccl_shader_graph_add_osl(g, osl->filepath.c_str(), n->name.c_str(), &shader_nodes[n]));
      }
      else
        OK(ccl_shader_graph_add(g, n->type->name.c_str(), n->name.c_str(), &shader_nodes[n]));
    }
    for (auto *n : shader->graph->nodes) {
      values(*n, shader_nodes.at(n), nodes);
      for (auto *input : n->inputs)
        if (input->link)
          OK(ccl_shader_graph_connect(g,
                                      shader_nodes.at(input->link->parent),
                                      input->link->socket_type.name.c_str(),
                                      shader_nodes.at(n),
                                      input->socket_type.name.c_str()));
    }
    OK(ccl_shader_set_graph(nodes.at(shader), g));
  }
  for (auto &[n, h] : nodes) {
    values(*n, h, nodes);
    OK(ccl_node_tag_update(h));
  }
  // XML edits the native fallback shader; an external host binds its cloned world explicitly.
  if (!ref.background->get_shader())
    OK(ccl_node_set_node(nodes.at(ref.background), "shader", nodes.at(ref.default_background)));
}
struct Pixels {
  std::map<std::string, std::vector<float>> passes;
  int width = 47, height = 20;
  std::atomic<int> updates{0}, finals{0}, progress{0}, reentries{0};
  bool fail_output = false;
  std::mutex mutex;
  void store(const char *name, int x, int y, int w, int h, int channels, const float *p)
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto &v = passes[name];
    v.resize(size_t(width) * height * channels);
    CHECK(x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    for (int row = 0; row < h; ++row)
      memcpy(v.data() + ((y + row) * width + x) * channels,
             p + row * w * channels,
             size_t(w) * channels * sizeof(float));
  }
};
static const ccl_output_pass requests[] = {{sizeof(ccl_output_pass), "Combined", 4},
                                           {sizeof(ccl_output_pass), "Depth", 1},
                                           {sizeof(ccl_output_pass), "Normal", 3},
                                           {sizeof(ccl_output_pass), "Sample Count", 1}};
static int32_t CCL_CALL output(const ccl_render_tile *t, void *user)
{
  auto &p = *static_cast<Pixels *>(user);
  uint32_t count;
  CHECK(ccl_device_count(&count) == CCL_ERROR_CALLBACK_REENTRY);
  ++p.reentries;
  CHECK(t->size == sizeof(*t) && t->float_count == uint64_t(t->width) * t->height * t->channels);
  if (p.fail_output)
    return 1;
  p.store(t->pass_name, t->x, t->y, t->width, t->height, t->channels, t->pixels);
  if (t->final)
    ++p.finals;
  else
    ++p.updates;
  return 0;
}
static void CCL_CALL progress(const ccl_progress *p, void *user)
{
  auto &v = *static_cast<Pixels *>(user);
  ++v.progress;
  CHECK(p->size == sizeof(*p));
  uint32_t count;
  CHECK(ccl_device_count(&count) == CCL_ERROR_CALLBACK_REENTRY);
  ++v.reentries;
}
class NativeOutput : public ccl::OutputDriver {
  Pixels &p;

 public:
  NativeOutput(Pixels &p) : p(p) {}
  void write_render_tile(const Tile &t) override
  {
    for (auto &r : requests) {
      std::vector<float> v(size_t(t.size.x) * t.size.y * r.channels);
      CHECK(t.get_pass_pixels(r.name, r.channels, v.data()));
      p.store(r.name, t.offset.x, t.offset.y, t.size.x, t.size.y, r.channels, v.data());
      ++p.finals;
    }
  }
};
static void normalized(const std::filesystem::path &input, const std::filesystem::path &output)
{
  ccl::xml_document doc;
  CHECK(doc.load_file(input.string().c_str()));
  std::function<void(ccl::xml_node)> walk = [&](ccl::xml_node node) {
    for (auto a : node.attributes()) {
      std::string v = a.value();
      if (!strcmp(a.name(), "color") || !strcmp(a.name(), "color1") || !strcmp(a.name(), "color2"))
      {
        std::replace(v.begin(), v.end(), ',', ' ');
        a.set_value(v.c_str());
      }
    }
    for (auto child : node.children())
      walk(child);
  };
  walk(doc);
  CHECK(doc.save_file(output.string().c_str()));
}
static ccl_buffer_params buffers(int samples = 16)
{
  return {sizeof(ccl_buffer_params), 47, 20, 0, 0, 47, 20, samples, 0, 0, 0, nullptr};
}
static void save(const Pixels &p, const std::filesystem::path &file)
{
  auto out = OIIO::ImageOutput::create(file.string());
  CHECK(out);
  OIIO::ImageSpec spec(p.width, p.height, 9, OIIO::TypeDesc::FLOAT);
  spec.channelnames = {"Combined.R",
                       "Combined.G",
                       "Combined.B",
                       "Combined.A",
                       "Depth.Z",
                       "Normal.X",
                       "Normal.Y",
                       "Normal.Z",
                       "Sample Count.X"};
  std::vector<float> v(size_t(p.width) * p.height * 9);
  int offset = 0;
  for (auto &r : requests) {
    CHECK(p.passes.count(r.name));
    auto &a = p.passes.at(r.name);
    for (int i = 0; i < p.width * p.height; ++i)
      for (int c = 0; c < r.channels; ++c)
        v[i * 9 + offset + c] = a[i * r.channels + c];
    offset += r.channels;
  }
  CHECK(out->open(file.string(), spec));
  CHECK(out->write_image(OIIO::TypeDesc::FLOAT, v.data()));
  CHECK(out->close());
}
int main(int argc, char **argv)
{
  CHECK(argc >= 5 && argc <= 7);
  setvbuf(stdout, nullptr, _IONBF, 0);
  const std::filesystem::path root = argv[3];
  std::filesystem::create_directories(root);
  ccl_init_params init{sizeof(init), argv[1], argv[3]};
  OK(ccl_init(&init));
  for (const char *dir : {"objects", "osl"})
    std::filesystem::copy(std::filesystem::path(argv[2]) / "examples" / dir,
                          root / dir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
  ccl::path_init(argv[1], argv[3]);
  const std::string backend = argv[4];
  auto device = test_device(backend.c_str());
  ccl_session_params p{};
  p.size = sizeof(p);
  OK(ccl_session_params_default(&p));
  p.device_index = device;
  p.threads = 4;
  p.bvh_static = 1;
  for (auto fixture : {"examples/scene_cube_surface.xml",
                       "src/app/deep_output_driver_transparent_test.xml",
                       "examples/scene_cube_volume.xml",
                       "examples/scene_osl_stripes.xml"})
  {
    p.use_osl = std::string(fixture).find("osl") != std::string::npos;
    ccl_session *api;
    OK(ccl_session_create(&p, &api));
    ccl_scene *scene;
    OK(ccl_session_scene(api, &scene));
    auto native_devices = ccl::Device::available_devices();
    ccl::DeviceInfo di;
    bool found = false;
    for (auto &d : native_devices)
      if (ccl::Device::string_from_type(d.type) == backend) {
        di = d;
        found = true;
        break;
      }
    CHECK(found);
    ccl::SessionParams np;
    np.device = np.denoise_device = di;
    np.threads = 4;
    np.headless = np.background = true;
    np.samples = 16;
    np.use_resolution_divider = np.use_auto_tile = false;
    np.temp_dir = root.string();
    np.shadingsystem = p.use_osl ? ccl::SHADINGSYSTEM_OSL : ccl::SHADINGSYSTEM_SVM;
    ccl::SceneParams sp;
    sp.background = true;
    sp.bvh_type = ccl::BVH_TYPE_STATIC;
    sp.shadingsystem = np.shadingsystem;
    ccl::Session reference(np, sp);
    auto xml = root / std::filesystem::path(fixture).filename();
    normalized(std::filesystem::path(argv[2]) / fixture, xml);
    ccl::xml_read_file(reference.scene.get(), xml.string().c_str());
    if (argc >= 6 && strcmp(argv[5], "fixture"))
      reference.scene->integrator->set_seed(std::stoi(argv[5]));
    reference.scene->camera->set_full_width(47);
    reference.scene->camera->set_full_height(20);
    int index = 0;
    for (auto type :
         {ccl::PASS_COMBINED, ccl::PASS_DEPTH, ccl::PASS_NORMAL, ccl::PASS_SAMPLE_COUNT})
    {
      auto *pass = reference.scene->create_node<ccl::Pass>();
      pass->set_type(type);
      pass->set_mode(ccl::PassMode::NOISY);
      pass->set_name(ccl::ustring(requests[index++].name));
    }
    clone_scene(*reference.scene, scene);
    Pixels a, b;
    reference.set_output_driver(ccl::make_unique<NativeOutput>(a));
    OK(ccl_session_set_output_callback(api, requests, 4, output, &b));
    OK(ccl_session_set_progress_callback(api, progress, &b));
    auto bp = buffers();
    OK(ccl_session_reset(api, &bp));
    ccl::BufferParams nb;
    nb.width = nb.full_width = 47;
    nb.height = nb.full_height = 20;
    reference.reset(np, nb);
    reference.start();
    reference.wait();
    CHECK(!reference.progress.get_error());
    if (argc == 7) {
      CHECK(!strcmp(argv[6], "native-only"));
      save(a, root / (xml.stem().string() + "-native.exr"));
      OK(ccl_session_destroy(api));
      continue;
    }
    OK(ccl_session_start(api));
    OK(ccl_session_wait(api));
    CHECK(a.finals == 4 && b.finals == 4 && b.progress > 0 && b.reentries > 0);
    const auto stem = xml.stem().string();
    save(a, root / (stem + "-native.exr"));
    save(b, root / (stem + "-api.exr"));
    if (backend == "CPU") {
      CHECK(a.passes == b.passes);
      printf("PASS CPU exact: %s\n", fixture);
    }
    else
      printf("GPU parity outputs saved: %s (policy analysis required)\n", fixture);
    OK(ccl_session_destroy(api));
  }
}
