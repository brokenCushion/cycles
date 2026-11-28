/* SPDX-License-Identifier: Apache-2.0 */
#include "session/deep.h"
#include "deep/mesh_topology.h"
#include "deep/volume.h"
#include "scene/background.h"
#include "scene/bake.h"
#include "scene/camera.h"
#include "scene/geometry.h"
#include "scene/hair.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/osl.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_nodes.h"
#include "scene/volume.h"
#include "scene/image.h"
#include "util/log.h"
#include "util/progress.h"
#include "session/session.h"
#include "util/math.h"
#include "util/murmurhash.h"
#include "kernel/deep/types.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef WITH_OSL
#  include <OSL/genclosure.h>
#  include "kernel/osl/types.h"
#endif

CCL_NAMESPACE_BEGIN
static void require_deep(const bool condition, const char *message)
{
  if (!condition)
    throw std::invalid_argument(string("Deep: ") + message);
}

static bool default_normal_link(const ShaderInput *input)
{
  /* Graph simplification fills unconnected closure normals before compilation.
   * Finalization may clear the simplified flag while rewriting closures; both
   * stages must pass preflight when a session is reset. */
  return input->link && (input->flags() & SocketType::LINK_NORMAL) &&
         input->link->parent->type->name == ustring("geometry") &&
         input->link->name() == ustring("Normal");
}

/* Prove a scalar multiple of density along the unscattered camera ray, without
 * changing the shader used by beauty. Ray Depth excludes transparent bounces,
 * so it stays zero along this straight camera-alpha path. */
struct DeepDensityExpression {
  double scale;
  bool grid;
};
static DeepDensityExpression density_expression(ShaderInput *input,
                                                const float constant,
                                                std::set<ShaderNode *> &visited,
                                                const int depth = 0)
{
  require_deep(depth < 64, "density expression is too deep or cyclic");
  if (!input->link) {
    require_deep(std::isfinite(constant) && constant >= 0, "invalid density multiplier");
    return {constant, false};
  }
  ShaderNode *node = input->link->parent;
  visited.insert(node);
  if (node->type->name == ustring("light_path")) {
    require_deep(input->link->name() == ustring("Ray Depth"),
                 "deep camera density only supports Light Path Ray Depth");
    return {0, false};
  }
  if (node->type->name == ustring("attribute")) {
    require_deep(static_cast<AttributeNode *>(node)->get_attribute() == ustring("density") &&
                     input->link->name() == ustring("Fac"),
                 "native deep volume requires the density attribute Fac output");
    return {1, true};
  }
  require_deep(node->type->name == ustring("math"), "unsupported native density expression");
  auto *math = static_cast<MathNode *>(node);
  require_deep(!math->get_use_clamp() &&
                   input->link->name() == ustring("Value"),
               "native deep density requires unclamped scalar math");
  const auto a = density_expression(math->input("Value1"), math->get_value1(), visited, depth + 1);
  const auto b = density_expression(math->input("Value2"), math->get_value2(), visited, depth + 1);
  if (math->get_math_type() == NODE_MATH_ADD || math->get_math_type() == NODE_MATH_POWER) {
    require_deep(!a.grid && !b.grid, "deep density addition and power require camera constants");
    const float value = math->get_math_type() == NODE_MATH_ADD ?
                            float(a.scale) + float(b.scale) :
                            std::pow(float(a.scale), float(b.scale));
    require_deep(std::isfinite(value) && value >= 0, "invalid camera density constant");
    return {value, false};
  }
  require_deep(math->get_math_type() == NODE_MATH_MULTIPLY,
               "unsupported native density math operation");
  require_deep(!(a.grid && b.grid), "nonlinear products of density are unsupported");
  const double scale = a.scale * b.scale;
  require_deep(std::isfinite(scale) && scale <= std::numeric_limits<float>::max(),
               "density multiplier overflow");
  return {scale, a.grid || b.grid};
}

static VolumeNode *scalar_volume_node(ShaderNode *node)
{
  const bool scatter = node->type->name == ustring("scatter_volume");
  require_deep(scatter || node->type->name == ustring("absorption_volume"),
               "deep volume requires absorption_volume or scatter_volume");
  if (scatter) {
    const auto *volume = static_cast<ScatterVolumeNode *>(node);
    require_deep(volume->get_phase() == CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID &&
                     isfinite_safe(volume->get_anisotropy()) &&
                     fabsf(volume->get_anisotropy()) <= 1,
                 "deep scattering requires finite Henyey-Greenstein anisotropy in [-1, 1]");
  }
  auto *volume = static_cast<VolumeNode *>(node);
  const float3 color = volume->get_color();
  require_deep(isfinite_safe(color) && color.x == color.y && color.x == color.z &&
                   color.x >= 0 && color.x <= 1,
               "deep volume requires scalar extinction");
  return volume;
}

static void validate_grid_shader(Scene *scene, Shader *shader)
{
  require_deep(shader && shader->graph, "missing native volume shader");
  auto *output = shader->graph->output();
  require_deep(!output->input("Surface")->link && !output->input("Displacement")->link &&
                   output->input("Volume")->link,
               "native deep grids require a pure volume material");
  ShaderNode *node = output->input("Volume")->link->parent;
  auto *volume = scalar_volume_node(node);
  std::set<ShaderNode *> visited{output, node};
  for (ShaderInput *input : node->inputs)
    if (input->name() == ustring("Anisotropy") && input->link &&
        node->type->name == ustring("scatter_volume"))
    {
      const auto phase = density_expression(input, 0, visited);
      require_deep(!phase.grid && phase.scale <= 1,
                   "deep anisotropy must be a bounded camera constant");
    }
    else {
      require_deep(input->name() == ustring("Density") || !input->link,
                   "native volume supports only linked Density and camera-constant Anisotropy");
    }
  const float3 color = volume->get_color();
  require_deep(shader->get_volume_interpolation_method() == VOLUME_INTERPOLATION_LINEAR,
               "native deep grids require linear interpolation");
  const auto expression = density_expression(volume->input("Density"),
                                              volume->get_density(), visited);
  /* Native graph simplification folds density * 0 to a constant. It remains
   * a valid empty medium; requiring a surviving attribute would reject it on
   * session reset even though its extinction is identically zero. */
  require_deep(expression.grid || expression.scale == 0,
               "native deep volume must use the density grid");
  for (ShaderNode *candidate : shader->graph->nodes)
    require_deep(visited.count(candidate) != 0, "unsupported node in native volume material");
  /* Match native SVM extinction: absorption uses (1-color), scattering color.
   * Phase redistributes light but does not change this scalar extinction. */
  const float scale = float(expression.scale *
                           (node->type->name == ustring("scatter_volume") ? color.x :
                                                                         1.0 - color.x));
  if (shader->deep_density_scale != scale) {
    shader->deep_density_scale = scale;
    shader->tag_update(scene);
  }
}

