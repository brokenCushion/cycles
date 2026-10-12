/* SPDX-License-Identifier: Apache-2.0 */
/* Private implementation included by cycles_c.cpp so all API calls share one boundary/lock. */
#include "graph/node.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/hair.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/particles.h"
#include "scene/pass.h"
#include "scene/pointcloud.h"
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/volume.h"
#include "session/session.h"
#include "util/profiling.h"
#include "util/stats.h"
#include <cmath>
#include <atomic>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

struct ccl_node {
  ccl_scene *scene;
  ccl::Node *native;
  void (*remove)(ccl::Scene *, ccl::Node *);
  ccl_shader_graph *graph = nullptr;
};
struct ccl_scene {
  ccl::Scene *native;
  ccl_session *owner = nullptr;
  std::vector<std::unique_ptr<ccl_node>> handles;
  std::unordered_map<ccl::Node *, ccl_node *> handles_by_node;
};
struct ccl_session {
  ccl::unique_ptr<ccl::Session> native;
  ccl_scene scene;
  ccl::SessionParams params;
  ccl::BufferParams buffers;
  bool configured = false, active = false;
  unsigned waiters = 0;
  std::atomic<bool> paused{false}, needs_reset{true};
  ccl_progress_fn progress_fn = nullptr;
  void *progress_user = nullptr;
  struct OutputPass { std::string name; int channels; };
  std::vector<OutputPass> output_passes;
  ccl_output_fn output_fn = nullptr;
  void *output_user = nullptr;
  ccl_deep_fn deep_fn = nullptr;
  void *deep_user = nullptr;
};
struct ccl_shader_graph {
  ccl_scene *scene;
  ccl::unique_ptr<ccl::ShaderGraph> native;
};
namespace {
std::unordered_map<ccl_shader_graph *, std::unique_ptr<ccl_shader_graph>> graphs;
void forget_graph(ccl_shader_graph *g);
std::unordered_set<ccl_scene *> scenes;
std::unordered_map<ccl_session *, std::unique_ptr<ccl_session>> sessions;
std::unordered_set<ccl_node *> live_nodes;
struct SocketBinding {
  const ccl::NodeType *type;
  const ccl::SocketType *socket;
};
std::vector<SocketBinding> socket_ids;

void install_session_bridges(ccl_session &s);
ccl_status scene_access(ccl_scene *s, bool write);

ccl_status editing()
{
  return initialized ? CCL_SUCCESS : fail(CCL_ERROR_NOT_INITIALIZED, "Call ccl_init first");
}
bool valid(ccl_scene *s)
{
  return scenes.find(s) != scenes.end();
}
bool valid(ccl_node *n)
{
  return live_nodes.find(n) != live_nodes.end();
}
ccl_node *wrap_node(ccl_scene *s,
                    ccl::Node *n,
                    void (*remove)(ccl::Scene *, ccl::Node *) = nullptr)
{
  if (!n)
    return nullptr;
  if (const auto found = s->handles_by_node.find(n); found != s->handles_by_node.end())
    return found->second;
  auto h = std::make_unique<ccl_node>(ccl_node{s, n, remove});
  auto *out = h.get();
  s->handles.push_back(std::move(h));
  try {
    s->handles_by_node.emplace(n, out);
    live_nodes.insert(out);
  }
  catch (...) {
    s->handles_by_node.erase(n);
    s->handles.pop_back();
    throw;
  }
  return out;
}
const ccl::SocketType *resolve(ccl_node *n, const char *name)
{
  if (!name) {
    fail(CCL_ERROR_INVALID_ARGUMENT, "NULL socket name");
    return nullptr;
  }
  const auto *s = n->native->type->find_input(ccl::ustring(name));
  if (!s)
    fail(CCL_ERROR_NOT_FOUND, "Unknown input socket");
  return s;
}
const ccl::SocketType *resolve(ccl_node *n, ccl_socket_id id)
{
  if (!id || id > socket_ids.size() || socket_ids[id - 1].type != n->native->type) {
    fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid socket ID or wrong node type");
    return nullptr;
  }
  return socket_ids[id - 1].socket;
}
template<bool Write = false, typename Key, typename F> ccl_status access(ccl_node *n, Key key, F fn)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node handle");
    if (auto st = scene_access(n->scene, Write))
      return st;
    const auto *s = resolve(n, key);
    if (!s)
      return std::is_same_v<Key, ccl_socket_id> || !key ? CCL_ERROR_INVALID_ARGUMENT :
                                                          CCL_ERROR_NOT_FOUND;
    return fn(*n, *s);
  });
}
ccl_status wrong_type()
{
  return fail(CCL_ERROR_TYPE_MISMATCH, "Socket type does not match accessor");
}
ccl_status reference(ccl_node &n, const ccl::SocketType &s, ccl_node *ref, ccl::Node *&out)
{
  if (!ref) {
    out = nullptr;
    return CCL_SUCCESS;
  }
  if (!valid(ref) || ref->scene != n.scene || ref == &n)
    return fail(CCL_ERROR_INVALID_ARGUMENT,
                "Reference must be a different live node in the same scene");
  if (s.node_type && !ref->native->is_a(s.node_type))
    return wrong_type();
  out = ref->native;
  return CCL_SUCCESS;
}
template<typename T> void remove_node(ccl::Scene *s, ccl::Node *n)
{
  s->delete_node(static_cast<T *>(n));
}
void remove_object(ccl::Scene *s, ccl::Node *n)
{
  auto *object = static_cast<ccl::Object *>(n);
  if (object->get_geometry())
    s->delete_node(object);
  else {
    /* Native single-object deletion assumes geometry; an unfinished API object has none. */
    s->objects.erase_by_swap(object);
    s->object_manager->tag_update(s, ccl::ObjectManager::OBJECT_REMOVED);
  }
}
struct Factory {
  const char *name;
  ccl::Node *(*create)(ccl::Scene *);
  void (*remove)(ccl::Scene *, ccl::Node *);
  ccl_shader_graph *graph = nullptr;
};
template<typename T> ccl::Node *create_node(ccl::Scene *s)
{
  return s->create_node<T>();
}
template<> ccl::Node *create_node<ccl::Shader>(ccl::Scene *s)
{
  // Native shader updates require an output graph, even before any graph edits.
  auto graph = ccl::make_unique<ccl::ShaderGraph>();
  auto *shader = s->create_node<ccl::Shader>();
  shader->set_graph(std::move(graph));
  return shader;
}
const Factory factories[] = {
#define FACTORY(name, T) {name, create_node<ccl::T>, remove_node<ccl::T>}
    {"object", create_node<ccl::Object>, remove_object},
    FACTORY("mesh", Mesh),
    FACTORY("hair", Hair),
    FACTORY("pointcloud", PointCloud),
    FACTORY("volume", Volume),
    FACTORY("shader", Shader),
    FACTORY("pass", Pass),
    FACTORY("particle_system", ParticleSystem),
#undef FACTORY
#define LIGHT(name, T) {name, create_node<ccl::T>, remove_node<ccl::Light>}
    LIGHT("pointlight", PointLight),
    LIGHT("spotlight", SpotLight),
    LIGHT("arealight", AreaLight),
    LIGHT("sunlight", SunLight),
    LIGHT("backgroundlight", BackgroundLight)
#undef LIGHT
};
ccl::Transform native_transform(ccl_transform v)
{
  ccl::Transform t;
  t.x = ccl::make_float4(v.m[0], v.m[1], v.m[2], v.m[3]);
  t.y = ccl::make_float4(v.m[4], v.m[5], v.m[6], v.m[7]);
  t.z = ccl::make_float4(v.m[8], v.m[9], v.m[10], v.m[11]);
  return t;
}
ccl_transform public_transform(const ccl::Transform &t)
{
  return {{t.x.x, t.x.y, t.x.z, t.x.w, t.y.x, t.y.y, t.y.z, t.y.w, t.z.x, t.z.y, t.z.z, t.z.w}};
}
}  // namespace

