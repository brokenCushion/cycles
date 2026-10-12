/* SPDX-License-Identifier: Apache-2.0 */
/* Private geometry boundary; native topology remains accessible through sockets. */
#include "scene/attribute.h"
#include "scene/image.h"
#include "scene/image_vdb.h"
#include "util/openvdb.h"
#include <climits>
#ifdef WITH_OPENVDB
#  include <openvdb/io/File.h>
#endif
namespace {
template<typename F> ccl_status geometry_access(ccl_node *n, F fn)
{
  return boundary([&]() -> ccl_status {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!valid(n))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid geometry handle");
    if (auto st = scene_access(n->scene, true))
      return st;
    if (!n->native->is_a(ccl::Geometry::get_node_base_type()))
      return wrong_type();
    auto &g = *static_cast<ccl::Geometry *>(n->native);
    if (!g.is_mesh() && !g.is_hair() && !g.is_pointcloud() && !g.is_volume())
      return wrong_type();
    return fn(g);
  });
}
bool fits_count(uint64_t count)
{
  return count <= INT_MAX;
}
const ccl::AttributeElement elements[] = {ccl::ATTR_ELEMENT_NONE,
                                          ccl::ATTR_ELEMENT_OBJECT,
                                          ccl::ATTR_ELEMENT_MESH,
                                          ccl::ATTR_ELEMENT_FACE,
                                          ccl::ATTR_ELEMENT_VERTEX,
                                          ccl::ATTR_ELEMENT_CORNER,
                                          ccl::ATTR_ELEMENT_CURVE,
                                          ccl::ATTR_ELEMENT_CURVE_KEY,
                                          ccl::ATTR_ELEMENT_VOXEL,
                                          ccl::ATTR_ELEMENT_VERTEX_NORMAL,
                                          ccl::ATTR_ELEMENT_CORNER_BYTE,
                                          ccl::ATTR_ELEMENT_CORNER_NORMAL,
                                          ccl::ATTR_ELEMENT_CURVE_KEY_NORMAL};
const ccl::TypeDesc attribute_types[] = {ccl::TypeDesc(),
                                         ccl::TypeFloat,
                                         ccl::TypeFloat2,
                                         ccl::TypeVector,
                                         ccl::TypeColor,
                                         ccl::TypePoint,
                                         ccl::TypeNormal,
                                         ccl::TypeFloat4,
                                         ccl::TypeMatrix,
                                         ccl::TypeRGBA};