static float3 homogeneous_extinction(ShaderNode *node,
                                    ShaderNodeSet &visited,
                                    const int depth = 0)
{
  require_deep(depth < 64, "deep volume closure graph is too deep");
  visited.insert(node);
  if (node->type->name == ustring("add_closure")) {
    float3 result = zero_float3();
    for (const char *name : {"Closure1", "Closure2"}) {
      if (node->input(name)->link)
        result += homogeneous_extinction(node->input(name)->link->parent, visited, depth + 1);
    }
    const auto *a = node->input("Closure1")->link;
    const auto *b = node->input("Closure2")->link;
    if (a && b &&
        ((a->parent->type->name == ustring("absorption_volume") &&
          b->parent->type->name == ustring("scatter_volume")) ||
         (b->parent->type->name == ustring("absorption_volume") &&
          a->parent->type->name == ustring("scatter_volume"))))
    {
      const auto *first = static_cast<VolumeNode *>(a->parent);
      const auto *second = static_cast<VolumeNode *>(b->parent);
      /* Equal density and colour cancel exactly: d*(1-c) + d*c = d.
       * Prove this structure instead of accepting genuinely coloured extinction
       * via a relaxed RGB-equality tolerance. Both closures were validated above. */
      if (first->get_density() == second->get_density() && first->get_color() == second->get_color())
        return make_float3(first->get_density());
    }
    return result;
  }
  const bool scatter = node->type->name == ustring("scatter_volume");
  require_deep(scatter || node->type->name == ustring("absorption_volume"),
               "deep homogeneous volume requires additive absorption/scattering");
  for (ShaderInput *input : node->inputs)
    require_deep(!input->link, "deep homogeneous volume inputs must be constant");
  const auto *volume = static_cast<VolumeNode *>(node);
  const float3 color = volume->get_color();
  require_deep(isfinite_safe(color) && min(color.x, min(color.y, color.z)) >= 0 &&
                   max(color.x, max(color.y, color.z)) <= 1 &&
                   std::isfinite(volume->get_density()) && volume->get_density() >= 0,
               "invalid homogeneous volume extinction");
  if (scatter) {
    const auto *phase = static_cast<ScatterVolumeNode *>(node);
    require_deep(phase->get_phase() == CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID &&
                     std::isfinite(phase->get_anisotropy()) && fabsf(phase->get_anisotropy()) <= 1,
                 "unsupported homogeneous scattering phase");
  }
  return (scatter ? color : one_float3() - color) * volume->get_density();
}

static void deep_shader_dependencies(ShaderNodeSet &nodes, ShaderInput *input)
{
  std::vector<ShaderNode *> pending;
  if (input && input->link)
    pending.push_back(input->link->parent);
  while (!pending.empty()) {
    ShaderNode *node = pending.back();
    pending.pop_back();
    if (!nodes.insert(node).second)
      continue;
    for (ShaderInput *dependency : node->inputs)
      if (dependency->link)
        pending.push_back(dependency->link->parent);
  }
}

/* Only transparent closures contribute to camera transmission. A mix of two
 * opaque closures remains opaque, regardless of its (possibly ray-traced)
 * factor. Finalized graphs express these factors as SurfaceMixWeight links. */
static bool deep_closure_can_transmit(ShaderNode *node)
{
  if (node->type->name == ustring("transparent_bsdf"))
    return true;
  if (node->type->name == ustring("principled_bsdf"))
    return node->input("Alpha")->link ||
           static_cast<PrincipledBsdfNode *>(node)->get_alpha() < 1;
  if (node->type->name == ustring("mix_closure") ||
      node->type->name == ustring("add_closure")) {
    for (const char *name : {"Closure1", "Closure2"}) {
      ShaderInput *input = node->input(name);
      if (input->link && deep_closure_can_transmit(input->link->parent))
        return true;
    }
  }
  return false;
}

static bool deep_shader_has_holdout(Shader *shader)
{
  if (!shader || !shader->graph)
    return false; /* Missing graphs still fail normal shader preflight. */
  ShaderNodeSet nodes;
  deep_shader_dependencies(nodes, shader->graph->output()->input("Surface"));
  for (ShaderNode *node : nodes)
    if (node->type->name == ustring("holdout"))
      return true;
  return false;
}

static bool shader_evaluated_volume(const Scene *scene, const DeepSettings &settings,
                                     const Shader *shader)
{
  return settings.volume && shader->graph->output()->input("Volume")->link &&
         (scene->params.shadingsystem == SHADINGSYSTEM_OSL || settings.volume_shader_eval);
}

static void validate_volume_evaluation(Scene *scene, Shader *shader)
{
  ShaderNodeSet nodes;
  deep_shader_dependencies(nodes, shader->graph->output()->input("Volume"));
  for (ShaderNode *node : nodes) {
    /* Script nodes conservatively advertise ray tracing even without trace().
     * Their actual optimized group/loaded bytecode is checked below for OSL. */
    require_deep((scene->params.shadingsystem == SHADINGSYSTEM_OSL ||
                  !(node->get_feature() & KERNEL_FEATURE_NODE_RAYTRACE)) &&
                     node->type->name != ustring("aov_output"),
                 "ray-traced shaders and AOV writes cannot drive deep volume extinction");
    if (scene->params.shadingsystem == SHADINGSYSTEM_SVM && node->type->name == ustring("light_path"))
      for (ShaderOutput *output : node->outputs)
        require_deep(output->links.empty() || output->name() == ustring("Is Camera Ray"),
                     "shader-evaluated volumes only support camera ray-type queries");
  }
}