ccl_status CCL_CALL ccl_session_create(const ccl_session_params *p, ccl_session **out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!p || p->size < sizeof(*p) || !out || !p->bvh_layout || p->threads < 0 ||
        p->bvh_time_steps < 0 || p->hair_subdivisions < 0 || p->hair_subdivisions > 4 ||
        p->hair_shape < 0 || p->hair_shape > 2 || p->texture_limit < 0 ||
        !std::isfinite(p->texture_resolution) || p->texture_resolution <= 0)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session settings");
    for (auto v : {p->headless,
                   p->background,
                   p->use_osl,
                   p->bvh_static,
                   p->bvh_spatial_split,
                   p->bvh_compact_structure,
                   p->bvh_unaligned_nodes,
                   p->use_texture_cache,
                   p->auto_texture_cache})
      if (v != 0 && v != 1)
        return fail(CCL_ERROR_INVALID_ARGUMENT, "Bool must be 0 or 1");
    ccl::SceneParams sp;
    const struct {
      const char *name;
      ccl::BVHLayout layout;
    } layouts[] = {{"AUTO", ccl::BVH_LAYOUT_AUTO},
                   {"BVH2", ccl::BVH_LAYOUT_BVH2},
                   {"EMBREE", ccl::BVH_LAYOUT_EMBREE},
                   {"OPTIX", ccl::BVH_LAYOUT_OPTIX},
                   {"METAL", ccl::BVH_LAYOUT_METAL},
                   {"HIPRT", ccl::BVH_LAYOUT_HIPRT},
                   {"EMBREEGPU", ccl::BVH_LAYOUT_EMBREEGPU}};
    bool found = false;
    for (auto &l : layouts)
      if (!strcmp(p->bvh_layout, l.name)) {
        sp.bvh_layout = l.layout;
        found = true;
      }
    if (!found)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Unknown BVH layout");
    if (auto st = discover())
      return st;
    uint32_t index = p->device_index;
    if (index == UINT32_MAX) {
      for (uint32_t i = 0; i < devices.size(); ++i)
        if (devices[i].type == ccl::DEVICE_CPU) {
          index = i;
          break;
        }
      if (index == UINT32_MAX)
        return fail(CCL_ERROR_UNSUPPORTED, "CPU unavailable");
    }
    if (index >= devices.size())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid device index");
