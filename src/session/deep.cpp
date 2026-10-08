/* SPDX-License-Identifier: Apache-2.0 */
#include "session/deep.h"
#include "deep/volume.h"
#include "scene/background.h"
#include "scene/bake.h"
#include "scene/camera.h"
#include "scene/geometry.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/osl.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_nodes.h"
#include "session/session.h"
#include "util/math.h"
#include "util/murmurhash.h"
#include "kernel/deep/types.h"

#include <algorithm>
#include <cmath>
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

static void validate_shader(Scene *scene, Shader *shader,
                            const bool background,
                            const bool transparent = false,
                            const bool volume = false)
{
  require_deep(shader && shader->graph, "missing shader graph");
  ShaderGraph *graph = shader->graph.get();
  auto *output = graph->output();
  ShaderNodeSet volume_nodes;
  if (volume && !background && output->input("Volume")->link) {
    require_deep(!output->input("Displacement")->link, "deep volume displacement is unsupported");
    const float3 sigma = homogeneous_extinction(output->input("Volume")->link->parent, volume_nodes);
    require_deep(isfinite_safe(sigma) && sigma.x >= 0 && sigma.x == sigma.y && sigma.x == sigma.z,
                 "deep volume requires scalar total extinction");
    if (shader->deep_homogeneous_extinction != sigma.x) {
      shader->deep_homogeneous_extinction = sigma.x;
      shader->tag_update(scene);
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
        for (ShaderOutput *socket : node->outputs) {
          require_deep(socket->links.empty() || socket->name() == ustring("Is Camera Ray") ||
                           socket->name() == ustring("Is Shadow Ray") ||
                           socket->name() == ustring("Ray Length") ||
                           socket->name() == ustring("Ray Depth"),
                       "unsupported deep surface Light Path output");
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
  for (Geometry *geometry : scene->geometry)
    params.deep.volume_grid |= volume && geometry->geometry_type == Geometry::VOLUME;
  require_deep(params.deep.max_events >= 1 &&
                   params.deep.max_events <= int(params.deep.volume_grid ? DEEP_MAX_VOLUME_EVENTS : DEEP_MAX_EVENTS),
               "invalid deep traversal limit");
  for (Shader *shader : scene->shaders) {
    if (shader->deep_homogeneous_extinction >= 0) {
      shader->deep_homogeneous_extinction = -1;
      shader->tag_update(scene);
    }
    if (shader->deep_density_scale >= 0) {
      shader->deep_density_scale = -1;
      shader->tag_update(scene);
    }
  }
  require_deep(params.deep.memory_bytes > 0, "working memory budget must be positive");
  if (volume) {
    require_deep((params.device.type == DEVICE_CPU || params.device.type == DEVICE_CUDA ||
                  params.device.type == DEVICE_OPTIX) &&
                     scene->params.shadingsystem == SHADINGSYSTEM_SVM,
                 "deep volumes require CPU, CUDA or OptiX native SVM");
    require_deep(!scene->integrator->get_motion_blur() &&
                     scene->camera->get_aperturesize() == 0 && scene->camera->get_nearclip() > 0,
                 "deep volumes require static pinhole camera and positive near clip");
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
  const auto validate_motion = [&](const array<Transform> &motion, const Transform &current) {
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
    for (const Transform &tfm : motion) {
      const float3 x = make_float3(tfm.x.x, tfm.x.y, tfm.x.z);
      const float3 y = make_float3(tfm.y.x, tfm.y.y, tfm.y.z);
      const float3 z = make_float3(tfm.z.x, tfm.z.y, tfm.z.z);
      require_deep(isfinite_safe(tfm.x) && isfinite_safe(tfm.y) && isfinite_safe(tfm.z) &&
                       fabsf(dot(x, x) - 1) < 1e-4f && fabsf(dot(y, y) - 1) < 1e-4f &&
                       fabsf(dot(z, z) - 1) < 1e-4f && fabsf(dot(x, y)) < 1e-4f &&
                       fabsf(dot(x, z)) < 1e-4f && fabsf(dot(y, z)) < 1e-4f &&
                       dot(x, cross(y, z)) > 0,
                   "deep motion requires finite rigid transforms without scale or reflection");
    }
  };
  validate_motion(camera->get_motion(), camera->get_matrix());
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
  for (Geometry *geometry : scene->geometry) {
    /* Analytic lights add radiance but do not attenuate camera alpha (see
     * integrator_shade_light_forward). They are not deep visibility surfaces. */
    if (geometry->is_light())
      continue;
    if (volume && geometry->geometry_type == Geometry::VOLUME) {
      require_deep(!geometry->get_use_motion_blur() && geometry->get_used_shaders().size() == 1,
                   "native deep grids require static geometry with one material");
      validate_grid_shader(scene, static_cast<Shader *>(geometry->get_used_shaders()[0]));
      continue;
    }
    require_deep(geometry->geometry_type == Geometry::MESH && !geometry->get_use_motion_blur(),
                 "only static polygon meshes and background lighting are supported");
    require_deep(static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE,
                 "subdivision is unsupported");
    if (geometry->get_used_shaders().empty())
      validate_shader(scene, scene->default_surface, false, transparent);
    for (Node *shader : geometry->get_used_shaders())
      validate_shader(scene, static_cast<Shader *>(shader), false, transparent || volume, volume);
    if (volume) {
      bool has_volume = false;
      for (Node *shader : geometry->get_used_shaders())
        has_volume |= static_cast<Shader *>(shader)->graph->output()->input("Volume")->link !=
                      nullptr;
      if (has_volume) {
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
      }
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
  for (Object *object : scene->objects) {
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
    validate_motion(object->get_motion(), object->get_tfm());
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
  std::set<Shader *> used;
  for (Geometry *geometry : scene->geometry) {
    if (geometry->is_light()) continue;
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
      require(found != shader->osl_cache.end() && found->second.surface, "missing compiled surface group");
      OSL::ShaderGroup *group = found->second.surface.get();
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
          require(name != ustring("@bevel"), "ray-traced @bevel texture is unsupported");
      const auto stable_attribute = [&](const string &name) {
        if (name.rfind("geom:", 0) == 0 && Attribute::name_standard(name.c_str()+5) != ATTR_STD_NONE)
          return true;
        static const std::set<string> known = {"object:location", "object:color", "object:alpha",
          "object:index", "object:random", "material:index", "geom:name", "geom:is_smooth",
          "geom:num_polyvertices", "geom:trianglevertices", "geom:polyvertices", "scene:time", "scene:frame"};
        if (known.count(name)) return true;
        bool present = false;
        for (Geometry *geometry : scene->geometry) {
          if (geometry->geometry_type != Geometry::MESH) continue;
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
  });
#else
  require_deep(scene->params.shadingsystem != SHADINGSYSTEM_OSL, "OSL support was not built");
#endif
}

CCL_NAMESPACE_END