static void validate_shader(Scene *scene, Shader *shader,
                            const bool background,
                            const bool transparent = false,
                            const bool volume = false,
                            const bool volume_eval = false)
{
  require_deep(shader && shader->graph, "missing shader graph");
  ShaderGraph *graph = shader->graph.get();
  auto *output = graph->output();
  ShaderNodeSet volume_nodes;
  if (volume && !background && output->input("Volume")->link) {
    require_deep(!output->input("Displacement")->link, "deep volume displacement is unsupported");
    if (volume_eval) {
      validate_volume_evaluation(scene, shader);
      deep_shader_dependencies(volume_nodes, output->input("Volume"));
    }
    else {
    const float3 sigma = homogeneous_extinction(output->input("Volume")->link->parent, volume_nodes);
    require_deep(isfinite_safe(sigma) && sigma.x >= 0 && sigma.x == sigma.y && sigma.x == sigma.z,
                 "deep volume requires scalar total extinction");
    if (shader->deep_homogeneous_extinction != sigma.x) {
      shader->deep_homogeneous_extinction = sigma.x;
      shader->tag_update(scene);
    }
    }
    if (!output->input("Surface")->link)
      return;
  }
  require_deep((volume || !output->input("Volume")->link) && !output->input("Displacement")->link,
               "volume and displacement shaders are unsupported");
  require_deep(output->input("Surface")->link != nullptr, "surface shader must be connected");
  if (!background && scene->params.shadingsystem == SHADINGSYSTEM_OSL) {
    /* Optimized groups are checked after scene update, before any camera work.
     * Native SVM keeps its existing opacity dependency proof unchanged. */
    return;
  }
  if (background) {
    /* Environment radiance does not attenuate camera alpha. Keep native beauty
     * evaluation, including textures and colour adjustments, untouched. World
     * volumes were rejected above; only a background closure is admitted. */
    require_deep(output->input("Surface")->link->parent->type->name ==
                     ustring("background_shader"),
                 "deep world requires a background surface closure");
    return;
  }
  ShaderNodeSet opacity_dependencies;
  if (transparent) {
    for (ShaderNode *candidate : graph->nodes) {
      if (deep_closure_can_transmit(candidate)) {
        if (ShaderInput *weight = candidate->input("SurfaceMixWeight"))
          deep_shader_dependencies(opacity_dependencies, weight);
      }
      if (candidate->type->name == ustring("mix_closure") &&
          deep_closure_can_transmit(candidate))
        deep_shader_dependencies(opacity_dependencies, candidate->input("Fac"));
      if (candidate->type->name == ustring("transparent_bsdf"))
        deep_shader_dependencies(opacity_dependencies, candidate->input("Color"));
      if (candidate->type->name == ustring("principled_bsdf"))
        deep_shader_dependencies(opacity_dependencies, candidate->input("Alpha"));
    }
  }
  for (ShaderNode *node : graph->nodes) {
    if (volume_nodes.contains(node))
      continue;
    const string type = node->type->name.string();
    require_deep(type != "hair_info", "Hair Info is unsupported by deep capture");
    if ((graph->simplified || graph->finalized) && type == "geometry") {
      if (transparent)
        continue;
      for (ShaderOutput *socket : node->outputs) {
        for (ShaderInput *input : socket->links) {
          require_deep(default_normal_link(input), "unsupported compiled geometry input");
        }
      }
      continue;
    }
    if (transparent && !background) {
      if (type == "ambient_occlusion" || type == "bevel") {
        /* Without ray-traced shading, AO returns one and Bevel returns sd->N.
         * These fallbacks are safe only outside opacity dependencies. Native
         * beauty retains its full evaluation and unchanged RNG state. */
        require_deep(!opacity_dependencies.contains(node),
                     type == "bevel" ? "ray-traced bevel cannot drive deep opacity" :
                                       "ray-traced ambient occlusion cannot drive deep opacity");
        continue;
      }
      if (type == "light_path") {
        /* Transparent Depth stays rejected: volume capture does not advance that counter. */
        for (ShaderOutput *socket : node->outputs) {
          require_deep(socket->links.empty() || socket->name() == ustring("Is Camera Ray") ||
                           socket->name() == ustring("Is Shadow Ray") ||
                           socket->name() == ustring("Ray Length") ||
                           socket->name() == ustring("Ray Depth") ||
                           socket->name() == ustring("Is Diffuse Ray") || /* Camera visibility stays camera. */
                           socket->name() == ustring("Is Glossy Ray") || /* Transparent continuation retains visibility. */
                           socket->name() == ustring("Is Singular Ray") || /* Camera/transparent flags are not singular. */
                           socket->name() == ustring("Is Reflection Ray") || /* No reflective bounce precedes camera hits. */
                           socket->name() == ustring("Is Transmission Ray") || /* Transparent does not set transmit visibility. */
                           socket->name() == ustring("Is Volume Scatter Ray") || /* Straight capture has no scattering bounce. */
                           socket->name() == ustring("Diffuse Depth") || /* Initialized zero; transparency does not increment it. */
                           socket->name() == ustring("Glossy Depth") || /* Initialized zero; transparency does not increment it. */
                           socket->name() == ustring("Transmission Depth") || /* Transparent is separate from transmission bounces. */
                           socket->name() == ustring("Portal Depth"), /* Portal closures remain rejected, hence zero. */
                       ("unsupported deep surface Light Path output: " + socket->name().string()).c_str());
        }
        continue;
      }
      /* Socket adaptation inserts numeric conversions before simplification.
       * They only convert values supplied to the native shading graph. */
      if (node->special_type == SHADER_SPECIAL_TYPE_AUTOCONVERT) {
        const auto numeric = [](const SocketType::Type type) {
          return type == SocketType::FLOAT || type == SocketType::INT ||
                 type == SocketType::COLOR || type == SocketType::VECTOR ||
                 type == SocketType::POINT || type == SocketType::NORMAL;
        };
        require_deep(node->inputs.size() == 1 && node->outputs.size() == 1 &&
                         numeric(node->inputs[0]->type()) && numeric(node->outputs[0]->type()),
                     "unsupported nonnumeric shader conversion");
        continue;
      }
      /* Finalization expands mix closures into weights. These generated nodes
       * do not add
       * a new closure or alter the qualified surface material set. */
      if (graph->finalized && type == "mix_closure_weight") {
        continue;
      }
      if (graph->finalized && type == "math" &&
          static_cast<MathNode *>(node)->get_math_type() == NODE_MATH_ADD)
      {
        continue;
      }
      require_deep(type == "output" || type == "emission" || type == "diffuse_bsdf" ||
                       type == "principled_bsdf" || type == "glass_bsdf" ||
                       type == "translucent_bsdf" || type == "holdout" ||
                       type == "transparent_bsdf" || type == "mix_closure" ||
                       type == "checker_texture" || type == "texture_coordinate" ||
                       type == "mapping" || type == "noise_texture" ||
                       type == "gradient_texture" || type == "rgb_ramp" ||
                       type == "invert" || type == "mix_color" || type == "bump" ||
                       type == "image_texture" || type == "math" || type == "clamp" || type == "color" ||
                       type == "value" || type == "geometry" || type == "fresnel" ||
                       type == "normal_map" || type == "brightness_contrast" ||
                       type == "hsv" || type == "rgb_curves" || type == "separate_color" ||
                       type == "combine_color" || type == "rgb_to_bw" ||
                       type == "attribute" || type == "wave_texture" || type == "brick_texture" ||
                       type == "glossy_bsdf" || /* Glossy setup allocates BSDFs only; no transparent extinction. */
                       type == "metallic_bsdf" || /* Metallic setup allocates BSDFs only; no transparent extinction. */
                       type == "refraction_bsdf" || /* Refraction redirects a sampled BSDF, but contributes no camera transparency. */
                       type == "sheen_bsdf" || /* Sheen setup does not set transparent extinction. */
                       type == "toon_bsdf" || /* Toon setup does not set transparent extinction. */
                       type == "subsurface_scattering" || /* BSSRDF setup does not set transparent extinction. */
                       type == "hair_bsdf" || /* Hair BSDF setup is opaque to camera alpha; mesh geometry only. */
                       type == "principled_hair_bsdf" || /* Hair randomization reads attributes/inputs, not path RNG; zero transparency. */
                       type == "vector_math" || /* SVM vector arithmetic reads only the input stack. */
                       type == "map_range" || /* SVM scalar range mapping reads only the input stack. */
                       type == "separate_xyz" || /* SVM component extraction reads only the input stack. */
                       type == "combine_xyz" || /* SVM component assembly reads only the input stack. */
                       type == "mix_float" || /* SVM scalar interpolation reads only the input stack. */
                       type == "mix_vector" || /* SVM vector interpolation reads only the input stack. */
                       type == "mix_vector_non_uniform" || /* SVM component-wise interpolation reads only the input stack. */
                       type == "vector_rotate" || /* SVM rotation uses stack vectors and angles, without tracing or RNG. */
                       type == "float_curve" || /* SVM uses immutable curve tables and stack inputs. */
                       type == "vector_curves" || /* SVM uses immutable component curve tables and stack inputs. */
                       type == "normal" || /* SVM normal/dot outputs use the input stack and stored direction. */
                       type == "blackbody" || /* SVM uses deterministic temperature/color-space math. */
                       type == "wavelength" || /* SVM uses deterministic wavelength/color-space math. */
                       type == "vector_transform" || /* SVM uses initialized object/time and camera transforms from the same hit. */
                       type == "add_closure",
                   ("unsupported deep surface node: " + type).c_str());
      continue;
    }
    require_deep(type == "output" || (background ? type == "background_shader" :
                                                   (type == "emission" || type == "diffuse_bsdf" ||
                                                    type == "holdout")),
                 "only constant diffuse/emission surfaces and constant background are supported");
    if (type != "output") {
      for (ShaderInput *input : node->inputs)
        require_deep(!input->link || ((graph->simplified || graph->finalized) && default_normal_link(input)),
                     "material inputs must be constant");
    }
  }
}

