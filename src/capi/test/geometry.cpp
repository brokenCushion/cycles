/* SPDX-License-Identifier: Apache-2.0 */
#include "../cycles_c.cpp"
#include <fstream>
#undef CHECK
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      abort(); \
    } \
  } while (0)
#include "session_helpers.h"

static ccl_node *node(ccl_scene *scene, const char *type)
{
  ccl_node *n = nullptr;
  CHECK(ccl_node_create(scene, type, type, &n) == CCL_SUCCESS);
  return n;
}
static ccl_attribute_desc descriptor(const char *name,
                                     ccl_attribute_type type,
                                     ccl_attribute_element element,
                                     const char *standard = nullptr)
{
  return {sizeof(ccl_attribute_desc), name, standard, type, element, 0, 0};
}
static void set(
    ccl_node *n, const ccl_attribute_desc &d, const void *data, uint64_t count, uint32_t step = 0)
{
  CHECK(ccl_geometry_set_attribute(n, &d, step, data, count) == CCL_SUCCESS);
}
int main(int argc, char **argv)
{
  CHECK(argc >= 3);
  std::filesystem::create_directories(argv[2]);
  ccl_init_params init{sizeof(init), argv[1], argv[2]};
  CHECK(ccl_init(&init) == CCL_SUCCESS);
  ccl_session_params params{};
  params.size = sizeof(params);
  CHECK(ccl_session_params_default(&params) == CCL_SUCCESS);
  params.device_index = test_device(argc > 3 ? argv[3] : "CPU");
  ccl_session *session;
  CHECK(ccl_session_create(&params, &session) == CCL_SUCCESS);
  ccl_scene *scene;
  CHECK(ccl_session_scene(session, &scene) == CCL_SUCCESS);
  auto *mesh = node(scene, "mesh"), *hair = node(scene, "hair");
  auto *points = node(scene, "pointcloud"), *volume = node(scene, "volume");
  CHECK(ccl_mesh_resize(mesh, 5, 3) == CCL_SUCCESS);
  CHECK(ccl_mesh_resize_subdivision(mesh, 1, 5) == CCL_SUCCESS);
  CHECK(ccl_hair_resize(hair, 2, 6) == CCL_SUCCESS);
  CHECK(ccl_pointcloud_resize(points, 5) == CCL_SUCCESS);
  ccl_float3 positions[6] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 0, 0}, {-1, -1, 0}};
  auto P = descriptor(nullptr, CCL_ATTRIBUTE_POINT, CCL_ATTRIBUTE_VERTEX, "POSITION");
  set(mesh, P, positions, 5);
  set(points, P, positions, 5);
  P.element = CCL_ATTRIBUTE_CURVE_KEY;
  set(hair, P, positions, 6);
  const int32_t triangles[] = {0, 1, 2, 0, 2, 3, 0, 3, 4}, corners[] = {0, 1, 2, 3, 4};
  CHECK(ccl_node_set_array(mesh, "triangles", CCL_SOCKET_INT_ARRAY, triangles, 9) == CCL_SUCCESS);
  CHECK(ccl_node_set_array(mesh, "subd_face_corners", CCL_SOCKET_INT_ARRAY, corners, 5) ==
        CCL_SUCCESS);
  int32_t zero = 0, five = 5;
  for (const char *key : {"subd_start_corner", "subd_shader", "subd_ptex_offset"})
    CHECK(ccl_node_set_array(mesh, key, CCL_SOCKET_INT_ARRAY, &zero, 1) == CCL_SUCCESS);
  CHECK(ccl_node_set_array(mesh, "subd_num_corners", CCL_SOCKET_INT_ARRAY, &five, 1) ==
        CCL_SUCCESS);
  CHECK(ccl_node_set_array(mesh, "subd_smooth", CCL_SOCKET_BOOLEAN_ARRAY, &zero, 1) ==
        CCL_SUCCESS);
  auto subP = descriptor(nullptr, CCL_ATTRIBUTE_POINT, CCL_ATTRIBUTE_VERTEX, "POSITION");
  subP.subdivision = 1;
  set(mesh, subP, positions, 5);
  CHECK(ccl_node_set_enum(mesh, "subdivision_type", ccl::Mesh::SUBDIVISION_CATMULL_CLARK) ==
        CCL_SUCCESS);
  auto *native_mesh = static_cast<ccl::Mesh *>(mesh->native);
  CHECK(native_mesh->get_num_subd_faces() == 1 && native_mesh->get_subd_face(0).num_corners == 5);
  CHECK(memcmp(native_mesh->get_triangles().data(), triangles, sizeof(triangles)) == 0);
  CHECK(memcmp(native_mesh->get_position(), positions, 5 * sizeof(ccl_float3)) == 0);
  int32_t starts[] = {0, 3};
  CHECK(ccl_node_set_array(hair, "curve_first_key", CCL_SOCKET_INT_ARRAY, starts, 2) ==
        CCL_SUCCESS);
  CHECK(static_cast<ccl::Hair *>(hair->native)->get_curve(1).num_keys == 3);

  // All non-voxel elements, every payload type, and subdivision attribute storage.
  for (auto *n : {mesh, hair, points}) {
    auto &g = *static_cast<ccl::Geometry *>(n->native);
    for (int element = 1; element <= 12; ++element) {
      if (element == CCL_ATTRIBUTE_VOXEL)
        continue;
      for (int type = 1; type <= 9; ++type) {
        auto d = descriptor("custom", type, element);
        ccl::AttributeSet *attrs;
        ccl::AttributeStandard standard;
        ccl::ustring name;
        if (attribute_layout(g, &d, attrs, standard, name) != CCL_SUCCESS)
          continue;
        auto count = ccl::Attribute::element_size(&g, elements[element], attrs->prim);
        std::vector<float> values(count * payload_size(d) / sizeof(float), 0.25f);
        set(n, d, values.data(), count);
        auto *a = attrs->find(ccl::ustring("custom"));
        CHECK(a && a->size == count && a->type == attribute_types[type] &&
              a->element == elements[element]);
        if (elements[element] & ccl::ATTR_ELEMENT_IS_NORMAL)
          CHECK(a->data<ccl::packed_normal>()[0] ==
                ccl::packed_normal(ccl::make_float3(.25f, .25f, .25f)));
        else
          CHECK(memcmp(a->data(), values.data(), a->buffer_size(&g, attrs->prim)) == 0);
      }
    }
  }
  float radii[] = {.1f, .2f, .3f, .4f, .5f, .6f};
  auto radius = descriptor(nullptr, CCL_ATTRIBUTE_FLOAT, CCL_ATTRIBUTE_CURVE_KEY, "RADIUS");
  set(hair, radius, radii, 6);
  radius.element = CCL_ATTRIBUTE_VERTEX;
  set(points, radius, radii, 5);
  CHECK(static_cast<ccl::PointCloud *>(points->native)->get_radius()[4] == .5f);
  auto uv = descriptor(nullptr, CCL_ATTRIBUTE_FLOAT2, CCL_ATTRIBUTE_CORNER, "UV");
  uv.subdivision = uv.smooth_fvar = 1;
  ccl_float2 uvs[5] = {};
  set(mesh, uv, uvs, 5);
  uv.smooth_fvar = 0;
  set(mesh, uv, uvs, 5);
  CHECK(native_mesh->subd_attributes.find(ccl::ATTR_STD_UV)->flags == 0);
  CHECK(ccl_node_set_uint(mesh, "motion_steps", 3) == CCL_SUCCESS);
  P.element = CCL_ATTRIBUTE_VERTEX;
  set(mesh, P, positions + 1, 5, 1);
  auto *a = native_mesh->attributes.find(ccl::ATTR_STD_POSITION);
  CHECK(a->num_motion_steps() == 3);
  CHECK(memcmp(a->data(1), positions + 1, 5 * sizeof(ccl_float3)) == 0);
  CHECK(memcmp(a->data(2), positions, 5 * sizeof(ccl_float3)) == 0);
  auto *o1 = node(scene, "object"), *o2 = node(scene, "object");
  CHECK(ccl_node_set_node(o1, "geometry", mesh) == CCL_SUCCESS);
  CHECK(ccl_node_set_node(o2, "geometry", mesh) == CCL_SUCCESS);
  CHECK(static_cast<ccl::Object *>(o1->native)->get_geometry() ==
        static_cast<ccl::Object *>(o2->native)->get_geometry());

  CHECK(ccl_mesh_resize(mesh, UINT64_MAX, 1) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_mesh_resize(points, 2, 1) == CCL_ERROR_TYPE_MISMATCH);
  CHECK(ccl_hair_resize(hair, 10, 1) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_geometry_set_attribute(mesh, &P, 0, positions, 4) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_geometry_set_attribute(mesh, &P, 3, positions, 5) == CCL_ERROR_INVALID_ARGUMENT);
  auto bad = P;
  bad.standard = "PARTICLE";
  CHECK(ccl_geometry_set_attribute(mesh, &bad, 0, positions, 5) == CCL_ERROR_UNSUPPORTED);
  bad = P;
  bad.standard = nullptr;
  bad.name = "P";
  CHECK(ccl_geometry_set_attribute(mesh, &bad, 0, positions, 5) == CCL_ERROR_INVALID_ARGUMENT);
  bad = P;
  bad.type = CCL_ATTRIBUTE_FLOAT;
  CHECK(ccl_geometry_set_attribute(mesh, &bad, 0, radii, 5) == CCL_ERROR_TYPE_MISMATCH);
  CHECK(memcmp(a->data(0), positions, 5 * sizeof(ccl_float3)) == 0);

