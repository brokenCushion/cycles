/* SPDX-License-Identifier: Apache-2.0 */
#include "app/deep_output.h"
#include "deep/exr_writer.h"
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

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

CCL_NAMESPACE_BEGIN
static void require_deep(const bool condition, const char *message)
{
  if (!condition)
    throw std::invalid_argument(string("Deep M3: ") + message);
}

static void validate_shader(Shader *shader, const bool background)
{
  require_deep(shader && shader->graph, "missing shader graph");
  ShaderGraph *graph = shader->graph.get();
  auto *output = graph->output();
  require_deep(!output->input("Volume")->link && !output->input("Displacement")->link,
               "volume and displacement shaders are unsupported");
  require_deep(output->input("Surface")->link != nullptr, "surface shader must be connected");
  for (ShaderNode *node : graph->nodes) {
    const string type = node->type->name.string();
    require_deep(type == "output" || (background ? type == "background_shader" :
                                                   (type == "emission" || type == "diffuse_bsdf")),
                 "only constant diffuse/emission surfaces and constant background are supported");
    if (type != "output") {
      for (ShaderInput *input : node->inputs)
        require_deep(!input->link, "material inputs must be constant");
    }
  }
}

void validate_deep_scene(Scene *scene, const SessionParams &params)
{
  require_deep(params.device.type == DEVICE_CPU && params.background,
               "requires CPU background rendering");
  require_deep(scene->params.shadingsystem == SHADINGSYSTEM_SVM,
               "OSL is not supported by the M3 material allowlist");
  require_deep(params.samples > 0 && params.samples <= 4096 && !params.use_sample_subset &&
                   params.pixel_size == 1 && params.time_limit == 0 && !params.use_auto_tile,
               "requires 1..4096 fixed samples, full resolution, no time limit or tiling");
  const Integrator *integrator = scene->integrator;
  require_deep(!integrator->get_use_adaptive_sampling() && !integrator->get_use_sample_subset(),
               "adaptive sampling and sample subsets are unsupported");
  require_deep(
      !integrator->get_motion_blur() && !integrator->get_use_guiding() &&
          !integrator->get_use_denoise() && !integrator->get_use_custom_pixel_jitter_sample() &&
          !integrator->get_use_pixel_jitter() && integrator->get_ao_bounces() == 0,
      "motion blur, guiding, denoising, pixel jitter overrides and AO bounces are unsupported");
  require_deep(scene->film->get_filter_type() == FILTER_BOX &&
                   scene->film->get_filter_width() == 1,
               "requires box filter width 1");
  const Camera *camera = scene->camera;
  require_deep(camera->get_camera_type() == CAMERA_PERSPECTIVE &&
                   camera->get_aperturesize() == 0 && camera->get_motion().empty() &&
                   !camera->get_use_perspective_motion() &&
                   camera->get_stereo_eye() == Camera::STEREO_NONE &&
                   !camera->get_use_spherical_stereo() && camera->script_name.empty(),
               "requires static mono perspective pinhole camera");
  require_deep(camera->get_nearclip() >= 0 && camera->get_farclip() > camera->get_nearclip() &&
                   std::isfinite(camera->get_fov()) && camera->get_fov() > 0 &&
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
    if (geometry->geometry_type == Geometry::BACKGROUND_LIGHT)
      continue;
    require_deep(geometry->geometry_type == Geometry::MESH && !geometry->get_use_motion_blur(),
                 "only static polygon meshes and background lighting are supported");
    require_deep(static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE,
                 "subdivision is unsupported");
    if (geometry->get_used_shaders().empty())
      validate_shader(scene->default_surface, false);
    for (Node *shader : geometry->get_used_shaders())
      validate_shader(static_cast<Shader *>(shader), false);
  }
  for (Object *object : scene->objects) {
    require_deep(object->get_motion().empty() && !object->get_use_holdout() &&
                     !object->get_is_shadow_catcher() && !object->get_is_caustics_caster() &&
                     !object->get_is_caustics_receiver(),
                 "object motion, holdout, shadow catcher and caustics are unsupported");
  }
}

void write_deep_capture(const deep::OpaqueCapture &capture,
                        const string &path,
                        const string &records_path)
{
  if (!capture.finalize())
    throw std::runtime_error(capture.error_message());
  deep::SurfaceImage image;
  image.display_window = {0, 0, capture.width() - 1, capture.height() - 1};
  image.data_window = image.display_window;
  image.compression = deep::DeepCompression::Zips;
  image.pixels.reserve(size_t(capture.width()) * size_t(capture.height()));
  for (int y = 0; y < capture.height(); ++y)
    for (int x = 0; x < capture.width(); ++x)
      image.pixels.push_back(capture.reconstruct_pixel(x, capture.height() - 1 - y));
  deep::write_deep_exr(path, image);
  if (!records_path.empty()) {
    std::ofstream records(records_path);
    records.exceptions(std::ios::badbit | std::ios::failbit);
    records << "file_x,file_y,sample,depth\n"
            << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (int y = 0; y < capture.height(); ++y)
      for (int x = 0; x < capture.width(); ++x)
        for (int sample = 0; sample < capture.samples(); ++sample)
          records << x << ',' << y << ',' << sample << ','
                  << capture.value(x, capture.height() - 1 - y, sample) << '\n';
    records.close();
  }
}
CCL_NAMESPACE_END