static std::set<const Geometry *> deep_camera_geometry(const Scene *scene)
{
  std::set<const Geometry *> geometry;
  for (const Object *object : scene->objects)
    if (object->get_visibility() & PATH_RAY_VISIBILITY_CAMERA)
      if (const Geometry *used = object->get_geometry()) geometry.insert(used);
  return geometry;
}

/* Smallest world-space voxel edge over the shutter. Motion steps decompose to
 * M(t) = R(t) S(t) with S(t) linearly interpolated (transform_motion_array_interpolate),
 * so |M(t) w| = |S(t) w| and its minimum over a segment is the distance from the
 * origin to the segment [S_a w, S_b w]. Rotation and translation do not matter. */
static float deep_shutter_voxel_edge(const array<Transform> &motion, const ImageMetaData &metadata)
{
  vector<DecomposedTransform> decomposed(motion.size());
  transform_motion_decompose(decomposed.data(), motion.data(), motion.size());
  const Transform index = metadata.use_transform_3d ? transform_inverse(metadata.transform_3d) :
                                                      transform_identity();
  double minimum = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i + 1 < decomposed.size(); ++i) {
    Transform scale[2];
    for (int end = 0; end < 2; ++end) {
      DecomposedTransform d = decomposed[i + end];
      d.x = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
      d.y.x = d.y.y = d.y.z = 0.0f;
      transform_compose(&scale[end], &d);
    }
    for (int axis = 0; axis < 3; ++axis) {
      const float3 w = transform_direction(&index, make_float3(axis == 0, axis == 1, axis == 2));
      const float3 a = transform_direction(&scale[0], w), b = transform_direction(&scale[1], w);
      const double ad[3] = {a.x, a.y, a.z}, bd[3] = {b.x, b.y, b.z};
      double ab = 0, aa = 0, dd = 0;
      for (int k = 0; k < 3; ++k) {
        aa += ad[k] * ad[k];
        ab += ad[k] * (bd[k] - ad[k]);
        dd += (bd[k] - ad[k]) * (bd[k] - ad[k]);
      }
      const double s = dd > 0 ? std::clamp(-ab / dd, 0.0, 1.0) : 0.0;
      double length = 0;
      for (int k = 0; k < 3; ++k) {
        const double v = ad[k] + s * (bd[k] - ad[k]);
        length += v * v;
      }
      minimum = std::min(minimum, std::sqrt(length));
    }
  }
  require_deep(std::isfinite(minimum) && minimum > 0, "invalid deep grid voxel step during the shutter");
  /* Round down so the stored FLOAT step never exceeds the real minimum. */
  float edge = float(minimum);
  if (double(edge) > minimum)
    edge = nextafterf(edge, 0.0f);
  return edge;
}