struct StandardName {
  const char *name;
  ccl::AttributeStandard value;
};
const StandardName standards[] = {
    {"POSITION", ccl::ATTR_STD_POSITION},
    {"RADIUS", ccl::ATTR_STD_RADIUS},
    {"VERTEX_NORMAL", ccl::ATTR_STD_VERTEX_NORMAL},
    {"CORNER_NORMAL", ccl::ATTR_STD_CORNER_NORMAL},
    {"UV", ccl::ATTR_STD_UV},
    {"UV_TANGENT", ccl::ATTR_STD_UV_TANGENT},
    {"UV_TANGENT_SIGN", ccl::ATTR_STD_UV_TANGENT_SIGN},
    {"UV_TANGENT_UNDISPLACED", ccl::ATTR_STD_UV_TANGENT_UNDISPLACED},
    {"UV_TANGENT_SIGN_UNDISPLACED", ccl::ATTR_STD_UV_TANGENT_SIGN_UNDISPLACED},
    {"VERTEX_COLOR", ccl::ATTR_STD_VERTEX_COLOR},
    {"GENERATED", ccl::ATTR_STD_GENERATED},
    {"GENERATED_TRANSFORM", ccl::ATTR_STD_GENERATED_TRANSFORM},
    {"POSITION_UNDEFORMED", ccl::ATTR_STD_POSITION_UNDEFORMED},
    {"POSITION_UNDISPLACED", ccl::ATTR_STD_POSITION_UNDISPLACED},
    {"NORMAL_UNDISPLACED", ccl::ATTR_STD_NORMAL_UNDISPLACED},
    {"PARTICLE", ccl::ATTR_STD_PARTICLE},
    {"CURVE_INTERCEPT", ccl::ATTR_STD_CURVE_INTERCEPT},
    {"CURVE_LENGTH", ccl::ATTR_STD_CURVE_LENGTH},
    {"CURVE_RANDOM", ccl::ATTR_STD_CURVE_RANDOM},
    {"POINT_RANDOM", ccl::ATTR_STD_POINT_RANDOM},
    {"PTEX_FACE_ID", ccl::ATTR_STD_PTEX_FACE_ID},
    {"PTEX_UV", ccl::ATTR_STD_PTEX_UV},
    {"VOLUME_DENSITY", ccl::ATTR_STD_VOLUME_DENSITY},
    {"VOLUME_COLOR", ccl::ATTR_STD_VOLUME_COLOR},
    {"VOLUME_FLAME", ccl::ATTR_STD_VOLUME_FLAME},
    {"VOLUME_HEAT", ccl::ATTR_STD_VOLUME_HEAT},
    {"VOLUME_TEMPERATURE", ccl::ATTR_STD_VOLUME_TEMPERATURE},
    {"VOLUME_VELOCITY", ccl::ATTR_STD_VOLUME_VELOCITY},
    {"VOLUME_VELOCITY_X", ccl::ATTR_STD_VOLUME_VELOCITY_X},
    {"VOLUME_VELOCITY_Y", ccl::ATTR_STD_VOLUME_VELOCITY_Y},
    {"VOLUME_VELOCITY_Z", ccl::ATTR_STD_VOLUME_VELOCITY_Z},
    {"POINTINESS", ccl::ATTR_STD_POINTINESS},
    {"RANDOM_PER_ISLAND", ccl::ATTR_STD_RANDOM_PER_ISLAND},
    {"SHADOW_TRANSPARENCY", ccl::ATTR_STD_SHADOW_TRANSPARENCY},
};
// Native add(standard) asserts on inapplicable semantics; validate before calling it.
bool standard_supported(ccl::Geometry &g, ccl::AttributeStandard s)
{
  using namespace ccl;
  if (s == ATTR_STD_POSITION || s == ATTR_STD_GENERATED_TRANSFORM)
    return true;
  if (g.is_mesh()) {
    switch (s) {
      case ATTR_STD_VERTEX_NORMAL:
      case ATTR_STD_CORNER_NORMAL:
      case ATTR_STD_UV:
      case ATTR_STD_UV_TANGENT:
      case ATTR_STD_UV_TANGENT_SIGN:
      case ATTR_STD_UV_TANGENT_UNDISPLACED:
      case ATTR_STD_UV_TANGENT_SIGN_UNDISPLACED:
      case ATTR_STD_VERTEX_COLOR:
      case ATTR_STD_GENERATED:
      case ATTR_STD_POSITION_UNDEFORMED:
      case ATTR_STD_POSITION_UNDISPLACED:
      case ATTR_STD_NORMAL_UNDISPLACED:
      case ATTR_STD_PTEX_FACE_ID:
      case ATTR_STD_PTEX_UV:
      case ATTR_STD_POINTINESS:
      case ATTR_STD_RANDOM_PER_ISLAND:
        return true;
      default:
        return false;
    }
  }
  if (g.is_pointcloud())
    return s == ATTR_STD_RADIUS || s == ATTR_STD_UV || s == ATTR_STD_GENERATED ||
           s == ATTR_STD_POINT_RANDOM;
  if (g.is_volume())
    return s == ATTR_STD_VERTEX_NORMAL || s == ATTR_STD_CORNER_NORMAL ||
           (s >= ATTR_STD_VOLUME_DENSITY && s <= ATTR_STD_VOLUME_VELOCITY_Z);
  if (g.is_hair())
    return s == ATTR_STD_RADIUS || s == ATTR_STD_VERTEX_NORMAL || s == ATTR_STD_UV ||
           s == ATTR_STD_GENERATED || s == ATTR_STD_CURVE_INTERCEPT ||
           s == ATTR_STD_CURVE_LENGTH || s == ATTR_STD_CURVE_RANDOM ||
           s == ATTR_STD_SHADOW_TRANSPARENCY;
  return false;
}
ccl_status attribute_layout(ccl::Geometry &g,
                            const ccl_attribute_desc *d,
                            ccl::AttributeSet *&set,
                            ccl::AttributeStandard &standard,
                            ccl::ustring &name)
{
  using namespace ccl;
  if (!d || d->size < sizeof(*d) || (d->subdivision != 0 && d->subdivision != 1) ||
      (d->smooth_fvar != 0 && d->smooth_fvar != 1))
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid attribute descriptor");
  if (d->element < 1 || d->element > 12 || d->type < 1 || d->type > 9)
    return fail(CCL_ERROR_UNSUPPORTED, "Unknown attribute type/element");
  if (d->subdivision && !g.is_mesh())
    return wrong_type();
  set = d->subdivision ? &static_cast<Mesh &>(g).subd_attributes : &g.attributes;
  const auto e = elements[d->element];
  bool applicable = e == ATTR_ELEMENT_OBJECT || e == ATTR_ELEMENT_MESH;
  if (g.is_mesh())
    applicable |= e == ATTR_ELEMENT_FACE || e == ATTR_ELEMENT_VERTEX || e == ATTR_ELEMENT_CORNER ||
                  e == ATTR_ELEMENT_VERTEX_NORMAL || e == ATTR_ELEMENT_CORNER_NORMAL ||
                  e == ATTR_ELEMENT_CORNER_BYTE;
  if (g.is_volume())
    applicable |= e == ATTR_ELEMENT_VOXEL || e == ATTR_ELEMENT_VERTEX ||
                  e == ATTR_ELEMENT_VERTEX_NORMAL || e == ATTR_ELEMENT_FACE;
  if (g.is_pointcloud())
    applicable |= e == ATTR_ELEMENT_VERTEX || e == ATTR_ELEMENT_VERTEX_NORMAL;
  if (g.is_hair())
    applicable |= e == ATTR_ELEMENT_CURVE || e == ATTR_ELEMENT_CURVE_KEY ||
                  e == ATTR_ELEMENT_CURVE_KEY_NORMAL;
  if (!applicable || ((e & ATTR_ELEMENT_IS_NORMAL) && d->type != CCL_ATTRIBUTE_NORMAL) ||
      (e == ATTR_ELEMENT_CORNER_BYTE && d->type != CCL_ATTRIBUTE_RGBA))
    return wrong_type();
  standard = ATTR_STD_NONE;
  if (d->standard && d->standard[0]) {
    auto found = std::find_if(std::begin(standards),
                              std::end(standards),
                              [&](const StandardName &s) { return !strcmp(s.name, d->standard); });
    if (found == std::end(standards) || !standard_supported(g, found->value))
      return fail(CCL_ERROR_UNSUPPORTED, "Unsupported standard for this geometry");
    standard = found->value;
    AttributeSet probe(&g, set->prim);
    const auto *a = probe.add(standard);
    if (a->type != attribute_types[d->type] || a->element != e)
      return wrong_type();
  }
  name = ustring(d->name && d->name[0] ? d->name : Attribute::standard_name(standard));
  if (name.empty())
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Empty custom attribute name");
  const auto *old = set->find(name);
  if (old && old->std != standard)
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Attribute name has a different standard semantic");
  if (standard != ATTR_STD_NONE) {
    const auto *other = set->find(standard);
    if (other && other->name != name)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Standard attribute already has another name");
  }
  return CCL_SUCCESS;
}
size_t payload_size(const ccl_attribute_desc &d)
{
  if (d.element == CCL_ATTRIBUTE_CORNER_BYTE)
    return sizeof(ccl_uchar4);
  switch (d.type) {
    case CCL_ATTRIBUTE_FLOAT:
      return sizeof(float);
    case CCL_ATTRIBUTE_FLOAT2:
      return sizeof(ccl_float2);
    case CCL_ATTRIBUTE_FLOAT4:
    case CCL_ATTRIBUTE_RGBA:
      return sizeof(ccl_float4);
    case CCL_ATTRIBUTE_MATRIX:
      return sizeof(ccl_transform);
    default:
      return sizeof(ccl_float3);
  }
}
}  // namespace
ccl_status CCL_CALL ccl_mesh_resize(ccl_node *node, uint64_t vertices, uint64_t triangles)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    if (!g.is_mesh())
      return wrong_type();
    if (!fits_count(vertices) || triangles > INT_MAX / 3)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Mesh count overflow");
    static_cast<ccl::Mesh &>(g).resize_mesh(int(vertices), int(triangles));
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_mesh_resize_subdivision(ccl_node *node, uint64_t faces, uint64_t corners)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    if (!g.is_mesh())
      return wrong_type();
    if (!fits_count(faces) || !fits_count(corners) || faces > corners / 3)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid subdivision counts");
    static_cast<ccl::Mesh &>(g).resize_subd_faces(int(faces), int(corners));
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_hair_resize(ccl_node *node, uint64_t curves, uint64_t keys)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    if (!g.is_hair())
      return wrong_type();
    if (!fits_count(curves) || !fits_count(keys) || curves > keys / 2)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid curve/key counts");
    static_cast<ccl::Hair &>(g).resize_curves(int(curves), int(keys));
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_pointcloud_resize(ccl_node *node, uint64_t count)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    if (!g.is_pointcloud())
      return wrong_type();
    if (!fits_count(count))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Point count overflow");
    static_cast<ccl::PointCloud &>(g).resize(int(count));
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_geometry_set_attribute(
    ccl_node *node, const ccl_attribute_desc *d, uint32_t step, const void *data, uint64_t count)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    using namespace ccl;
    AttributeSet *set;
    AttributeStandard standard;
    ustring name;
    if (auto st = attribute_layout(g, d, set, standard, name))
      return st;
    if (d->element == CCL_ATTRIBUTE_VOXEL)
      return wrong_type();
    if (!fits_count(count) || (count && !data) ||
        count != Attribute::element_size(&g, elements[d->element], set->prim))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Attribute count does not match geometry");
    /* Native zero means no motion, with one writable center buffer. */
    const uint32_t steps = std::max(1u, g.get_motion_steps());
    if (steps < 1 || steps > Geometry::MAX_MOTION_STEPS || !(steps & 1) || step >= steps)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid motion step/count");
    const auto *old = set->find(name);
    if (step &&
        (!old || old->type != attribute_types[d->type] || old->element != elements[d->element]))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Motion requires matching center attribute");
    if (d->element != CCL_ATTRIBUTE_CORNER_BYTE) {
      const auto *values = static_cast<const float *>(data);
      for (size_t i = 0; i < count * payload_size(*d) / sizeof(float); ++i)
        if (!std::isfinite(values[i]))
          return fail(CCL_ERROR_INVALID_ARGUMENT, "Nonfinite attribute");
    }
    Attribute replacement(name, attribute_types[d->type], elements[d->element], &g, set->prim);
    replacement.std = standard;
    replacement.flags = d->smooth_fvar ? ATTR_SUBDIVIDE_SMOOTH_FVAR : 0;
    const auto bytes = replacement.buffer_size(&g, set->prim);
    if (old && old->type == replacement.type && old->element == replacement.element &&
        old->size == replacement.size)
    {
      if (bytes)
        memcpy(replacement.data_for_write(), old->data(), bytes);
      if (old->has_motion() || step) {
        replacement.add_motion(&g);
        for (uint32_t i = 1; i < steps; ++i)
          if (bytes)
            memcpy(replacement.data_for_write(i),
                   old->data(i < uint32_t(old->num_motion_steps()) ? i : 0),
                   bytes);
      }
    }
    if (replacement.element & ATTR_ELEMENT_IS_NORMAL) {
      auto *dst = replacement.data_for_write<packed_normal>(step);
      const auto *src = static_cast<const ccl_float3 *>(data);
      for (size_t i = 0; i < count; ++i)
        dst[i] = packed_normal(make_float3(src[i].x, src[i].y, src[i].z));
    }
    else if (bytes)
      memcpy(replacement.data_for_write(step), data, bytes);
    set->add_from(std::move(replacement))->flags = d->smooth_fvar ? ATTR_SUBDIVIDE_SMOOTH_FVAR : 0;
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_volume_set_grid(ccl_node *node,
                                        const ccl_attribute_desc *d,
                                        const char *filename,
                                        const char *grid_name,
                                        float clipping)
{
  return geometry_access(node, [&](ccl::Geometry &g) -> ccl_status {
    using namespace ccl;
    if (!g.is_volume())
      return wrong_type();
    AttributeSet *set;
    AttributeStandard standard;
    ustring name;
    if (auto st = attribute_layout(g, d, set, standard, name))
      return st;
    if (d->element != CCL_ATTRIBUTE_VOXEL || d->subdivision || d->smooth_fvar || !filename ||
        !std::filesystem::path(filename).is_absolute() || !grid_name || !grid_name[0] ||
        !std::isfinite(clipping) || clipping < 0)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid VDB grid description/path");
#if defined(WITH_OPENVDB) && defined(WITH_NANOVDB)
    if (!std::filesystem::is_regular_file(filename))
      return fail(CCL_ERROR_NOT_FOUND, "VDB file missing");
    /* Embedding hosts need not have registered OpenVDB grid types. */
    openvdb::initialize();
    openvdb::io::File file(filename);
#  ifdef OPENVDB_USE_DELAYED_LOADING
    file.setCopyMaxBytes(0);
#  endif
    file.open(false);
    if (!file.hasGrid(grid_name))
      return fail(CCL_ERROR_NOT_FOUND, "VDB grid missing");
    auto grid = openvdb_convert_to_known_type(file.readGrid(grid_name));
    file.close();
    if (!grid)
      return fail(CCL_ERROR_UNSUPPORTED, "Unsupported VDB grid type");
    const int channels = openvdb_num_channels(grid);
    if (!((channels == 1 && d->type == CCL_ATTRIBUTE_FLOAT) ||
          (channels == 3 && (d->type == CCL_ATTRIBUTE_COLOR || d->type == CCL_ATTRIBUTE_FLOAT3))))
      return wrong_type();
    auto loader = make_unique<VDBImageLoader>(grid, grid_name, clipping);
    ImageParams params;
    auto image = node->scene->native->image_manager->add_image(std::move(loader), params, false);
    Attribute replacement(name, attribute_types[d->type], ATTR_ELEMENT_VOXEL, &g, set->prim);
    replacement.std = standard;
    replacement.data_voxel_for_write() = image;
    set->add_from(std::move(replacement))->flags = d->smooth_fvar ? ATTR_SUBDIVIDE_SMOOTH_FVAR : 0;
    g.tag_update(node->scene->native, true);
    return CCL_SUCCESS;
#else
    return fail(CCL_ERROR_UNSUPPORTED, "VDB grids require OpenVDB and NanoVDB");
#endif
  });
}
