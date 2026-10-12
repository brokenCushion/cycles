/* SPDX-License-Identifier: Apache-2.0 */
/* Reuse the independent scene conversion fixture, without changing its parity test. */
#define main phase6_render_main
#include "rendering.cpp"
#undef main
#include "app/deep_output.h"
#include "app/deep_output.cpp"
#include "app/oiio_output_driver.cpp"

class DeepProxy : public ccl::OutputDriver::DeepTile {
  ccl_deep_tile *tile;
  const ccl_deep_info &info;

 public:
  DeepProxy(ccl_deep_tile *t, const ccl_deep_info &i)
      : DeepTile(i.width, i.height, bool(i.volume), i.layer, i.view), tile(t), info(i)
  {
  }
  std::vector<ccl::deep::IntervalSample> get_pixel(int x, int y) const override
  {
    uint64_t count = 0;
    uint32_t accepted = 0;
    OK(ccl_deep_tile_get_pixel(tile, x, height - 1 - y, nullptr, 0, &count, &accepted));
    CHECK(accepted == 16);
    std::vector<ccl_deep_sample> samples(count);
    if (count) {
      samples[0].z = -99;
      CHECK(ccl_deep_tile_get_pixel(
                tile, x, height - 1 - y, samples.data(), count - 1, &count, &accepted) ==
            CCL_ERROR_BUFFER_TOO_SMALL);
      CHECK(samples[0].z == -99);
    }
    OK(ccl_deep_tile_get_pixel(
        tile, x, height - 1 - y, samples.data(), samples.size(), &count, &accepted));
    std::vector<ccl::deep::IntervalSample> result;
    for (auto &s : samples)
      result.push_back({s.z, s.z_back, s.alpha, s.object_index, s.facing});
    return result;
  }
  size_t volume_row_sample_limit() const override
  {
    return size_t(info.row_sample_limit);
  }
  float error() const override
  {
    return info.error;
  }
  float z_tolerance() const override
  {
    return info.z_tolerance;
  }
  float volume_shader_error() const override
  {
    return info.volume_shader_error;
  }
  bool volume_shader_adaptive() const override
  {
    return bool(info.volume_shader_adaptive);
  }
  float volume_step_min() const override
  {
    return info.volume_step_min;
  }
  float volume_step_max() const override
  {
    return info.volume_step_max;
  }
  bool ids() const override
  {
    return bool(info.ids);
  }
  int sample_limit() const override
  {
    return info.sample_limit;
  }
  std::vector<std::pair<uint32_t, std::string>> object_manifest() const override
  {
    std::vector<std::pair<uint32_t, std::string>> result;
    for (uint64_t i = 0; i < info.object_count; ++i)
      result.emplace_back(info.objects[i].id, info.objects[i].name);
    return result;
  }
  std::vector<std::pair<uint32_t, std::string>> holdout_manifest() const override
  {
    std::vector<std::pair<uint32_t, std::string>> result;
    for (uint64_t i = 0; i < info.holdout_count; ++i)
      result.emplace_back(info.holdouts[i].id, info.holdouts[i].name);
    return result;
  }
  int population(int, int) const override
  {
    throw std::runtime_error("No raw-record export requested");
  }
  ccl::deep::VolumeCameraSample get_camera_sample(int, int, int) const override
  {
    throw std::runtime_error("No raw-record export requested");
  }
  bool cancelled() const override
  {
    return false;
  }
};
struct DeepResult {
  std::filesystem::path output;
  bool called = false, fail = false;
  int throw_exception = 0;
  ccl_deep_tile *expired = nullptr;
};
static int32_t CCL_CALL deep_output(ccl_deep_tile *tile, const ccl_deep_info *info, void *user)
{
  auto &result = *static_cast<DeepResult *>(user);
  CHECK(!result.called && info && info->size == sizeof(*info));
  result.called = true;
  result.expired = tile;
  ccl_version version{sizeof(version)};
  CHECK(ccl_api_version(&version) == CCL_ERROR_CALLBACK_REENTRY);
  uint64_t count = 99;
  uint32_t accepted = 99;
  CHECK(ccl_deep_tile_get_pixel(tile, -1, 0, nullptr, 0, &count, &accepted) ==
        CCL_ERROR_INVALID_ARGUMENT);
  CHECK(count == 99 && accepted == 99);
  std::thread other([&] {
    CHECK(ccl_deep_tile_get_pixel(tile, 0, 0, nullptr, 0, &count, &accepted) ==
          CCL_ERROR_INVALID_ARGUMENT);
  });
  other.join();
  if (result.throw_exception == 1)
    throw std::runtime_error("host deep error");
  if (result.throw_exception == 2) throw 42;
  if (result.fail)
    return 1;
  DeepProxy proxy(tile, *info);
  ccl::DeepOutputDriver writer("", "combined", {}, result.output.string(), "", false);
  writer.write_deep_render_tile(proxy);
  return 0;
}