void validate_deep_scene(Scene *scene, SessionParams &params)
{
  require_deep(!params.deep.ids || params.deep.error != 0,
               "--deep-ids requires a numeric --deep-error; strict mode reproduces legacy output without IDs");
  params.deep.object_manifest.clear();
  params.deep.holdout_manifest.clear();
  if (params.deep.ids) {
    std::map<uint32_t, std::string> names;
    for (const Object *object : scene->objects) {
      const std::string name = object->name.c_str();
      const uint32_t id = util_murmur_hash3(name.data(), name.size(), 0);
      const auto inserted = names.emplace(id, name);
      require_deep(inserted.second || inserted.first->second == name,
                   "object-name hash collision in deep ID manifest");
      params.deep.object_manifest.emplace_back(id, name);
      bool holdout = object->get_use_holdout();
      if (const Geometry *geometry = object->get_geometry()) {
        if (geometry->get_used_shaders().empty())
          holdout |= deep_shader_has_holdout(scene->default_surface);
        for (Node *node : geometry->get_used_shaders())
          holdout |= deep_shader_has_holdout(static_cast<Shader *>(node));
      }
      if (holdout)
        params.deep.holdout_manifest.emplace_back(id, name);
    }
  }
  deep::error_budget(params.deep.error);
  require_deep(std::isfinite(params.deep.z_tolerance) && params.deep.z_tolerance >= 0,
               "surface depth tolerance must be finite and nonnegative");
  require_deep(params.deep.samples >= 0, "deep sample limit must be nonnegative");
  require_deep(deep_object_count_valid(scene->objects.size()),
               "scene object count exceeds the 30-bit deep object-index range");
  const bool transparent = params.deep.transparent;
  const bool volume = params.deep.volume;
  params.deep.volume_grid = false;
  params.deep.volume_shader_evaluation = false;
  params.deep.volume_step_min = params.deep.volume_step_max = 0;
  require_deep(std::isfinite(params.deep.volume_step) && params.deep.volume_step >= 0,
               "deep volume step must be finite and nonnegative");
  require_deep(!params.deep.volume_shader_eval || volume,
               "--deep-volume-shader-eval requires --deep-volume");
  for (Geometry *geometry : scene->geometry)
    params.deep.volume_grid |= volume && geometry->geometry_type == Geometry::VOLUME;
  for (Geometry *geometry : scene->geometry)
    for (Node *node : geometry->get_used_shaders())
      params.deep.volume_shader_evaluation |= shader_evaluated_volume(
          scene, params.deep, static_cast<Shader *>(node));
  if (params.deep.volume_shader_evaluation && params.device.type == DEVICE_OPTIX &&
      scene->params.shadingsystem == SHADINGSYSTEM_OSL) {
    LOG_WARNING << "Deep: OSL volume shaders on OptiX can take several minutes to set up. "
                   "CPU or SVM shaders start much faster.";
  }
  params.deep.volume_grid |= params.deep.volume_shader_evaluation;
#ifndef WITH_NANOVDB
  require_deep(!params.deep.volume_shader_evaluation, "shader-evaluated volumes require NanoVDB capture buffers");
#endif
  require_deep(!params.deep.volume_shader_evaluation || params.deep.error > 2e-6f,
               "shader-evaluated volumes require numeric --deep-error greater than 2e-6; strict reproduces legacy analytic output");
  require_deep(params.deep.max_events >= 1 &&
                   params.deep.max_events <= int(params.deep.volume_grid ? DEEP_MAX_VOLUME_EVENTS : DEEP_MAX_EVENTS),
               "invalid deep traversal limit");
  for (Shader *shader : scene->shaders) {
    if (shader->deep_homogeneous_extinction != -1) {
      shader->deep_homogeneous_extinction = -1;
      shader->tag_update(scene);
    }
    if (shader->deep_density_scale != -1) {
      shader->deep_density_scale = -1;
      shader->tag_update(scene);
    }
  }
  require_deep(params.deep.memory_bytes > 0, "working memory budget must be positive");
  if (volume) {
    require_deep((params.device.type == DEVICE_CPU || params.device.type == DEVICE_CUDA ||
                  params.device.type == DEVICE_OPTIX),
                 "deep volumes require CPU, CUDA or OptiX");
    /* Volume depth, clipping, object-space boundaries, grids and the CPU oracle
     * use the camera and object transforms interpolated at each ray's time, so
     * camera and object transform motion are validated below like surfaces. */
    require_deep(scene->camera->get_aperturesize() == 0 && scene->camera->get_nearclip() > 0,
                 "deep volumes require a pinhole camera without depth of field and a positive near clip");
  }
  require_deep((params.device.type == DEVICE_CPU || params.device.type == DEVICE_CUDA ||
                  params.device.type == DEVICE_OPTIX) &&
                   params.background,
               "requires single CPU, CUDA or OptiX background rendering");
  require_deep(params.device.type != DEVICE_CUDA ||
                   scene->params.shadingsystem == SHADINGSYSTEM_SVM,
               "CUDA deep requires native SVM; GPU OSL requires OptiX");
  require_deep(transparent || scene->params.shadingsystem == SHADINGSYSTEM_SVM,
               "OSL is not supported by the M3 material allowlist");
  require_deep(params.samples > 0 && params.samples <= 4096 && !params.use_sample_subset &&
                   params.pixel_size == 1 && params.time_limit == 0 && !params.use_auto_tile,
               "requires 1..4096 maximum samples, full resolution, no time limit or tiling");
  const Integrator *integrator = scene->integrator;
  require_deep(!integrator->get_use_sample_subset(), "sample subsets are unsupported");
  require_deep(!integrator->get_use_guiding() &&
                   !integrator->get_use_custom_pixel_jitter_sample() &&
                   !integrator->get_use_pixel_jitter() && integrator->get_ao_bounces() == 0,
               "guiding, pixel jitter overrides and AO bounces are unsupported");
  require_deep(!integrator->get_use_denoise() || integrator->get_denoiser_upscale_factor() == 1,
               "deep output requires native-resolution denoising without upscaling");
  /* These nonnegative filters are importance-sampled by camera_sample(). Deep
   * captures those exact accepted rays, so each still has unit population weight. */
  require_deep((scene->film->get_filter_type() == FILTER_BOX ||
                scene->film->get_filter_type() == FILTER_GAUSSIAN ||
                scene->film->get_filter_type() == FILTER_BLACKMAN_HARRIS) &&
                   isfinite_safe(scene->film->get_filter_width()) &&
                   scene->film->get_filter_width() > 0,
               "requires a finite positive-width nonnegative camera filter");
  const Camera *camera = scene->camera;
  require_deep((camera->get_camera_type() == CAMERA_PERSPECTIVE ||
                    (!volume && camera->get_camera_type() == CAMERA_ORTHOGRAPHIC)) &&
                   !camera->get_use_perspective_motion() &&
                   camera->get_stereo_eye() == Camera::STEREO_NONE &&
                   !camera->get_use_spherical_stereo() && camera->script_name.empty(),
               "requires mono perspective or surface orthographic camera without animated field of view");
  require_deep(camera->get_rolling_shutter_type() == Camera::ROLLING_SHUTTER_NONE,
               "rolling shutter is unsupported");
  if (integrator->get_motion_blur()) {
    require_deep(isfinite_safe(camera->get_shuttertime()) && camera->get_shuttertime() > 0,
                 "motion blur requires a positive finite shutter duration");
    for (const float weight : camera->get_shutter_curve())
      require_deep(isfinite_safe(weight) && weight == 1.0f,
                   "deep motion currently requires a uniform shutter curve");
  }
  /* Rigid motion (the camera) keeps unit scale and no shear, and each
   * step keeps the handedness of the static matrix. Blender cameras are
   * reflected (blender_camera_matrix flips Z), so a positive determinant cannot
   * be required. transform_motion_decompose stores a reflection as scale -1 and
   * interpolates it unchanged; ray generation and deep_camera_depth share that
   * interpolated matrix. Surface, hair and volume objects may use any finite,
   * non-degenerate, unreflected affine motion: surface depth is taken from the
   * ray-time hit position, and volumes are traversed in object space with the
   * ray-time inverse, so scale needs no depth change. */
  const auto validate_motion = [&](const array<Transform> &motion,
                                   const Transform &current,
                                   const bool rigid) {
    /* Motion-vector/denoising passes store adjacent-frame transforms even
     * without motion blur. They do not move the accepted camera rays. */
    if (!integrator->get_motion_blur())
      return;
    /* Blender initializes camera motion slots even for a static render.
     * Camera::update likewise treats copies of the current transform as static. */
    bool have_motion = false;
    for (const Transform &tfm : motion)
      have_motion |= tfm != current;
    if (!have_motion)
      return;
    require_deep(integrator->get_motion_blur() && motion.size() >= 2,
                 "motion transforms require enabled motion blur and at least two steps");
    const float handedness = dot(make_float3(current.x.x, current.x.y, current.x.z),
                                 cross(make_float3(current.y.x, current.y.y, current.y.z),
                                       make_float3(current.z.x, current.z.y, current.z.z)));
    for (const Transform &tfm : motion) {
      const float3 x = make_float3(tfm.x.x, tfm.x.y, tfm.x.z);
      const float3 y = make_float3(tfm.y.x, tfm.y.y, tfm.y.z);
      const float3 z = make_float3(tfm.z.x, tfm.z.y, tfm.z.z);
      if (!rigid) {
        /* Rows of the 3x3 part; the determinant is the same for rows or columns.
         * Normalizing by the row lengths rejects near-singular (flattened) frames. */
        const float lx = len(x), ly = len(y), lz = len(z);
        const float determinant = dot(x, cross(y, z));
        require_deep(isfinite_safe(tfm.x) && isfinite_safe(tfm.y) && isfinite_safe(tfm.z) &&
                         std::isfinite(lx * ly * lz) && lx > 0 && ly > 0 && lz > 0 &&
                         std::isfinite(determinant) && determinant > 0 &&
                         determinant > 1e-6f * lx * ly * lz,
                     "deep object motion requires finite non-degenerate transforms without "
                     "reflection");
        continue;
      }
      require_deep(isfinite_safe(tfm.x) && isfinite_safe(tfm.y) && isfinite_safe(tfm.z) &&
                       fabsf(dot(x, x) - 1) < 1e-4f && fabsf(dot(y, y) - 1) < 1e-4f &&
                       fabsf(dot(z, z) - 1) < 1e-4f && fabsf(dot(x, y)) < 1e-4f &&
                       fabsf(dot(x, z)) < 1e-4f && fabsf(dot(y, z)) < 1e-4f &&
                       std::isfinite(handedness) && handedness != 0 &&
                       (dot(x, cross(y, z)) > 0) == (handedness > 0),
                   "deep motion requires finite rigid camera transforms without scale, "
                   "shear or a handedness change during the shutter");
    }
  };
  validate_motion(camera->get_motion(), camera->get_matrix(), true);
  require_deep(isfinite_safe(camera->get_aperturesize()) && camera->get_aperturesize() >= 0 &&
                   isfinite_safe(camera->get_aperture_ratio()) &&
                   camera->get_aperture_ratio() > 0 &&
                   isfinite_safe(camera->get_bladesrotation()) &&
                   isfinite_safe(camera->get_focaldistance()) &&
                   (camera->get_aperturesize() == 0 || camera->get_focaldistance() > 0),
               "invalid aperture size, ratio, rotation or focal distance");
  require_deep(camera->get_nearclip() >= 0 && camera->get_farclip() > camera->get_nearclip() &&
                   isfinite_safe(camera->get_nearclip()) && isfinite_safe(camera->get_farclip()) &&
                   isfinite_safe(camera->get_fov()) && camera->get_fov() > 0 &&
                   camera->get_fov() < M_PI_F,
               "invalid camera clipping or field of view");
  require_deep(camera->border.left == 0 && camera->border.bottom == 0 &&
                   camera->border.right == 1 && camera->border.top == 1,
               "camera borders are unsupported");
  require_deep(!scene->bake_manager->get_baking() && scene->procedurals.empty(),
               "baking and procedural geometry are unsupported");
  require_deep(!scene->background->get_transparent_glass(), "transparent glass is unsupported");
  validate_shader(scene, scene->background->get_shader() ? scene->background->get_shader() :
                                                    scene->default_background,
                  true);
  const auto camera_geometry = deep_camera_geometry(scene);
  /* Open deep volume boundaries, with their boundary-edge counts. Warned per
   * object below; they stay supported (an open interval ends at the clip end). */
  std::map<const Geometry *, size_t> open_volume_meshes;
  std::set<const Geometry *> intersecting_volume_meshes;
  for (Geometry *geometry : scene->geometry) {
    /* Camera-only deep traversal cannot record geometry outside this set. */
    if (!camera_geometry.count(geometry))
      continue;
    /* Analytic lights add radiance but do not attenuate camera alpha (see
     * integrator_shade_light_forward). They are not deep visibility surfaces. */
    if (geometry->is_light())
      continue;
    if (volume && geometry->geometry_type == Geometry::VOLUME) {
      /* Blender never deforms a grid's boundary (object transform motion only).
       * Velocity-grid motion advects density during the shutter; deep does not. */
      bool velocity = false;
      for (const AttributeStandard standard : {ATTR_STD_VOLUME_VELOCITY, ATTR_STD_VOLUME_VELOCITY_X,
                                          ATTR_STD_VOLUME_VELOCITY_Y, ATTR_STD_VOLUME_VELOCITY_Z})
        velocity |= geometry->attributes.find(standard) != nullptr;
      require_deep(!scene->integrator->get_motion_blur() || !velocity ||
                       static_cast<Volume *>(geometry)->get_velocity_scale() == 0.0f,
                   "deep volumes do not support velocity-grid motion blur: set the volume's "
                   "Velocity Scale to 0 or remove its velocity grid");
      require_deep(!geometry->has_motion_blur() && geometry->get_used_shaders().size() == 1,
                   "native deep grids require static geometry with one material");
      auto *shader = static_cast<Shader *>(geometry->get_used_shaders()[0]);
      if (shader_evaluated_volume(scene, params.deep, shader))
        validate_shader(scene, shader, false, transparent || volume, volume, true);
      else
        validate_grid_shader(scene, shader);
      continue;
    }
    require_deep(geometry->geometry_type != Geometry::POINTCLOUD,
                 "point clouds are unsupported by deep capture");
    if (geometry->geometry_type == Geometry::HAIR) {
      require_deep(!volume,
                   "hair requires Deep Volume off for this layer (View Layer > Deep > Volume: Off)");
      require_deep(params.deep.z_tolerance == 0,
                   "hair requires Surface Depth Tolerance 0 for this layer");
      require_deep(static_cast<Hair *>(geometry)->curve_shape == CURVE_RIBBON,
                   "deep hair requires Ribbon shape; THICK and THICK_LINEAR are unsupported");
      /* Moving and deforming Ribbon hair: intersection (motion curve keys),
       * curve shader setup and depth use the ray time, as for motion triangles.
       * Camera motion is validated separately; object motion uses the surface rule. */
      require_deep(scene->params.shadingsystem == SHADINGSYSTEM_SVM,
                   "OSL on hair is unsupported by deep capture");
    }
    else {
      /* Deforming meshes: intersection, shading and depth use the ray time
       * (motion triangles), so deep records each ray's shutter-time surface. */
      require_deep(geometry->geometry_type == Geometry::MESH,
                   ("only polygon meshes and background lighting are supported: " + geometry->name.string()).c_str());
      require_deep(static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE,
                   "subdivision is unsupported");
    }
    if (geometry->get_used_shaders().empty())
      validate_shader(scene, scene->default_surface, false, transparent);
    for (Node *shader : geometry->get_used_shaders())
      validate_shader(scene, static_cast<Shader *>(shader), false, transparent || volume, volume,
                      shader_evaluated_volume(scene, params.deep, static_cast<Shader *>(shader)));
    if (volume) {
      bool has_volume = false;
      for (Node *shader : geometry->get_used_shaders())
        has_volume |= static_cast<Shader *>(shader)->graph->output()->input("Volume")->link !=
                      nullptr;
      if (has_volume) {
        /* deep_volume_object reads the boundary's static object-space vertices;
         * object transform motion is supported, deformation is not. */
        require_deep(!geometry->has_motion_blur(),
                     "deep volume meshes must not deform during the shutter; object transform motion "
                     "is supported, deforming volume boundaries are not");
        const Mesh *mesh = static_cast<Mesh *>(geometry);
        require_deep(geometry->get_used_shaders().size() == 1 && mesh->num_triangles() >= 1,
                     "deep volume mesh must have one material and a triangle boundary");
        const auto *vertices = mesh->get_position();
        std::map<std::pair<int, int>, int> directed_edges;
        double signed_volume = 0;
        for (size_t i = 0; i < mesh->num_triangles(); ++i) {
          const auto tri = mesh->get_triangle(i);
          for (int j = 0; j < 3; ++j) {
            require_deep(tri.v[j] >= 0 && size_t(tri.v[j]) < mesh->num_verts(),
                         "invalid volume vertex index");
            require_deep(++directed_edges[{tri.v[j], tri.v[(j + 1) % 3]}] == 1,
                         "M8 volume boundary must be consistently oriented and manifold");
          }
          const float3 a = vertices[tri.v[0]], b = vertices[tri.v[1]], c = vertices[tri.v[2]];
          const float3 n = cross(b - a, c - a);
          signed_volume += double(dot(a, cross(b, c)));
          require_deep(isfinite_safe(a) && isfinite_safe(b) && isfinite_safe(c) && len(n) > 0,
                       "invalid volume triangle");
        }
        bool closed = true;
        for (const auto &edge : directed_edges)
          closed &= directed_edges.count({edge.first.second, edge.first.first}) == 1;
        require_deep(std::isfinite(signed_volume) && (!closed || signed_volume > 0),
                     "closed deep volume mesh must enclose positive volume");
        if (closed && deep::mesh_has_proper_self_intersection(
                          mesh->get_triangles().data(), mesh->num_triangles(),
                          &vertices[0].x, sizeof(vertices[0]) / sizeof(float)))
          intersecting_volume_meshes.insert(geometry);
        if (!closed) {
          /* Diagnostics only. Split vertices are welded by exact position so
           * that only real holes are reported. */
          const size_t boundary = deep::open_mesh_boundary_edges(
              mesh->get_triangles().data(), mesh->num_triangles(), &vertices[0].x,
              sizeof(vertices[0]) / sizeof(float));
          if (boundary)
            open_volume_meshes[geometry] = boundary;
        }
      }
    }
  }
  for (Object *object : scene->objects) {
    const bool self_intersects = intersecting_volume_meshes.count(object->get_geometry()) != 0;
    if (object->deep_volume_self_intersects != self_intersects) {
      object->deep_volume_self_intersects = self_intersects;
      object->tag_update(scene);
    }
    if (self_intersects && (object->get_visibility() & PATH_RAY_VISIBILITY_CAMERA))
      LOG_WARNING << "Deep: volume mesh \"" << object->name
                  << "\" self-intersects; capture uses the outermost volume crossings.";
  }
  if (!open_volume_meshes.empty()) {
    std::set<string> warned;
    for (const Object *object : scene->objects) {
      const auto open = open_volume_meshes.find(object->get_geometry());
      if (open == open_volume_meshes.end() ||
          !(object->get_visibility() & PATH_RAY_VISIBILITY_CAMERA))
        continue;
      const string name = object->name.empty() ? open->first->name.string() :
                                                 object->name.string();
      if (warned.insert(name).second)
        LOG_WARNING << "Deep: volume mesh \"" << name << "\" is open (" << open->second
                    << " boundary edges); rays that leave through the hole keep the volume to "
                       "the camera clip end. Close the mesh or put an opaque surface behind it.";
    }
  }
  /* Reuse film padding: deep gets a scene bound without changing any beauty
   * constant offsets or kernel layout. Film updates leave this slot untouched. */
  int volume_objects = 0;
  for (Object *object : scene->objects)
    if (object->get_geometry())
      for (Node *node : object->get_geometry()->get_used_shaders())
        if (static_cast<Shader *>(node)->graph->output()->input("Volume")->link) {
          ++volume_objects;
          break;
        }
  scene->dscene.data.film.pad1 = std::max(1, std::min(volume_objects, int(DEEP_MAX_MEDIA)));
  /* Deep-only padding: constant offsets and beauty kernel layouts stay fixed. */
  scene->dscene.data.pad2 = __float_as_int(params.deep.error / 2);
  const char *fixed = std::getenv("CYCLES_DEEP_VOLUME_FIXED_STEP");
  params.deep.volume_shader_fixed_step = fixed && string(fixed) == "1";
  scene->dscene.data.pad3 = params.deep.volume_shader_fixed_step ? 1 : 0;
  /* Store deep-only selection and step in existing shader slots, preserving
   * KernelShader size/beauty constant offsets. Shared materials take the finest
   * step of their objects. Metadata states the actual material-step range. */
  std::map<Shader *, float> volume_steps;
  Progress metadata_progress;
  for (Object *object : scene->objects) {
    Geometry *geometry = object->get_geometry();
    if (!geometry) continue;
    for (Node *node : geometry->get_used_shaders()) {
      auto *shader = static_cast<Shader *>(node);
      if (!shader_evaluated_volume(scene, params.deep, shader)) continue;
      float step = params.deep.volume_step > 0 ? params.deep.volume_step : FLT_MAX;
      if (geometry->is_volume()) {
        for (Attribute &attr : static_cast<Volume *>(geometry)->attributes.attributes) {
          if (attr.element != ATTR_ELEMENT_VOXEL) continue;
          const ImageMetaData metadata = attr.data_voxel_for_write().metadata(metadata_progress);
          if (!metadata.nanovdb_byte_size) continue;
          const Transform voxel = metadata.use_transform_3d ?
              object->get_tfm() * transform_inverse(metadata.transform_3d) : object->get_tfm();
          const float edge = std::min({len(transform_direction(&voxel, make_float3(1, 0, 0))),
                                      len(transform_direction(&voxel, make_float3(0, 1, 0))),
                                      len(transform_direction(&voxel, make_float3(0, 0, 1)))});
          require_deep(std::isfinite(edge) && edge > 0, "invalid deep grid voxel step");
          step = std::min(step, edge);
          if (scene->integrator->get_motion_blur() && object->use_motion())
            step = std::min(step, deep_shutter_voxel_edge(object->get_motion(), metadata));
        }
      }
      require_deep(step > 0 && step < FLT_MAX && std::isfinite(step),
                   "shader-evaluated volume without a grid requires --deep-volume-step in world units");
      auto [entry, inserted] = volume_steps.emplace(shader, step);
      if (!inserted) entry->second = std::min(entry->second, step);
    }
  }
  for (const auto &[shader, step] : volume_steps) {
    shader->deep_density_scale = -2;
    shader->deep_homogeneous_extinction = -step;
    shader->tag_update(scene);
    params.deep.volume_step_min = params.deep.volume_step_min ?
                                     std::min(params.deep.volume_step_min, step) : step;
    params.deep.volume_step_max = std::max(params.deep.volume_step_max, step);
  }
  for (Object *object : scene->objects) {
    if (!(object->get_visibility() & PATH_RAY_VISIBILITY_CAMERA))
      continue;
    if (volume) {
      const auto &tfm = object->get_tfm();
      const float determinant = dot(make_float3(tfm.x.x, tfm.x.y, tfm.x.z),
                                    cross(make_float3(tfm.y.x, tfm.y.y, tfm.y.z),
                                          make_float3(tfm.z.x, tfm.z.y, tfm.z.z)));
      bool has_volume = false;
      if (object->get_geometry())
        for (Node *node : object->get_geometry()->get_used_shaders())
          has_volume |= static_cast<Shader *>(node)->graph->output()->input("Volume")->link != nullptr;
      require_deep(isfinite_safe(tfm.x) && isfinite_safe(tfm.y) && isfinite_safe(tfm.z) &&
                       std::isfinite(determinant) && determinant != 0 &&
                       (!has_volume || determinant > 0),
                   "deep requires finite nonsingular transforms and unreflected volumes");
    }
    /* Every object uses the non-rigid rule. Surface and ribbon depth come from
     * the ray-time hit position; volumes map each ray into object space with the
     * ray-time inverse, where boundaries, grids and depth stay affine in the ray
     * parameter. Scale changes the voxel step; see the shutter minimum above. */
    validate_motion(object->get_motion(), object->get_tfm(), false);
    /* Blender stores shadow-catcher flags on analytic lights as well. These
     * objects have no surface closure and cannot create deep opacity events. */
    if (object->get_geometry() && object->get_geometry()->is_light())
      continue;
    /* Holdout affects beauty colour/alpha, not the straight camera opacity
     * already recorded by capture. Preserve the legacy rejection diagnostic
     * for shadow catchers and caustics; their support has not changed. */
    require_deep(!object->get_is_shadow_catcher() &&
                     !object->get_is_caustics_caster() && !object->get_is_caustics_receiver(),
                 "holdout, shadow catcher and caustics are unsupported");
  }
}