#ifndef WITH_OSL
    if (p->use_osl)
      return fail(CCL_ERROR_UNSUPPORTED, "OSL not compiled in");
#endif
    if (p->use_osl && !devices[index].has_osl)
      return fail(CCL_ERROR_UNSUPPORTED, "Selected device does not support OSL");
    ccl::SessionParams params;
    params.device = params.denoise_device = devices[index];
    params.threads = p->threads;
    params.headless = bool(p->headless);
    params.background = sp.background = bool(p->background);
    params.shadingsystem = sp.shadingsystem = p->use_osl ? ccl::SHADINGSYSTEM_OSL :
                                                           ccl::SHADINGSYSTEM_SVM;
    sp.bvh_type = p->bvh_static ? ccl::BVH_TYPE_STATIC : ccl::BVH_TYPE_DYNAMIC;
    sp.use_bvh_spatial_split = bool(p->bvh_spatial_split);
    sp.use_bvh_compact_structure = bool(p->bvh_compact_structure);
    sp.use_bvh_unaligned_nodes = bool(p->bvh_unaligned_nodes);
    sp.num_bvh_time_steps = p->bvh_time_steps;
    sp.hair_subdivisions = p->hair_subdivisions;
    sp.hair_shape = ccl::CurveShapeType(p->hair_shape);
    sp.texture_resolution = p->texture_resolution;
    sp.texture_limit = p->texture_limit;
    sp.use_texture_cache = bool(p->use_texture_cache);
    sp.auto_texture_cache = bool(p->auto_texture_cache);
    sp.texture_cache_path = p->texture_cache_path ? p->texture_cache_path : "";
    auto session = std::make_unique<ccl_session>();
    session->native = ccl::make_unique<ccl::Session>(params, sp);
    if (session->native->progress.get_error())
      return fail(CCL_ERROR_INTERNAL, session->native->progress.get_error_message().c_str());
    session->params = params;
    session->scene.native = session->native->scene.get();
    session->scene.owner = session.get();
    install_session_bridges(*session);
    auto *h = session.get();
    sessions.emplace(h, std::move(session));
    try {
      scenes.insert(&h->scene);
    }
    catch (...) {
      sessions.erase(h);
      throw;
    }
    *out = h;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_destroy(ccl_session *h)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!sessions.count(h))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session handle");
    if (h->waiters)
      return fail(CCL_ERROR_IN_USE, "A host thread is waiting on this session");
    h->native->cancel();
    for (auto it = graphs.begin(); it != graphs.end();) {
      auto *g = it->first;
      ++it;
      if (g->scene == &h->scene)
        forget_graph(g);
    }
    for (auto &n : h->scene.handles)
      live_nodes.erase(n.get());
    scenes.erase(&h->scene);
    sessions.erase(h);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_scene(ccl_session *h, ccl_scene **out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!sessions.count(h) || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session/scene output");
    *out = &h->scene;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_scene_singleton(ccl_scene *s, const char *name, ccl_node **out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(s) || !name || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid singleton arguments");
    if (auto st = scene_access(s, false))
      return st;
    ccl::Node *n = nullptr;
    if (!strcmp(name, "camera"))
      n = s->native->camera;
    else if (!strcmp(name, "dicing_camera"))
      n = s->native->dicing_camera;
    else if (!strcmp(name, "film"))
      n = s->native->film;
    else if (!strcmp(name, "integrator"))
      n = s->native->integrator;
    else if (!strcmp(name, "background"))
      n = s->native->background;
    else if (!strcmp(name, "scene_attribute"))
      n = s->native->scene_attribute;
    if (!n)
      return fail(CCL_ERROR_NOT_FOUND, "Unknown scene singleton");
    *out = wrap_node(s, n);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_node_create(ccl_scene *s,
                                    const char *type,
                                    const char *name,
                                    ccl_node **out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(s) || !type || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node creation arguments");
    if (auto st = scene_access(s, true))
      return st;
    for (const auto &f : factories)
      if (!strcmp(type, f.name)) {
        ccl::ustring node_name(name ? name : type);
        auto *n = f.create(s->native);
        n->name = node_name;
        *out = wrap_node(s, n, f.remove);
        return CCL_SUCCESS;
      }
    return ccl::NodeType::find(ccl::ustring(type)) ?
               fail(CCL_ERROR_UNSUPPORTED, "Type is not a creatable scene item") :
               fail(CCL_ERROR_NOT_FOUND, "Unknown node type");
  });
}
ccl_status CCL_CALL ccl_node_delete(ccl_scene *s, ccl_node *n)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(s) || !valid(n) || n->scene != s)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node/scene ownership");
    if (auto st = scene_access(s, true))
      return st;
    if (!n->remove)
      return fail(CCL_ERROR_UNSUPPORTED, "Borrowed singleton/default node cannot be deleted");
    if (n->native->reference_count())
      return fail(CCL_ERROR_IN_USE, "Node still has incoming references");
    n->remove(s->native, n->native);
    s->handles_by_node.erase(n->native);
    n->native = nullptr;
    live_nodes.erase(n);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_node_tag_update(ccl_node *n)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node handle");
    if (auto st = scene_access(n->scene, true))
      return st;
    auto *s = n->scene->native;
    auto *p = n->native;
    if (p->is_a(ccl::Light::get_node_base_type()))
      static_cast<ccl::Light *>(p)->tag_update(s);
    else if (p->is_a(ccl::Geometry::get_node_base_type()))
      static_cast<ccl::Geometry *>(p)->tag_update(s, true);
    else if (p->is_a(ccl::Object::get_node_type()))
      static_cast<ccl::Object *>(p)->tag_update(s);
    else if (p->is_a(ccl::Shader::get_node_type()))
      static_cast<ccl::Shader *>(p)->tag_update(s);
    else if (p->is_a(ccl::ParticleSystem::get_node_type()))
      static_cast<ccl::ParticleSystem *>(p)->tag_update(s);
    else {
      p->tag_modified();
      if (p->is_a(ccl::Pass::get_node_type()))
        s->film->tag_modified();
    }
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_socket_find(const char *type, const char *name, ccl_socket_id *out)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!type || !name || !out)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL socket lookup argument");
    const auto *t = ccl::NodeType::find(ccl::ustring(type));
    const auto *s = t ? t->find_input(ccl::ustring(name)) : nullptr;
    if (!s)
      return fail(CCL_ERROR_NOT_FOUND, "Unknown node type or input socket");
    for (size_t i = 0; i < socket_ids.size(); ++i)
      if (socket_ids[i].type == t && socket_ids[i].socket == s) {
        *out = i + 1;
        return CCL_SUCCESS;
      }
    socket_ids.push_back({t, s});
    *out = socket_ids.size();
    return CCL_SUCCESS;
  });
}