int main(int argc, char **argv)
{
  CHECK(argc == 4);
  setvbuf(stdout, nullptr, _IONBF, 0);
  const std::filesystem::path root = argv[3], repo = argv[2];
  std::filesystem::create_directories(root);
  ccl_init_params init{sizeof(init), argv[1], argv[3]};
  OK(ccl_init(&init));
  ccl::path_init(argv[1], argv[3]);
  for (bool transparent : {false, true})
    for (bool ids : {false, true}) {
      ccl_session_params p{sizeof(p)};
      OK(ccl_session_params_default(&p));
      p.threads = 4;
      ccl_session *api;
      OK(ccl_session_create(&p, &api));
      ccl_scene *scene;
      OK(ccl_session_scene(api, &scene));
      ccl::SessionParams np;
      np.device = np.denoise_device = ccl::Device::available_devices(ccl::DEVICE_MASK_CPU).at(0);
      np.headless = np.background = true;
      np.threads = 4;
      ccl::SceneParams sp;
      sp.background = true;
      ccl::Session reference(np, sp);
      auto fixture = repo / "src/app" /
                     (transparent ? "deep_output_driver_transparent_test.xml" :
                                    "deep_output_driver_test.xml");
      ccl::xml_read_file(reference.scene.get(), fixture.string().c_str());
      reference.scene->camera->set_full_width(47);
      reference.scene->camera->set_full_height(20);
      reference.scene->camera->compute_auto_viewplane();
      auto *pass = reference.scene->create_node<ccl::Pass>();
      pass->set_name(ccl::ustring("combined"));
      pass->set_type(ccl::PASS_COMBINED);
      clone_scene(*reference.scene, scene);
      auto label = std::string(transparent ? "transparent" : "opaque") + (ids ? "-ids" : "");
      DeepResult result{root / (label + "-api.exr")};
      ccl_deep_params deep{sizeof(deep)};
      OK(ccl_deep_params_default(&deep));
      deep.error = 0;
      deep.transparent = transparent;
      deep.ids = ids;
      auto bad = deep;
      bad.memory_bytes = 0;
      CHECK(ccl_session_set_deep_output(api, &bad, deep_output, &result) ==
            CCL_ERROR_INVALID_ARGUMENT);
      OK(ccl_session_set_deep_output(api, &deep, deep_output, &result));
      auto bp = buffers();
      OK(ccl_session_reset(api, &bp));
      OK(ccl_session_start(api));
      OK(ccl_session_wait(api));
      CHECK(result.called && std::filesystem::file_size(result.output) > 0);
      uint64_t count;
      uint32_t population;
      CHECK(ccl_deep_tile_get_pixel(result.expired, 0, 0, nullptr, 0, &count, &population) ==
            CCL_ERROR_INVALID_ARGUMENT);
      for (int failure = 0; failure < 3; ++failure) {
        DeepResult rejected{root / (label + "-rejected.exr")};
        rejected.fail = failure == 0;
        rejected.throw_exception = failure;
        OK(ccl_session_set_deep_output(api, &deep, deep_output, &rejected));
        OK(ccl_session_reset(api, &bp));
        OK(ccl_session_start(api));
        CHECK(ccl_session_wait(api) == CCL_ERROR_INTERNAL);
        CHECK(rejected.called && !std::filesystem::exists(rejected.output));
      }
      OK(ccl_session_set_deep_output(api, nullptr, nullptr, nullptr));
      OK(ccl_session_reset(api, &bp));
      OK(ccl_session_start(api));
      OK(ccl_session_wait(api));
      OK(ccl_session_destroy(api));
      printf("PASS deep callback contract: %s (standalone byte comparison pending)\n",
             label.c_str());
    }
}