/* Query the groups OSL actually optimized; neither execute them nor specialize
 * their ray types for deep. Beauty keeps its original group and callables. */
void validate_deep_osl(Scene *scene)
{
#ifdef WITH_OSL
  if (scene->params.shadingsystem != SHADINGSYSTEM_OSL) return;
  const auto camera_geometry = deep_camera_geometry(scene);
  std::set<Shader *> used;
  for (Geometry *geometry : scene->geometry) {
    if (geometry->is_light() || !camera_geometry.count(geometry)) continue;
    if (geometry->get_used_shaders().empty()) used.insert(scene->default_surface);
    for (Node *node : geometry->get_used_shaders()) used.insert(static_cast<Shader *>(node));
  }
  OSLManager::foreach_osl_device(scene->device, [&](Device *device, OSLGlobals *) {
    OSL::ShadingSystem *ss = scene->osl_manager->get_shading_system(device);
    for (Shader *shader : used) {
      const string prefix = "OSL material '" + shader->name.string() + "': ";
      const auto require = [&](bool ok, const string &reason) {
        require_deep(ok, (prefix + reason).c_str());
      };
      const auto found = shader->osl_cache.find(device);
      require(found != shader->osl_cache.end(), "missing compiled shader groups");
      require(!shader->has_surface || found->second.surface, "missing compiled surface group");
      require(!shader->has_volume || found->second.volume, "missing compiled volume group");
      for (OSL::ShaderGroup *group : {found->second.surface.get(), found->second.volume.get()}) {
        if (!group) continue;
        if (device->info.type == DEVICE_OPTIX) {
          string ptx;
          require(ss->getattribute(group, "ptx_compiled_version", OSL::TypeDesc::PTR, &ptx) && !ptx.empty(),
                  "compiled OptiX shader group has no PTX; native OSL cannot evaluate this shader");
        }
      const auto integer = [&](const char *name) {
        int value = 0;
        require(ss->getattribute(group, name, value), string("unavailable OSL query ") + name);
        return value;
      };
      const auto names = [&](const char *count, const char *attribute) {
        const int n = integer(count);
        require(n >= 0, "invalid OSL query count");
        OSL::ustring *data = nullptr;
        require(ss->getattribute(group, attribute, OSL::TypeDesc::PTR, &data) && (!n || data),
                string("unavailable OSL query ") + attribute);
        return n ? std::vector<OSL::ustring>(data, data + n) : std::vector<OSL::ustring>();
      };
      require(!(integer("raytype_queries") & ~ss->raytype_bit(ustring("camera"))),
              "ray-type queries other than camera are unsupported");
        require(!integer("unknown_attributes_needed"), "dynamic/unknown attribute queries are unsupported");
        require(!integer("unknown_textures_needed"), "dynamic/unknown texture queries are unsupported");
        for (const auto &name : names("num_textures_needed", "textures_needed"))
          require(name != ustring("@bevel") && name != ustring("@ao"),
                  "ray-traced " + name.string() + " texture is unsupported");
      const auto stable_attribute = [&](const string &name) {
        if (name.rfind("geom:", 0) == 0 && Attribute::name_standard(name.c_str()+5) != ATTR_STD_NONE)
          return true;
        static const std::set<string> known = {"object:location", "object:color", "object:alpha",
          "object:index", "object:random", "material:index", "geom:name", "geom:is_smooth",
          "geom:num_polyvertices", "geom:trianglevertices", "geom:polyvertices", "scene:time", "scene:frame"};
        if (known.count(name)) return true;
        bool present = false;
        for (Geometry *geometry : scene->geometry) {
          if (!camera_geometry.count(geometry)) continue;
          if (!geometry->is_mesh() && !geometry->is_volume()) continue;
          const auto &shaders = geometry->get_used_shaders();
          if (std::find(shaders.begin(), shaders.end(), shader) == shaders.end()) continue;
          if (!static_cast<Mesh *>(geometry)->attributes.find(ustring(name))) return false;
          present = true;
        }
        return present;
      };
      const auto attributes = names("num_attributes_needed", "attributes_needed");
      OSL::ustring *scopes = nullptr;
      require(ss->getattribute(group, "attribute_scopes", OSL::TypeDesc::PTR, &scopes) &&
                  (attributes.empty() || scopes), "unavailable attribute scope query");
      for (size_t i = 0; i < attributes.size(); ++i) {
        const string name = attributes[i].string();
        require(scopes[i].empty() && name.rfind("path:", 0) != 0 && stable_attribute(name),
                "disallowed attribute '" + name + "'");
      }
      for (const auto &name : names("num_userdata", "userdata_names"))
        require(stable_attribute(name.string()), "unknown userdata '" + name.string() + "'");
      static const std::set<string> globals = {"P", "I", "N", "Ng", "u", "v", "dPdu", "dPdv",
        "time", "dtime", "dPdx", "dPdy", "dIdx", "dIdy", "Ci", "surfacearea", "backfacing", "flipHandedness"};
      for (const auto &name : names("num_globals_needed", "globals_needed"))
        require(globals.count(name.string()), "unsupported global '" + name.string() + "'");
      require(!integer("unknown_closures_needed"), "unknown closure types are unsupported");
      size_t component_size = sizeof(OSLClosureComponent), alignment = alignof(OSLClosureComponent);
      for (const auto &name : names("num_closures_needed", "closures_needed")) {
        require(name != ustring("layer"), "nested closure layering is unsupported by the bounded traversal");
        const char *closure_name = name.c_str(); int id = 0;
        const OSL::ClosureParam *params = nullptr;
        require(ss->query_closure(&closure_name, &id, &params) && params, "unknown closure '" + name.string() + "'");
        while (params->type != OSL::TypeDesc()) ++params;
        require(params->offset >= 0 && params->field_size > 0, "invalid closure layout");
        component_size = std::max(component_size, sizeof(OSLClosureComponent) + size_t(params->offset));
        alignment = std::max(alignment, size_t(params->field_size));
      }
      const deep::OSLFeatures &f = shader->deep_osl_features;
      require(f.unsupported.empty(), "unsupported operation '" + f.unsupported + "'");
      require(!f.loop || !(f.components || f.muls || f.adds), "cannot bound closure-building loops");
      require(f.adds < 16, "closure traversal exceeds the existing 16-entry stack");
      /* Every component/mul/add executes at most once (branches count both
       * sides). Charge alignment-1 to EVERY allocation, including the first.
       * Optimization may remove allocations, never increase this upper bound. */
      require(f.components <= 1024 && f.muls <= 1024 && f.adds <= 1024, "closure arena capacity exceeded");
      /* Weighted-closure folding may replace a mul with a component. Charge
       * the largest registered component/operator to every closure operation. */
      component_size = std::max(component_size, std::max(sizeof(OSLClosureMul), sizeof(OSLClosureAdd)));
      alignment = std::max(alignment, std::max(alignof(OSLClosureMul), alignof(OSLClosureAdd)));
      const size_t bytes = (f.components + f.muls + f.adds) * (component_size + alignment - 1);
      require(bytes <= 1024, "closure allocation bound " + std::to_string(bytes) + " exceeds the existing 1024-byte GPU arena");
      }
    }
  });
#else
  require_deep(scene->params.shadingsystem != SHADINGSYSTEM_OSL, "OSL support was not built");
#endif
}

CCL_NAMESPACE_END