namespace {
ccl_status set_bool(ccl_node &n, const ccl::SocketType &s, int32_t value)
{
  if (!(s.type == ccl::SocketType::BOOLEAN))
    return wrong_type();
  if (value != 0 && value != 1)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Bool must be 0 or 1");
  n.native->set(s, bool(value));
  return CCL_SUCCESS;
}
ccl_status get_bool(ccl_node &n, const ccl::SocketType &s, int32_t *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::BOOLEAN))
    return wrong_type();
  *out = int32_t(n.native->get_bool(s));
  return CCL_SUCCESS;
}
ccl_status set_float(ccl_node &n, const ccl::SocketType &s, float value)
{
  if (!(s.type == ccl::SocketType::FLOAT))
    return wrong_type();

  n.native->set(s, value);
  return CCL_SUCCESS;
}
ccl_status get_float(ccl_node &n, const ccl::SocketType &s, float *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::FLOAT))
    return wrong_type();
  *out = n.native->get_float(s);
  return CCL_SUCCESS;
}
ccl_status set_int(ccl_node &n, const ccl::SocketType &s, int32_t value)
{
  if (!(s.type == ccl::SocketType::INT))
    return wrong_type();

  n.native->set(s, int(value));
  return CCL_SUCCESS;
}
ccl_status get_int(ccl_node &n, const ccl::SocketType &s, int32_t *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::INT))
    return wrong_type();
  *out = n.native->get_int(s);
  return CCL_SUCCESS;
}
ccl_status set_uint(ccl_node &n, const ccl::SocketType &s, uint32_t value)
{
  if (!(s.type == ccl::SocketType::UINT))
    return wrong_type();

  n.native->set(s, uint32_t(value));
  return CCL_SUCCESS;
}
ccl_status get_uint(ccl_node &n, const ccl::SocketType &s, uint32_t *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::UINT))
    return wrong_type();
  *out = n.native->get_uint(s);
  return CCL_SUCCESS;
}
ccl_status set_uint64(ccl_node &n, const ccl::SocketType &s, uint64_t value)
{
  if (!(s.type == ccl::SocketType::UINT64))
    return wrong_type();

  n.native->set(s, value);
  return CCL_SUCCESS;
}
ccl_status get_uint64(ccl_node &n, const ccl::SocketType &s, uint64_t *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::UINT64))
    return wrong_type();
  *out = n.native->get_uint64(s);
  return CCL_SUCCESS;
}
ccl_status set_float2(ccl_node &n, const ccl::SocketType &s, ccl_float2 value)
{
  if (!(s.type == ccl::SocketType::POINT2))
    return wrong_type();

  n.native->set(s, ccl::make_float2(value.x, value.y));
  return CCL_SUCCESS;
}
ccl_status get_float2(ccl_node &n, const ccl::SocketType &s, ccl_float2 *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::POINT2))
    return wrong_type();
  *out = ccl_float2{n.native->get_float2(s).x, n.native->get_float2(s).y};
  return CCL_SUCCESS;
}
ccl_status set_float3(ccl_node &n, const ccl::SocketType &s, ccl_float3 value)
{
  if (!(ccl::SocketType::is_float3(s.type)))
    return wrong_type();

  n.native->set(s, ccl::make_float3(value.x, value.y, value.z));
  return CCL_SUCCESS;
}
ccl_status get_float3(ccl_node &n, const ccl::SocketType &s, ccl_float3 *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(ccl::SocketType::is_float3(s.type)))
    return wrong_type();
  *out = ccl_float3{
      n.native->get_float3(s).x, n.native->get_float3(s).y, n.native->get_float3(s).z};
  return CCL_SUCCESS;
}
ccl_status set_string(ccl_node &n, const ccl::SocketType &s, const char *value)
{
  if (!(s.type == ccl::SocketType::STRING))
    return wrong_type();
  if (!value)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL string");
  n.native->set(s, value);
  return CCL_SUCCESS;
}
ccl_status get_string(ccl_node &n, const ccl::SocketType &s, const char **out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::STRING))
    return wrong_type();
  *out = text(n.native->get_string(s));
  return CCL_SUCCESS;
}
ccl_status set_enum(ccl_node &n, const ccl::SocketType &s, int32_t value)
{
  if (!(s.type == ccl::SocketType::ENUM))
    return wrong_type();
  if (!s.enum_values || !s.enum_values->exists(int(value)))
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Unknown enum value");
  n.native->set(s, int(value));
  return CCL_SUCCESS;
}
ccl_status get_enum(ccl_node &n, const ccl::SocketType &s, int32_t *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::ENUM))
    return wrong_type();
  *out = n.native->get_int(s);
  return CCL_SUCCESS;
}
ccl_status set_transform(ccl_node &n, const ccl::SocketType &s, ccl_transform value)
{
  if (!(s.type == ccl::SocketType::TRANSFORM))
    return wrong_type();

  n.native->set(s, native_transform(value));
  return CCL_SUCCESS;
}
ccl_status get_transform(ccl_node &n, const ccl::SocketType &s, ccl_transform *out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::TRANSFORM))
    return wrong_type();
  *out = public_transform(n.native->get_transform(s));
  return CCL_SUCCESS;
}
ccl_status set_node(ccl_node &n, const ccl::SocketType &s, ccl_node *value)
{
  if (!(s.type == ccl::SocketType::NODE))
    return wrong_type();
  ccl::Node *ref;
  if (auto st = reference(n, s, value, ref))
    return st;
  n.native->set(s, ref);
  return CCL_SUCCESS;
}
ccl_status get_node(ccl_node &n, const ccl::SocketType &s, ccl_node **out)
{
  if (!out)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL output");
  if (!(s.type == ccl::SocketType::NODE))
    return wrong_type();
  *out = wrap_node(n.scene, n.native->get_node(s));
  return CCL_SUCCESS;
}
}  // namespace
ccl_status CCL_CALL ccl_node_set_bool(ccl_node *n, const char *key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_bool(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_bool(ccl_node *n, const char *key, int32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_bool(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_bool_id(ccl_node *n, ccl_socket_id key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_bool(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_bool_id(ccl_node *n, ccl_socket_id key, int32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_bool(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float(ccl_node *n, const char *key, float value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float(ccl_node *n, const char *key, float *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float_id(ccl_node *n, ccl_socket_id key, float value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float_id(ccl_node *n, ccl_socket_id key, float *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_int(ccl_node *n, const char *key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_int(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_int(ccl_node *n, const char *key, int32_t *out)
{
  return access(n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_int(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_int_id(ccl_node *n, ccl_socket_id key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_int(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_int_id(ccl_node *n, ccl_socket_id key, int32_t *out)
{
  return access(n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_int(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_uint(ccl_node *n, const char *key, uint32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_uint(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_uint(ccl_node *n, const char *key, uint32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_uint(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_uint_id(ccl_node *n, ccl_socket_id key, uint32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_uint(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_uint_id(ccl_node *n, ccl_socket_id key, uint32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_uint(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_uint64(ccl_node *n, const char *key, uint64_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_uint64(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_uint64(ccl_node *n, const char *key, uint64_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_uint64(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_uint64_id(ccl_node *n, ccl_socket_id key, uint64_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_uint64(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_uint64_id(ccl_node *n, ccl_socket_id key, uint64_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_uint64(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float2(ccl_node *n, const char *key, ccl_float2 value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float2(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float2(ccl_node *n, const char *key, ccl_float2 *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float2(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float2_id(ccl_node *n, ccl_socket_id key, ccl_float2 value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float2(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float2_id(ccl_node *n, ccl_socket_id key, ccl_float2 *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float2(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float3(ccl_node *n, const char *key, ccl_float3 value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float3(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float3(ccl_node *n, const char *key, ccl_float3 *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float3(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_float3_id(ccl_node *n, ccl_socket_id key, ccl_float3 value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_float3(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_float3_id(ccl_node *n, ccl_socket_id key, ccl_float3 *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_float3(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_string(ccl_node *n, const char *key, const char *value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_string(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_string(ccl_node *n, const char *key, const char **out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_string(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_string_id(ccl_node *n, ccl_socket_id key, const char *value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_string(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_string_id(ccl_node *n, ccl_socket_id key, const char **out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_string(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_enum(ccl_node *n, const char *key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_enum(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_enum(ccl_node *n, const char *key, int32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_enum(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_enum_id(ccl_node *n, ccl_socket_id key, int32_t value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_enum(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_enum_id(ccl_node *n, ccl_socket_id key, int32_t *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_enum(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_transform(ccl_node *n, const char *key, ccl_transform value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_transform(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_transform(ccl_node *n, const char *key, ccl_transform *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_transform(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_transform_id(ccl_node *n, ccl_socket_id key, ccl_transform value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_transform(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_transform_id(ccl_node *n, ccl_socket_id key, ccl_transform *out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_transform(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_node(ccl_node *n, const char *key, ccl_node *value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_node(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_node(ccl_node *n, const char *key, ccl_node **out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_node(h, s, out); });
}
ccl_status CCL_CALL ccl_node_set_node_id(ccl_node *n, ccl_socket_id key, ccl_node *value)
{
  return access<true>(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return set_node(h, s, value); });
}
ccl_status CCL_CALL ccl_node_get_node_id(ccl_node *n, ccl_socket_id key, ccl_node **out)
{
  return access(
      n, key, [&](ccl_node &h, const ccl::SocketType &s) { return get_node(h, s, out); });
}
namespace {
template<typename Public, typename Native, typename Convert>
ccl_status put_array(
    ccl_node &n, const ccl::SocketType &s, const void *data, uint64_t count, Convert convert)
{
  if ((!data && count) || count > SIZE_MAX / sizeof(Native) || count > SIZE_MAX / sizeof(Public))
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid array buffer or count overflow");
  ccl::array<Native> copy(static_cast<size_t>(count));
  const auto *values = static_cast<const Public *>(data);
  for (size_t i = 0; i < count; ++i)
    if (auto st = convert(values[i], copy[i]))
      return st;
  n.native->set(s, copy);
  return CCL_SUCCESS;
}
template<typename Public, typename Native, typename Convert>
ccl_status copy_array(const ccl::array<Native> &values,
                      void *data,
                      uint64_t capacity,
                      uint64_t *required,
                      Convert convert)
{
  if (!required || (!data && capacity))
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid array output");
  if (!data) {
    *required = values.size();
    return CCL_SUCCESS;
  }
  if (capacity < values.size()) {
    *required = values.size();
    return fail(CCL_ERROR_BUFFER_TOO_SMALL, "Array output too small");
  }
  std::vector<Public> copy;
  copy.reserve(values.size());
  for (const auto &v : values)
    copy.push_back(convert(v));
  if (!copy.empty())
    std::memcpy(data, copy.data(), copy.size() * sizeof(Public));
  *required = copy.size();
  return CCL_SUCCESS;
}
ccl_status array_type(const ccl::SocketType &s, ccl_socket_type type)
{
  if (type < CCL_SOCKET_BOOLEAN_ARRAY || type > CCL_SOCKET_NODE_ARRAY)
    return fail(CCL_ERROR_UNSUPPORTED, "Unknown array type");
  return socket_type(s.type) == type ? CCL_SUCCESS : wrong_type();
}
ccl_status set_array(
    ccl_node &n, const ccl::SocketType &s, ccl_socket_type type, const void *data, uint64_t count)
{
  if (auto st = array_type(s, type))
    return st;
  switch (type) {
    case CCL_SOCKET_BOOLEAN_ARRAY:
      return put_array<int32_t, bool>(n, s, data, count, [](int32_t v, bool &out) {
        if (v != 0 && v != 1)
          return fail(CCL_ERROR_INVALID_ARGUMENT, "Bool must be 0 or 1");
        out = v != 0;
        return CCL_SUCCESS;
      });
    case CCL_SOCKET_FLOAT_ARRAY:
      return put_array<float, float>(n, s, data, count, [](float v, float &out) {
        out = v;
        return CCL_SUCCESS;
      });
    case CCL_SOCKET_INT_ARRAY:
      return put_array<int32_t, int>(n, s, data, count, [](int32_t v, int &out) {
        out = v;
        return CCL_SUCCESS;
      });
    case CCL_SOCKET_POINT2_ARRAY:
      return put_array<ccl_float2, ccl::float2>(
          n, s, data, count, [](ccl_float2 v, ccl::float2 &out) {
            out = ccl::make_float2(v.x, v.y);
            return CCL_SUCCESS;
          });
    case CCL_SOCKET_COLOR_ARRAY:
    case CCL_SOCKET_VECTOR_ARRAY:
    case CCL_SOCKET_POINT_ARRAY:
    case CCL_SOCKET_NORMAL_ARRAY:
      return put_array<ccl_float3, ccl::packed_float3>(
          n, s, data, count, [](ccl_float3 v, ccl::packed_float3 &out) {
            out = ccl::make_float3(v.x, v.y, v.z);
            return CCL_SUCCESS;
          });
    case CCL_SOCKET_TRANSFORM_ARRAY:
      return put_array<ccl_transform, ccl::Transform>(
          n, s, data, count, [](ccl_transform v, ccl::Transform &out) {
            out = native_transform(v);
            return CCL_SUCCESS;
          });
    case CCL_SOCKET_STRING_ARRAY:
      return put_array<const char *, ccl::ustring>(
          n, s, data, count, [](const char *v, ccl::ustring &out) {
            if (!v)
              return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL array string");
            out = ccl::ustring(v);
            return CCL_SUCCESS;
          });
    case CCL_SOCKET_NODE_ARRAY:
      return put_array<ccl_node *, ccl::Node *>(
          n, s, data, count, [&](ccl_node *v, ccl::Node *&out) {
            if (!v)
              return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL node array entry");
            return reference(n, s, v, out);
          });
    default:
      return fail(CCL_ERROR_UNSUPPORTED, "Unknown array type");
  }
}
ccl_status get_array(ccl_node &n,
                     const ccl::SocketType &s,
                     ccl_socket_type type,
                     void *data,
                     uint64_t capacity,
                     uint64_t *required)
{
  if (auto st = array_type(s, type))
    return st;
  switch (type) {
    case CCL_SOCKET_BOOLEAN_ARRAY:
      return copy_array<int32_t>(
          n.native->get_bool_array(s), data, capacity, required, [](bool v) {
            return int32_t(v);
          });
    case CCL_SOCKET_FLOAT_ARRAY:
      return copy_array<float>(
          n.native->get_float_array(s), data, capacity, required, [](float v) { return v; });
    case CCL_SOCKET_INT_ARRAY:
      return copy_array<int32_t>(
          n.native->get_int_array(s), data, capacity, required, [](int v) { return int32_t(v); });
    case CCL_SOCKET_POINT2_ARRAY:
      return copy_array<ccl_float2>(
          n.native->get_float2_array(s), data, capacity, required, [](ccl::float2 v) {
            return ccl_float2{v.x, v.y};
          });
    case CCL_SOCKET_COLOR_ARRAY:
    case CCL_SOCKET_VECTOR_ARRAY:
    case CCL_SOCKET_POINT_ARRAY:
    case CCL_SOCKET_NORMAL_ARRAY:
      return copy_array<ccl_float3>(
          n.native->get_float3_array(s), data, capacity, required, [](ccl::packed_float3 v) {
            return ccl_float3{v.x, v.y, v.z};
          });
    case CCL_SOCKET_TRANSFORM_ARRAY:
      return copy_array<ccl_transform>(
          n.native->get_transform_array(s), data, capacity, required, public_transform);
    case CCL_SOCKET_STRING_ARRAY:
      return copy_array<const char *>(
          n.native->get_string_array(s), data, capacity, required, [](ccl::ustring v) {
            return text(v);
          });
    case CCL_SOCKET_NODE_ARRAY:
      return copy_array<ccl_node *>(
          n.native->get_node_array(s), data, capacity, required, [&](ccl::Node *v) {
            return wrap_node(n.scene, v);
          });
    default:
      return fail(CCL_ERROR_UNSUPPORTED, "Unknown array type");
  }
}
}  // namespace
ccl_status CCL_CALL ccl_node_set_array(
    ccl_node *n, const char *key, ccl_socket_type type, const void *data, uint64_t count)
{
  return access<true>(n, key, [&](ccl_node &h, const ccl::SocketType &s) {
    return set_array(h, s, type, data, count);
  });
}
ccl_status CCL_CALL ccl_node_set_array_id(
    ccl_node *n, ccl_socket_id key, ccl_socket_type type, const void *data, uint64_t count)
{
  return access<true>(n, key, [&](ccl_node &h, const ccl::SocketType &s) {
    return set_array(h, s, type, data, count);
  });
}
ccl_status CCL_CALL ccl_node_get_array(ccl_node *n,
                                       const char *key,
                                       ccl_socket_type type,
                                       void *data,
                                       uint64_t capacity,
                                       uint64_t *required)
{
  return access(n, key, [&](ccl_node &h, const ccl::SocketType &s) {
    return get_array(h, s, type, data, capacity, required);
  });
}
ccl_status CCL_CALL ccl_node_get_array_id(ccl_node *n,
                                          ccl_socket_id key,
                                          ccl_socket_type type,
                                          void *data,
                                          uint64_t capacity,
                                          uint64_t *required)
{
  return access(n, key, [&](ccl_node &h, const ccl::SocketType &s) {
    return get_array(h, s, type, data, capacity, required);
  });
}
