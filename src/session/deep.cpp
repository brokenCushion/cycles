/* SPDX-License-Identifier: Apache-2.0 */
#include "session/deep.h"
#include "scene/background.h"
#include "scene/bake.h"
#include "scene/camera.h"
#include "scene/geometry.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_nodes.h"
#include "session/session.h"
#include "util/math.h"

#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

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

static void validate_shader(Shader *shader,
                            const bool background,
                            const bool transparent = false,
                            const bool volume = false)
{
  require_deep(shader && shader->graph, "missing shader graph");
  ShaderGraph *graph = shader->graph.get();
  auto *output = graph->output();
  if (volume && !background && output->input("Volume")->link) {
    require_deep(!output->input("Surface")->link && !output->input("Displacement")->link,
                 "M8 volume boundaries require pure volume materials");
    for (ShaderNode *node : graph->nodes) {
      if (node == output)
        continue;
      require_deep(node->type->name == ustring("absorption_volume"),
                   "M8 supports constant absorption_volume only");
      for (ShaderInput *input : node->inputs)
        require_deep(!input->link, "M8 volume inputs must be constant");
      const auto *absorption = static_cast<AbsorptionVolumeNode *>(node);
      const float3 color = absorption->get_color();
      require_deep(isfinite_safe(color) && color.x == color.y && color.x == color.z &&
                       color.x >= 0 && color.x <= 1 && isfinite_safe(absorption->get_density()) &&
                       absorption->get_density() >= 0,
                   "M8 requires finite nonnegative scalar extinction");
    }
    return;
  }
  require_deep(!output->input("Volume")->link && !output->input("Displacement")->link,
               "volume and displacement shaders are unsupported");
  require_deep(output->input("Surface")->link != nullptr, "surface shader must be connected");
  for (ShaderNode *node : graph->nodes) {
    const string type = node->type->name.string();
    if ((graph->simplified || graph->finalized) && type == "geometry") {
      for (ShaderOutput *socket : node->outputs) {
        for (ShaderInput *input : socket->links) {
          require_deep(default_normal_link(input), "unsupported compiled geometry input");
        }
      }
      continue;
    }
    if (transparent && !background) {
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
                       type == "translucent_bsdf" ||
                       type == "transparent_bsdf" || type == "mix_closure" ||
                       type == "checker_texture" || type == "texture_coordinate" ||
                       type == "mapping" || type == "noise_texture" ||
                       type == "gradient_texture" || type == "rgb_ramp" ||
                       type == "invert" || type == "mix_color" || type == "bump" ||
                       type == "image_texture",
                   ("unsupported deep surface node: " + type).c_str());
      continue;
    }
    require_deep(type == "output" || (background ? type == "background_shader" :
                                                   (type == "emission" || type == "diffuse_bsdf")),
                 "only constant diffuse/emission surfaces and constant background are supported");
    if (type != "output") {
      for (ShaderInput *input : node->inputs)
        require_deep(!input->link || ((graph->simplified || graph->finalized) && default_normal_link(input)),
                     "material inputs must be constant");
    }
  }
}