#if defined(WITH_OPENVDB) && defined(WITH_NANOVDB)
  auto file = (std::filesystem::path(argv[2]) / "tiny.vdb").string();
  auto grid = openvdb::FloatGrid::create(0.0f);
  grid->setName("density");
  grid->getAccessor().setValue(openvdb::Coord(1, 2, 3), .5f);
  openvdb::io::File(file).write(openvdb::GridPtrVec{grid});
  auto density = descriptor(nullptr, CCL_ATTRIBUTE_FLOAT, CCL_ATTRIBUTE_VOXEL, "VOLUME_DENSITY");
  CHECK(ccl_volume_set_grid(volume, &density, file.c_str(), "density", .001f) == CCL_SUCCESS);
  auto *v = static_cast<ccl::Volume *>(volume->native);
  CHECK(v->attributes.find(ccl::ATTR_STD_VOLUME_DENSITY));
  CHECK(ccl_volume_set_grid(volume, &density, file.c_str(), "missing", .001f) ==
        CCL_ERROR_NOT_FOUND);
  CHECK(ccl_volume_set_grid(volume, &density, "relative.vdb", "density", .001f) ==
        CCL_ERROR_INVALID_ARGUMENT);
#endif
  CHECK(ccl_session_destroy(session) == CCL_SUCCESS);
  CHECK(ccl_pointcloud_resize(points, 0) == CCL_ERROR_INVALID_ARGUMENT);
  puts(
      "PASS mesh triangles/ngon subdivision, hair, points, instancing, attribute "
      "domains/types/motion, VDB, validation");
}
