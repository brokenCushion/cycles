/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic socket storage covers types with no writable built-in scene socket. */
#include "../cycles_c.cpp"
#include <cstdlib>
#include <thread>
#define TEST_CHECK(x) \
  do { \
    if (!(x)) { \
      std::fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      std::abort(); \
    } \
  } while (0)
template<typename T> struct Sample : ccl::Node {
  T value{};
  explicit Sample(ccl::NodeType *t) : Node(t) {}
  ~Sample()
  {
    dereference_all_used_nodes();
  }
};
template<typename T> struct Slot {
  ccl::NodeType type;
  Sample<T> sample;
  ccl_node *handle;
  ccl_socket_id id;
  Slot(ccl_scene *scene,
       ccl::SocketType::Type kind,
       const ccl::NodeEnum *en = nullptr,
       const ccl::NodeType *constraint = nullptr)
      : sample(&type)
  {
    type.name = ccl::ustring("capi_test");
    auto offset = reinterpret_cast<const char *>(&sample.value) -
                  reinterpret_cast<const char *>(&sample);
    type.register_input(
        ccl::ustring("value"), ccl::ustring("Value"), kind, int(offset), nullptr, en, constraint);
    handle = wrap_node(scene, &sample);
    socket_ids.push_back({&type, &type.inputs[0]});
    id = socket_ids.size();
  }
  ~Slot()
  {
    live_nodes.erase(handle);
    handle->scene->handles_by_node.erase(&sample);
    handle->native = nullptr;
    socket_ids[id - 1] = {nullptr, nullptr};
  }
};
template<typename Public,
         typename Native,
         typename Set,
         typename Get,
         typename SetID,
         typename GetID,
         typename Equal>
