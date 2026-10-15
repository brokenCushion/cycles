/* SPDX-License-Identifier: Apache-2.0 */
#include "../cycles_c.h"
#include "scene/camera.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif
#define TEST_CHECK(x) \
  do { \
    if (!(x)) { \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
      std::exit(1); \
    } \
  } while (0)
int main(int argc, char **argv)
{
  TEST_CHECK(argc == 4);
#ifdef _WIN32
  auto library = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  TEST_CHECK(library);
#  define LOAD(n) \
    auto n = reinterpret_cast<decltype(&::n)>(GetProcAddress(library, #n)); \
    TEST_CHECK(n)
#else
  auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  TEST_CHECK(library);
#  define LOAD(n) \
    auto n = reinterpret_cast<decltype(&::n)>(dlsym(library, #n)); \
    TEST_CHECK(n)
#endif
  LOAD(ccl_init);
  LOAD(ccl_session_create);
  LOAD(ccl_session_params_default);
  LOAD(ccl_session_scene);
  LOAD(ccl_session_destroy);
  LOAD(ccl_scene_singleton);
  LOAD(ccl_socket_find);
  LOAD(ccl_node_set_float);
  LOAD(ccl_node_set_float_id);
  LOAD(ccl_node_get_float_id);
  ccl_init_params p{sizeof(p), argv[2], argv[2]};
  TEST_CHECK(ccl_init(&p) == CCL_SUCCESS);
  ccl_scene *scene;
  ccl_node *camera;
  ccl_socket_id id;
  ccl_session *session;
  ccl_session_params params{};
  params.size = sizeof(params);
  TEST_CHECK(ccl_session_params_default(&params) == CCL_SUCCESS);
  TEST_CHECK(ccl_session_create(&params, &session) == CCL_SUCCESS);
  TEST_CHECK(ccl_session_scene(session, &scene) == CCL_SUCCESS);
  TEST_CHECK(ccl_scene_singleton(scene, "camera", &camera) == CCL_SUCCESS);
  TEST_CHECK(ccl_socket_find("camera", "fov", &id) == CCL_SUCCESS);
  ccl::Camera native;
  const auto *socket = native.type->find_input(ccl::ustring("fov"));
  TEST_CHECK(socket);
  constexpr int count = 1000000, rounds = 9;
  std::array<std::vector<double>, 3> samples;
  for (int round = -1; round < rounds; ++round)
    for (int order = 0; order < 3; ++order) {
      int which = (order + (round < 0 ? 0 : round)) % 3;
      int32_t status = 0;
      auto start = std::chrono::steady_clock::now();
      if (which == 0)
        for (int i = 0; i < count; ++i)
          native.set(*socket, float(i & 1));
      if (which == 1)
        for (int i = 0; i < count; ++i)
          status |= ccl_node_set_float(camera, "fov", float(i & 1));
      if (which == 2)
        for (int i = 0; i < count; ++i)
          status |= ccl_node_set_float_id(camera, id, float(i & 1));
      auto end = std::chrono::steady_clock::now();
      TEST_CHECK(status == 0);
      if (which == 0)
        TEST_CHECK(native.get_float(*socket) == 1.0f);
      else {
        float v;
        TEST_CHECK(ccl_node_get_float_id(camera, id, &v) == CCL_SUCCESS && v == 1.0f);
      }
      if (round >= 0)
        samples[which].push_back(std::chrono::duration<double, std::nano>(end - start).count() /
                                 count);
    }
  FILE *out = std::fopen(argv[3], "w");
  TEST_CHECK(out);
  std::fprintf(out,
               "{\"iterations_per_round\":%d,\"rounds\":%d,\"unit\":\"ns_per_set\",\"results\":{",
               count,
               rounds);
  const char *names[] = {"direct_cpp", "name", "id"};
  for (int i = 0; i < 3; ++i) {
    auto sorted = samples[i];
    std::sort(sorted.begin(), sorted.end());
    std::fprintf(out,
                 "%s\"%s\":{\"median\":%.6f,\"min\":%.6f,\"max\":%.6f,\"samples\":[",
                 i ? "," : "",
                 names[i],
                 sorted[rounds / 2],
                 sorted.front(),
                 sorted.back());
    for (int j = 0; j < rounds; ++j)
      std::fprintf(out, "%s%.6f", j ? "," : "", samples[i][j]);
    std::fprintf(out, "]}");
    std::printf("%s median %.3f ns/set (%.3f..%.3f)\n",
                names[i],
                sorted[rounds / 2],
                sorted.front(),
                sorted.back());
  }
  std::fprintf(out, "}}\n");
  std::fclose(out);
  TEST_CHECK(ccl_session_destroy(session) == CCL_SUCCESS);
  // Retain DLL until process exit, as the API lifetime contract requires.
}
