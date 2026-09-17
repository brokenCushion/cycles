/* SPDX-License-Identifier: Apache-2.0 */
#include "app/deep_output.h"
#include "deep/exr_writer.h"
#include "deep/publication.h"
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
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

CCL_NAMESPACE_BEGIN
static void require_deep(const bool condition, const char *message)
{
  if (!condition)
    throw std::invalid_argument(string("Deep: ") + message);
}

static void validate_shader(Shader *shader, const bool background, const bool transparent = false)
{
  require_deep(shader && shader->graph, "missing shader graph");
  ShaderGraph *graph = shader->graph.get();
  auto *output = graph->output();
  require_deep(!output->input("Volume")->link && !output->input("Displacement")->link,
               "volume and displacement shaders are unsupported");
  require_deep(output->input("Surface")->link != nullptr, "surface shader must be connected");
  for (ShaderNode *node : graph->nodes) {
    const string type = node->type->name.string();
    if (transparent && !background) {
      require_deep(type == "output" || type == "emission" || type == "diffuse_bsdf" ||
                       type == "transparent_bsdf" || type == "mix_closure" ||
                       type == "checker_texture" || type == "texture_coordinate" ||
                       type == "image_texture",
                   "unsupported M4 surface node (refraction, transmission, holdout and arbitrary "
                   "OSL are deferred)");
      continue;
    }
    require_deep(type == "output" || (background ? type == "background_shader" :
                                                   (type == "emission" || type == "diffuse_bsdf")),
                 "only constant diffuse/emission surfaces and constant background are supported");
    if (type != "output") {
      for (ShaderInput *input : node->inputs)
        require_deep(!input->link, "material inputs must be constant");
    }
  }
}

void validate_deep_scene(Scene *scene, const SessionParams &params, const bool transparent)
{
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
                   camera->get_motion().empty() &&
                   !camera->get_use_perspective_motion() &&
                   camera->get_stereo_eye() == Camera::STEREO_NONE &&
                   !camera->get_use_spherical_stereo() && camera->script_name.empty(),
               "requires static mono perspective camera");
  require_deep(isfinite_safe(camera->get_aperturesize()) && camera->get_aperturesize() >= 0 &&
                   isfinite_safe(camera->get_aperture_ratio()) && camera->get_aperture_ratio() > 0 &&
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
    if (geometry->geometry_type == Geometry::BACKGROUND_LIGHT)
      continue;
    require_deep(geometry->geometry_type == Geometry::MESH && !geometry->get_use_motion_blur(),
                 "only static polygon meshes and background lighting are supported");
    require_deep(static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE,
                 "subdivision is unsupported");
    if (geometry->get_used_shaders().empty())
      validate_shader(scene->default_surface, false, transparent);
    for (Node *shader : geometry->get_used_shaders())
      validate_shader(static_cast<Shader *>(shader), false, transparent);
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
                        const string &records_path,
                        const string &beauty_path,
                        const bool reduce,
                        const std::function<bool()> &cancelled)
{
  if (!capture.finalize())
    throw std::runtime_error(capture.error_message());
  deep::SurfaceImage image;
  image.display_window = {0, 0, capture.width() - 1, capture.height() - 1};
  image.data_window = image.display_window;
  image.compression = deep::DeepCompression::Zips;
  image.reduction_error = reduce ? 1e-3 : 0;
  const auto check_cancel = [&] {
    if (cancelled())
      throw std::runtime_error("Deep export cancelled; final EXR was not replaced");
  };
  check_cancel();
  if (!beauty_path.empty()) {
    /* Pair the exact already-written beauty bytes, including its header,
     * with this deep export. FNV-1a is an identity checksum, not security. */
    std::ifstream beauty(beauty_path, std::ios::binary);
    if (!beauty)
      throw std::runtime_error("Cannot read required beauty output for deep pairing");
    uint64_t hash = UINT64_C(14695981039346656037), bytes = 0;
    char buffer[65536];
    while (beauty) {
      check_cancel();
      beauty.read(buffer, sizeof(buffer));
      for (std::streamsize i = 0; i < beauty.gcount(); ++i) {
        hash ^= static_cast<unsigned char>(buffer[i]);
        hash *= UINT64_C(1099511628211);
        ++bytes;
      }
    }
    if (!beauty.eof())
      throw std::runtime_error("Beauty identity read failed");
    image.beauty_identity = "fnv1a64:" + std::to_string(hash) + ":bytes:" + std::to_string(bytes) +
                            ":path:" + beauty_path;
  }
  if (!records_path.empty()) {
    deep::AtomicOutput publication(records_path);
    std::ofstream records(publication.temporary());
    records.exceptions(std::ios::badbit | std::ios::failbit);
    records << "file_x,file_y,sample,depth,alpha,event\n"
            << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (int y = 0; y < capture.height(); ++y) {
      check_cancel();
      for (int x = 0; x < capture.width(); ++x)
        for (int sample = 0; sample < capture.population(x, capture.height() - 1 - y); ++sample) {
          const auto events = capture.events(x, capture.height() - 1 - y, sample);
          if (events.empty())
            records << x << ',' << y << ',' << sample << ",0,0,-1\n";
          for (size_t i = 0; i < events.size(); ++i)
            records << x << ',' << y << ',' << sample << ',' << events[i].depth << ','
                    << events[i].alpha << ',' << i << '\n';
        }
    }
    records.close();
    check_cancel();
    publication.publish();
  }
  deep::write_deep_exr_rows(
      path,
      image,
      [&](const int y) {
        check_cancel();
        std::vector<std::vector<deep::SurfaceSample>> row;
        row.reserve(capture.width());
        for (int x = 0; x < capture.width(); ++x)
          row.push_back(capture.reconstruct_pixel(x, capture.height() - 1 - y));
        check_cancel();
        return row;
      },
      check_cancel);
}
CCL_NAMESPACE_END