void scalar(ccl_scene *scene,
            ccl::SocketType::Type type,
            Public v,
            Set set,
            Get get,
            SetID setid,
            GetID getid,
            Equal equal,
            const ccl::NodeEnum *en = nullptr)
{
  Slot<Native> s(scene, type, en);
  Public out{};
  TEST_CHECK(set(s.handle, "value", v) == CCL_SUCCESS);
  TEST_CHECK(equal(s.sample.value, v));
  TEST_CHECK(getid(s.handle, s.id, &out) == CCL_SUCCESS);
  TEST_CHECK(equal(s.sample.value, out));
  s.sample.clear_modified();
  TEST_CHECK(setid(s.handle, s.id, v) == CCL_SUCCESS);
  TEST_CHECK(get(s.handle, "value", &out) == CCL_SUCCESS);
  TEST_CHECK(equal(s.sample.value, out));
  TEST_CHECK(getid(s.handle, s.id, nullptr) == CCL_ERROR_INVALID_ARGUMENT);
  TEST_CHECK(ccl_node_get_float_id(s.handle, 0, nullptr) == CCL_ERROR_INVALID_ARGUMENT);
}
template<typename Public, typename Native, typename Equal>
void array_test(ccl_scene *scene,
                ccl::SocketType::Type type,
                ccl_socket_type api_type,
                Public value,
                Equal equal)
{
  Slot<ccl::array<Native>> s(scene, type);
  Public data[2] = {value, value};
  Public out[2]{};
  unsigned char saved[sizeof(out)];
  memcpy(saved, out, sizeof(out));
  uint64_t count = 99;
  TEST_CHECK(ccl_node_set_array(s.handle, "value", api_type, data, 2) == CCL_SUCCESS);
  TEST_CHECK(s.sample.value.size() == 2 && equal(s.sample.value[1], value));
  data[1] = {};  // Prove copied storage.
  TEST_CHECK(equal(s.sample.value[1], value));
  TEST_CHECK(ccl_node_get_array_id(s.handle, s.id, api_type, nullptr, 0, &count) == CCL_SUCCESS &&
             count == 2);
  TEST_CHECK(ccl_node_get_array(s.handle, "value", api_type, out, 1, &count) ==
                 CCL_ERROR_BUFFER_TOO_SMALL &&
             count == 2);
  TEST_CHECK(memcmp(saved, out, sizeof(out)) == 0);
  TEST_CHECK(ccl_node_get_array_id(s.handle, s.id, api_type, out, 2, &count) == CCL_SUCCESS);
  TEST_CHECK(equal(s.sample.value[0], out[0]) && equal(s.sample.value[1], out[1]));
  TEST_CHECK(ccl_node_set_array_id(s.handle, s.id, api_type, out, 2) == CCL_SUCCESS);
  TEST_CHECK(ccl_node_set_array_id(s.handle, s.id, api_type, nullptr, 1) ==
             CCL_ERROR_INVALID_ARGUMENT);
  TEST_CHECK(ccl_node_set_array_id(s.handle, s.id, api_type, out, UINT64_MAX) ==
             CCL_ERROR_INVALID_ARGUMENT);
  TEST_CHECK(ccl_node_set_array_id(s.handle, s.id, 999, out, 2) == CCL_ERROR_UNSUPPORTED);
  TEST_CHECK(ccl_node_set_float_id(s.handle, s.id, 0) == CCL_ERROR_TYPE_MISMATCH);
  TEST_CHECK(ccl_node_set_array_id(s.handle, s.id, api_type, nullptr, 0) == CCL_SUCCESS);
  TEST_CHECK(ccl_node_get_array(s.handle, "value", api_type, nullptr, 0, &count) == CCL_SUCCESS &&
             count == 0);
}
#include "session_helpers.h"
int main(int argc, char **argv)
{
  TEST_CHECK(argc == 2 || argc == 3);
  ccl_init_params p{sizeof(p), argv[1], argv[1]};
  TEST_CHECK(ccl_init(&p) == CCL_SUCCESS);
  ccl_scene *scene;
  ccl_session_params params{};
  params.size = sizeof(params);
  TEST_CHECK(ccl_session_params_default(&params) == CCL_SUCCESS);
  params.device_index = test_device(argc == 3 ? argv[2] : "CPU");
  ccl_session *session;
  TEST_CHECK(ccl_session_create(&params, &session) == CCL_SUCCESS);
  TEST_CHECK(ccl_session_scene(session, &scene) == CCL_SUCCESS);
  TEST_CHECK(scene->native == session->native->scene.get());
  TEST_CHECK(scene->native->device == session->native->device.get());
  TEST_CHECK(session->native->params.device.id == devices[params.device_index].id);
  ccl::SceneParams defaults;
  TEST_CHECK(!scene->native->params.modified(defaults));
  ccl_scene *again = nullptr;
  TEST_CHECK(ccl_session_scene(session, &again) == CCL_SUCCESS && again == scene);
  ccl_session *untouched = session;
  auto bad = params;
  bad.size = 0;
  TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_INVALID_ARGUMENT &&
             untouched == session);
  bad = params;
  bad.headless = 2;
  TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_INVALID_ARGUMENT);
  bad = params;
  bad.device_index = UINT32_MAX - 1;
  TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_INVALID_ARGUMENT);
  bad = params;
  bad.bvh_layout = "not-a-layout";
  TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_INVALID_ARGUMENT);
  bad = params;
  bad.texture_resolution = NAN;
  TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_INVALID_ARGUMENT);
  TEST_CHECK(ccl_session_scene(nullptr, &again) == CCL_ERROR_INVALID_ARGUMENT);
  TEST_CHECK(ccl_session_destroy(nullptr) == CCL_ERROR_INVALID_ARGUMENT);
  if (!devices[params.device_index].has_osl) {
    bad = params;
    bad.use_osl = 1;
    TEST_CHECK(ccl_session_create(&bad, &untouched) == CCL_ERROR_UNSUPPORTED);
  }
  auto options = params;
  options.threads = 2;
  options.background = 0;
  options.bvh_static = 1;
  options.bvh_spatial_split = 1;
  options.bvh_compact_structure = 0;
  options.bvh_unaligned_nodes = 0;
  options.bvh_time_steps = 2;
  options.hair_subdivisions = 4;
  options.hair_shape = 1;
  options.texture_resolution = 0.5f;
  options.texture_limit = 1024;
  options.use_texture_cache = 0;
  options.auto_texture_cache = 1;
  std::string cache = std::string(argv[1]) + "/test-cache";
  options.texture_cache_path = cache.c_str();
  ccl_session *configured;
  TEST_CHECK(ccl_session_create(&options, &configured) == CCL_SUCCESS);
  cache.clear();
  auto &actual = configured->native->scene->params;
  TEST_CHECK(configured->native->params.threads == 2 && !actual.background);
  TEST_CHECK(actual.bvh_type == ccl::BVH_TYPE_STATIC && actual.use_bvh_spatial_split &&
             !actual.use_bvh_compact_structure && !actual.use_bvh_unaligned_nodes &&
             actual.num_bvh_time_steps == 2 && actual.hair_subdivisions == 4 &&
             actual.hair_shape == ccl::CURVE_THICK && actual.texture_resolution == 0.5f &&
             actual.texture_limit == 1024 && !actual.use_texture_cache &&
             actual.auto_texture_cache &&
             actual.texture_cache_path == std::string(argv[1]) + "/test-cache");
  TEST_CHECK(ccl_session_destroy(configured) == CCL_SUCCESS);
  auto eq = [](auto a, auto b) { return a == b; };
  auto v2 = [](ccl::float2 a, ccl_float2 b) { return a.x == b.x && a.y == b.y; };
  auto v3 = [](auto a, ccl_float3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
  auto str = [](ccl::ustring a, const char *b) { return a == ccl::ustring(b); };
  auto tf = [](const ccl::Transform &a, ccl_transform b) {
    auto c = public_transform(a);
    return !memcmp(c.m, b.m, sizeof(c.m));
  };
  ccl_transform transform{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};
#define SCALAR(P, N, T, A, V, E) \
  scalar<P, N>(scene, \
               ccl::SocketType::T, \
               V, \
               ccl_node_set_##A, \
               ccl_node_get_##A, \
               ccl_node_set_##A##_id, \
               ccl_node_get_##A##_id, \
               E)
  SCALAR(int32_t, bool, BOOLEAN, bool, 1, eq);
  SCALAR(float, float, FLOAT, float, 1.25f, eq);
  SCALAR(int32_t, int, INT, int, -47, eq);
  SCALAR(uint32_t, uint32_t, UINT, uint, UINT32_MAX, eq);
  SCALAR(uint64_t, uint64_t, UINT64, uint64, UINT64_MAX, eq);
  SCALAR(ccl_float2, ccl::float2, POINT2, float2, (ccl_float2{2, 3}), v2);
  for (auto t : {ccl::SocketType::COLOR,
                 ccl::SocketType::VECTOR,
                 ccl::SocketType::POINT,
                 ccl::SocketType::NORMAL})
    scalar<ccl_float3, ccl::float3>(scene,
                                    t,
                                    {2, 3, 4},
                                    ccl_node_set_float3,
                                    ccl_node_get_float3,
                                    ccl_node_set_float3_id,
                                    ccl_node_get_float3_id,
                                    v3);
  SCALAR(const char *, ccl::ustring, STRING, string, "snow \xE9\x9B\xAA", str);
  SCALAR(ccl_transform, ccl::Transform, TRANSFORM, transform, transform, tf);
  ccl::NodeEnum en;
  en.insert("negative", -9);
  en.insert("positive", 17);
  scalar<int32_t, int>(scene,
                       ccl::SocketType::ENUM,
                       -9,
                       ccl_node_set_enum,
                       ccl_node_get_enum,
                       ccl_node_set_enum_id,
                       ccl_node_get_enum_id,
                       eq,
                       &en);
#undef SCALAR
#define ARRAY(P, N, T, V, E) array_test<P, N>(scene, ccl::SocketType::T, CCL_SOCKET_##T, V, E)
  ARRAY(int32_t, bool, BOOLEAN_ARRAY, 1, eq);
  ARRAY(float, float, FLOAT_ARRAY, 1.5f, eq);
  ARRAY(int32_t, int, INT_ARRAY, -7, eq);
  ARRAY(ccl_float2, ccl::float2, POINT2_ARRAY, (ccl_float2{2, 3}), v2);
  ARRAY(ccl_float3, ccl::packed_float3, COLOR_ARRAY, (ccl_float3{2, 3, 4}), v3);
  ARRAY(ccl_float3, ccl::packed_float3, VECTOR_ARRAY, (ccl_float3{2, 3, 4}), v3);
  ARRAY(ccl_float3, ccl::packed_float3, POINT_ARRAY, (ccl_float3{2, 3, 4}), v3);
  ARRAY(ccl_float3, ccl::packed_float3, NORMAL_ARRAY, (ccl_float3{2, 3, 4}), v3);
  ARRAY(const char *, ccl::ustring, STRING_ARRAY, "array UTF8 \xE9\x9B\xAA", str);
  ARRAY(ccl_transform, ccl::Transform, TRANSFORM_ARRAY, transform, tf);
#undef ARRAY
  ccl_node *mesh;
  TEST_CHECK(ccl_node_create(scene, "mesh", nullptr, &mesh) == CCL_SUCCESS);
  auto ref = [](ccl::Node *a, ccl_node *b) { return a == b->native; };
  scalar<ccl_node *, ccl::Node *>(scene,
                                  ccl::SocketType::NODE,
                                  mesh,
                                  ccl_node_set_node,
                                  ccl_node_get_node,
                                  ccl_node_set_node_id,
                                  ccl_node_get_node_id,
                                  ref);
  array_test<ccl_node *, ccl::Node *>(
      scene, ccl::SocketType::NODE_ARRAY, CCL_SOCKET_NODE_ARRAY, mesh, ref);
  TEST_CHECK(mesh->native->reference_count() == 0);
  {
    Slot<int> s(scene, ccl::SocketType::ENUM, &en);
    TEST_CHECK(ccl_node_set_enum_id(s.handle, s.id, 123) == CCL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK(s.sample.value == 0);
  }
  {
    Slot<bool> s(scene, ccl::SocketType::BOOLEAN);
    TEST_CHECK(ccl_node_set_bool(s.handle, "value", 2) == CCL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK(!s.sample.value);
  }
  {
    Slot<ccl::array<bool>> s(scene, ccl::SocketType::BOOLEAN_ARRAY);
    int32_t b[] = {1, 2};
    TEST_CHECK(ccl_node_set_array(s.handle, "value", CCL_SOCKET_BOOLEAN_ARRAY, b, 2) ==
               CCL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK(s.sample.value.empty());
  }
  {
    Slot<ccl::array<ccl::Node *>> s(scene, ccl::SocketType::NODE_ARRAY);
    ccl_node *b[] = {mesh, nullptr};
    TEST_CHECK(ccl_node_set_array(s.handle, "value", CCL_SOCKET_NODE_ARRAY, b, 2) ==
               CCL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK(s.sample.value.empty());
    TEST_CHECK(mesh->native->reference_count() == 0);
  }
  {
    Slot<ccl::ustring> s(scene, ccl::SocketType::STRING);
    char value[] = "owned";
    TEST_CHECK(ccl_node_set_string(s.handle, "value", value) == CCL_SUCCESS);
    value[0] = 'x';
    TEST_CHECK(s.sample.value == ccl::ustring("owned"));
    TEST_CHECK(ccl_node_set_string_id(s.handle, s.id, nullptr) == CCL_ERROR_INVALID_ARGUMENT);
  }
  for (auto t : {ccl::SocketType::UNDEFINED, ccl::SocketType::CLOSURE}) {
    Slot<int> s(scene, t);
    TEST_CHECK(ccl_node_set_int(s.handle, "value", 2) == CCL_ERROR_TYPE_MISMATCH);
  }
  ccl_node *camera;
  TEST_CHECK(ccl_scene_singleton(scene, "camera", &camera) == CCL_SUCCESS);
  ccl_socket_id id;
  TEST_CHECK(ccl_socket_find("camera", "fov", &id) == CCL_SUCCESS);
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t)
    threads.emplace_back([&] {
      for (int i = 0; i < 1000; ++i) {
        TEST_CHECK(ccl_node_set_float_id(camera, id, float(i)) == CCL_SUCCESS);
        float f;
        TEST_CHECK(ccl_node_get_float(camera, "fov", &f) == CCL_SUCCESS);
      }
    });
  for (auto &t : threads)
    t.join();
  TEST_CHECK(ccl_session_destroy(session) == CCL_SUCCESS);
  puts(
      "PASS all 25 value types, 2 non-value types, copied arrays, references, invalid values and "
      "concurrent edits");
}