void validate_deep_scene(Scene *scene, const SessionParams &params)
{
  const bool transparent = params.deep.transparent;
  const bool volume = params.deep.volume;
  require_deep(params.deep.max_events >= 1 && params.deep.max_events <= 64,
               "traversal limit must be 1..64 events");
  require_deep(params.deep.memory_bytes > 0 &&
                   params.deep.memory_bytes <= size_t(1024) * 1024 * 1024,
               "working memory budget must be at most 1024 MiB");
  if (volume) {
    require_deep((params.device.type == DEVICE_CPU || params.device.type == DEVICE_CUDA) &&
                     scene->params.shadingsystem == SHADINGSYSTEM_SVM,
                 "M8 volumes require CPU or CUDA native SVM");
    require_deep(!scene->integrator->get_use_adaptive_sampling() &&
                     !scene->integrator->get_motion_blur() &&
                     scene->camera->get_motion().empty() &&
                     scene->camera->get_aperturesize() == 0 && scene->camera->get_nearclip() > 0,
                 "M8 requires fixed samples, static pinhole camera and positive near clip");
  }
  require_deep((params.device.type == DEVICE_CPU || params.device.type == DEVICE_CUDA) &&
                   params.background,
               "requires single CPU or CUDA background rendering");
  require_deep(params.device.type == DEVICE_CPU ||
                   scene->params.shadingsystem == SHADINGSYSTEM_SVM,
               "CUDA deep supports native SVM only; GPU OSL is not qualified");
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
  require_deep(!integrator->get_use_denoise() || params.device.type == DEVICE_CPU,
               "deep denoising is currently qualified for CPU rendering only");
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
                   isfinite_safe(camera->get_focaldistance()) && camera->get_focaldistance() > 0,
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
  validate_shader(scene->background->get_shader() ? scene->background->get_shader() :
                                                    scene->default_background,
                  true);
  for (Geometry *geometry : scene->geometry) {
    /* Analytic lights add radiance but do not attenuate camera alpha (see
     * integrator_shade_light_forward). They are not deep visibility surfaces. */
    if (geometry->is_light())
      continue;
    require_deep(geometry->geometry_type == Geometry::MESH && !geometry->get_use_motion_blur(),
                 "only static polygon meshes and background lighting are supported");
    require_deep(static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE,
                 "subdivision is unsupported");
    if (geometry->get_used_shaders().empty())
      validate_shader(scene->default_surface, false, transparent);
    for (Node *shader : geometry->get_used_shaders())
      validate_shader(static_cast<Shader *>(shader), false, transparent || volume, volume);
    if (volume) {
      bool has_volume = false;
      for (Node *shader : geometry->get_used_shaders())
        has_volume |= static_cast<Shader *>(shader)->graph->output()->input("Volume")->link !=
                      nullptr;
      if (has_volume) {
        const Mesh *mesh = static_cast<Mesh *>(geometry);
        require_deep(geometry->get_used_shaders().size() == 1 && mesh->num_triangles() >= 4,
                     "M8 volume mesh must have one material and a closed boundary");
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
          for (size_t v = 0; v < mesh->num_verts(); ++v) {
            const float3 delta = float3(vertices[v]) - a;
            require_deep(dot(n, delta) <= 1e-5f * len(n) * fmaxf(1, len(delta)),
                         "M8 volume meshes must be convex with outward normals");
          }
        }
        for (const auto &edge : directed_edges)
          require_deep(directed_edges.count({edge.first.second, edge.first.first}) == 1,
                       "M8 volume mesh has an open boundary");
        require_deep(std::isfinite(signed_volume) && signed_volume > 0,
                     "M8 volume mesh must enclose positive volume");
      }
    }
  }
  for (Object *object : scene->objects) {
    if (volume) {
      require_deep(object->get_motion().empty(), "M8 object motion is unsupported");
      const auto &tfm = object->get_tfm();
      require_deep(isfinite_safe(tfm.x) && isfinite_safe(tfm.y) && isfinite_safe(tfm.z) &&
                       dot(make_float3(tfm.x.x, tfm.x.y, tfm.x.z),
                           cross(make_float3(tfm.y.x, tfm.y.y, tfm.y.z),
                                 make_float3(tfm.z.x, tfm.z.y, tfm.z.z))) > 0,
                   "M8 requires finite nonsingular object transforms without reflection");
    }
    validate_motion(object->get_motion(), object->get_tfm());
    /* Blender stores shadow-catcher flags on analytic lights as well. These
     * objects have no surface closure and cannot create deep opacity events. */
    if (object->get_geometry() && object->get_geometry()->is_light())
      continue;
    require_deep(!object->get_use_holdout() && !object->get_is_shadow_catcher() &&
                     !object->get_is_caustics_caster() && !object->get_is_caustics_receiver(),
                 "holdout, shadow catcher and caustics are unsupported");
  }
}

CCL_NAMESPACE_END
