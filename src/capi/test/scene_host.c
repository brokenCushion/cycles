/* SPDX-License-Identifier: Apache-2.0 */
#include "../cycles_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      exit(1); \
    } \
  } while (0)
#include "session_helpers.h"
int main(int argc, char **argv)
{
  CHECK(argc >= 2 && argc <= 4);
  ccl_session *session = NULL, *other_session = NULL;
  ccl_session_params params = {0};
  params.size = sizeof(params);
  CHECK(ccl_session_params_default(&params) == CCL_SUCCESS);
  ccl_scene *s = NULL, *other = NULL;
  CHECK(ccl_session_create(&params, &session) == CCL_ERROR_NOT_INITIALIZED);
  ccl_init_params init = {sizeof(init), argv[1], argv[1]};
  CHECK(ccl_init(&init) == CCL_SUCCESS);
  params.device_index = test_device(argc >= 3 ? argv[2] : "CPU");
  params.use_osl = argc == 4;
  CHECK(ccl_session_create(&params, &session) == CCL_SUCCESS);
  CHECK(ccl_session_scene(session, &s) == CCL_SUCCESS);
  CHECK(ccl_session_create(&params, &other_session) == CCL_SUCCESS);
  CHECK(ccl_session_scene(other_session, &other) == CCL_SUCCESS);
  const char *types[] = {"object",
                         "mesh",
                         "hair",
                         "pointcloud",
                         "volume",
                         "shader",
                         "pass",
                         "particle_system",
                         "pointlight",
                         "spotlight",
                         "arealight",
                         "sunlight",
                         "backgroundlight"};
  for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
    ccl_node *n = NULL;
    CHECK(ccl_node_create(s, types[i], "host node", &n) == CCL_SUCCESS);
    CHECK(ccl_node_tag_update(n) == CCL_SUCCESS);
    CHECK(ccl_node_delete(s, n) == CCL_SUCCESS);
    CHECK(ccl_node_tag_update(n) == CCL_ERROR_INVALID_ARGUMENT);
  }
  const char *singletons[] = {
      "camera", "dicing_camera", "film", "integrator", "background", "scene_attribute"};
  for (unsigned i = 0; i < sizeof(singletons) / sizeof(singletons[0]); ++i) {
    ccl_node *n = NULL;
    CHECK(ccl_scene_singleton(s, singletons[i], &n) == CCL_SUCCESS);
    CHECK(ccl_node_tag_update(n) == CCL_SUCCESS);
    CHECK(ccl_node_delete(s, n) == CCL_ERROR_UNSUPPORTED);
    CHECK(ccl_node_delete(other, n) == CCL_ERROR_INVALID_ARGUMENT);
  }
  ccl_node *camera = NULL, *again = NULL, *object = NULL, *mesh = NULL, *foreign = NULL;
  CHECK(ccl_scene_singleton(s, "camera", &camera) == CCL_SUCCESS);
  CHECK(ccl_scene_singleton(s, "camera", &again) == CCL_SUCCESS && again == camera);
  CHECK(ccl_node_delete(s, camera) == CCL_ERROR_UNSUPPORTED);
  CHECK(ccl_node_create(s, "missing", NULL, &again) == CCL_ERROR_NOT_FOUND && again == camera);
  CHECK(ccl_node_create(s, "camera", NULL, &again) == CCL_ERROR_UNSUPPORTED);
  CHECK(ccl_node_create(s, "object", NULL, &object) == CCL_SUCCESS);
  CHECK(ccl_node_create(s, "mesh", NULL, &mesh) == CCL_SUCCESS);
  CHECK(ccl_node_create(other, "mesh", NULL, &foreign) == CCL_SUCCESS);
  CHECK(ccl_node_set_node(object, "geometry", foreign) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_node_set_node(object, "geometry", camera) == CCL_ERROR_TYPE_MISMATCH);
  CHECK(ccl_node_set_node(object, "geometry", mesh) == CCL_SUCCESS);
  CHECK(ccl_node_get_node(object, "geometry", &again) == CCL_SUCCESS && again == mesh);
  CHECK(ccl_node_delete(s, mesh) == CCL_ERROR_IN_USE);
  CHECK(ccl_node_set_node(object, "geometry", NULL) == CCL_SUCCESS);
  CHECK(ccl_node_delete(s, mesh) == CCL_SUCCESS);
  CHECK(ccl_node_set_node(object, "geometry", mesh) == CCL_ERROR_INVALID_ARGUMENT);
  ccl_socket_id id = 0, id2 = 0;
  CHECK(ccl_socket_find("camera", "fov", &id) == CCL_SUCCESS && id != 0);
  CHECK(ccl_socket_find("camera", "fov", &id2) == CCL_SUCCESS && id == id2);
  CHECK(ccl_node_set_float(camera, "fov", 1.25f) == CCL_SUCCESS);
  float f = 9;
  CHECK(ccl_node_get_float_id(camera, id, &f) == CCL_SUCCESS && f == 1.25f);
  CHECK(ccl_node_set_float_id(camera, id, 0.75f) == CCL_SUCCESS);
  CHECK(ccl_node_get_float(camera, "fov", &f) == CCL_SUCCESS && f == 0.75f);
  CHECK(ccl_node_set_float_id(object, id, 1) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_node_set_float_id(camera, UINT64_MAX, 1) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_node_set_int(camera, "fov", 1) == CCL_ERROR_TYPE_MISMATCH);
  CHECK(ccl_node_get_float(camera, "missing", &f) == CCL_ERROR_NOT_FOUND && f == 0.75f);
  CHECK(ccl_node_get_float(camera, NULL, &f) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_scene_singleton(other, "camera", &again) == CCL_SUCCESS);
  CHECK(ccl_node_set_float_id(again, id, 1) == CCL_SUCCESS);
  uint32_t type_count = 0;
  CHECK(ccl_node_type_count(&type_count) == CCL_SUCCESS);
  for (uint32_t t = 0; t < type_count; ++t) {
    ccl_node_type_desc type = {0};
    type.size = sizeof(type);
    CHECK(ccl_node_type_info(t, &type) == CCL_SUCCESS);
    for (uint32_t i = 0; i < type.input_count; ++i) {
      ccl_socket_desc socket = {0};
      socket.size = sizeof(socket);
      CHECK(ccl_socket_info(type.name, CCL_SOCKET_INPUT, i, &socket) == CCL_SUCCESS);
      ccl_socket_id found = 0;
      CHECK(ccl_socket_find(type.name, socket.name, &found) == CCL_SUCCESS && found != 0);
    }
  }
  /* Growing the ID cache must not invalidate an earlier ID. */
  CHECK(ccl_node_set_float_id(camera, id, 0.5f) == CCL_SUCCESS);
  CHECK(ccl_node_get_float(camera, "fov", &f) == CCL_SUCCESS && f == 0.5f);
  /* Destroy with live object -> geometry -> shader references. */
  CHECK(ccl_node_create(s, "mesh", NULL, &mesh) == CCL_SUCCESS);
  CHECK(ccl_node_set_node(object, "geometry", mesh) == CCL_SUCCESS);
  ccl_node *shader = NULL;
  CHECK(ccl_node_create(s, "shader", NULL, &shader) == CCL_SUCCESS);
  CHECK(ccl_node_set_array(mesh, "used_shaders", CCL_SOCKET_NODE_ARRAY, &shader, 1) ==
        CCL_SUCCESS);
  CHECK(ccl_node_delete(s, shader) == CCL_ERROR_IN_USE);
  CHECK(ccl_node_set_array(mesh, "used_shaders", CCL_SOCKET_NODE_ARRAY, &mesh, 1) ==
        CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_node_set_array(mesh, "used_shaders", CCL_SOCKET_NODE_ARRAY, &camera, 1) ==
        CCL_ERROR_TYPE_MISMATCH);
  {
    ccl_shader_graph *g = NULL, *g2 = NULL;
    ccl_node *a = NULL, *b = NULL, *output = NULL, *foreign = NULL, *shader = NULL;
    CHECK(ccl_shader_graph_create(s, &g) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_create(other, &g2) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_add(g, "math", "a", &a) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_add(g, "math", "b", &b) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_output(g, &output) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_output(g2, &foreign) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_add(g, "missing", NULL, &b) == CCL_ERROR_NOT_FOUND);
    CHECK(ccl_shader_graph_add(g, "object", NULL, &b) == CCL_ERROR_UNSUPPORTED);
    CHECK(ccl_shader_graph_add(g, "output", NULL, &b) == CCL_ERROR_UNSUPPORTED);
    CHECK(ccl_shader_graph_connect(g, a, "value", b, "value1") == CCL_SUCCESS);
    CHECK(ccl_shader_graph_connect(g, a, "value", b, "value1") == CCL_ERROR_IN_USE);
    CHECK(ccl_shader_graph_connect(g, b, "value", a, "value1") == CCL_ERROR_INVALID_ARGUMENT);
    CHECK(ccl_shader_graph_connect(g, a, "value", foreign, "surface") ==
          CCL_ERROR_INVALID_ARGUMENT);
    CHECK(ccl_shader_graph_connect(g, a, "absent", output, "surface") == CCL_ERROR_NOT_FOUND);
    CHECK(ccl_shader_graph_disconnect(g, b, "value1") == CCL_SUCCESS);
    CHECK(ccl_shader_graph_disconnect(g, b, "value1") == CCL_SUCCESS);
    CHECK(ccl_shader_graph_connect(g, b, "value", a, "value1") == CCL_SUCCESS);
    CHECK(ccl_shader_graph_connect(g, a, "value", output, "surface") == CCL_SUCCESS);
    ccl_node *diffuse = NULL;
    CHECK(ccl_shader_graph_add(g, "diffuse_bsdf", "diffuse", &diffuse) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_connect(g, diffuse, "bsdf", b, "value1") == CCL_ERROR_NOT_FOUND);
    CHECK(ccl_shader_graph_connect(g, diffuse, "BSDF", b, "value1") == CCL_ERROR_TYPE_MISMATCH);
    ccl_socket_id graph_id = 0;
    CHECK(ccl_node_socket_find(a, "value2", &graph_id) == CCL_SUCCESS);
    CHECK(ccl_node_set_float_id(a, graph_id, 0.25f) == CCL_SUCCESS);
    CHECK(ccl_node_delete(s, a) == CCL_ERROR_UNSUPPORTED);
    CHECK(ccl_node_create(s, "shader", "graph_shader", &shader) == CCL_SUCCESS);
    CHECK(ccl_shader_set_graph(shader, g2) == CCL_ERROR_INVALID_ARGUMENT);
    CHECK(ccl_shader_set_graph(shader, g) == CCL_SUCCESS);
    CHECK(ccl_node_set_float_id(a, graph_id, 1.0f) == CCL_ERROR_INVALID_ARGUMENT);
    CHECK(ccl_shader_graph_destroy(g) == CCL_ERROR_INVALID_ARGUMENT);
    CHECK(ccl_shader_graph_create(s, &g) == CCL_SUCCESS);
    CHECK(ccl_shader_graph_add(g, "math", "replacement", &a) == CCL_SUCCESS);
    CHECK(ccl_node_set_float_id(a, graph_id, 2.0f) == CCL_SUCCESS);
    CHECK(ccl_shader_set_graph(shader, g) == CCL_SUCCESS);
    if (argc == 4) {
      ccl_node *osl = NULL;
      ccl_socket_id osl_id = 0;
      float scale = 0;
      CHECK(ccl_shader_graph_create(s, &g) == CCL_SUCCESS);
      CHECK(ccl_shader_graph_add_osl(g, argv[3], "osl", &osl) == CCL_SUCCESS);
      CHECK(ccl_node_socket_find(osl, "Scale", &osl_id) == CCL_SUCCESS);
      CHECK(ccl_node_get_float_id(osl, osl_id, &scale) == CCL_SUCCESS && scale == 0.5f);
      CHECK(ccl_node_set_float_id(osl, osl_id, 2.0f) == CCL_SUCCESS);
      CHECK(ccl_node_get_float(osl, "Scale", &scale) == CCL_SUCCESS && scale == 2.0f);
      CHECK(ccl_shader_graph_output(g, &output) == CCL_SUCCESS);
      CHECK(ccl_shader_graph_connect(g, osl, "BSDF", output, "surface") == CCL_SUCCESS);
      CHECK(ccl_shader_set_graph(shader, g) == CCL_SUCCESS);
      CHECK(ccl_node_get_float_id(osl, osl_id, &scale) == CCL_ERROR_INVALID_ARGUMENT);
      puts("PASS installed C interface OSL load, instance sockets, connection and attachment");
    }
    CHECK(ccl_shader_graph_destroy(g2) == CCL_SUCCESS);
    CHECK(ccl_node_delete(s, shader) == CCL_SUCCESS);
  }
  CHECK(ccl_session_destroy(session) == CCL_SUCCESS);
  CHECK(ccl_session_destroy(other_session) == CCL_SUCCESS);
  puts("PASS plain-C scene ownership, factories, references and name/ID access");
  return 0;
}
