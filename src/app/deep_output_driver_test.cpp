/* SPDX-License-Identifier: Apache-2.0 */
#include "app/cycles_xml.h"
#include "device/device.h"
#include "device/queue.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "session/output_driver.h"
#include "session/session.h"
#include "util/log.h"
#include "util/path.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ccl;

static void check(bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Result {
  int flat_calls = 0, deep_calls = 0;
  int last_deep_flat_call = 0;
  int width = 0, height = 0, samples = 0;
  double expected_transmittance = 0;
  bool volume = false;
  bool supported = true, fail_flat = false, fail_deep = false, cancel_flat = false;
  bool fail_device = false;
  std::vector<deep::IntervalSample> retained;
};

/* A host consuming samples directly, with no EXR writer or file output. */
class MemoryDriver : public OutputDriver {
 public:
  MemoryDriver(Result &result, Progress &progress, Device &device)
      : result_(result), progress_(progress), device_(device) {}
  bool supports_deep_output() const override
  {
    return result_.supported;
  }
  void write_render_tile(const Tile &) override
  {
    ++result_.flat_calls;
    if (result_.fail_flat) {
      throw std::runtime_error("test flat callback failure");
    }
    if (result_.cancel_flat) {
      progress_.set_cancel("test cancellation before deep delivery");
    }
    if (result_.fail_device) {
      device_.set_error("test device failure before deep delivery");
    }
  }
  void write_deep_render_tile(const DeepTile &tile) override
  {
    ++result_.deep_calls;
    check(result_.flat_calls > result_.last_deep_flat_call,
          "each deep output must follow a new flat output");
    result_.last_deep_flat_call = result_.flat_calls;
    check(!tile.cancelled(), "cancelled result delivered");
    check(tile.width == result_.width && tile.height == result_.height, "stale dimensions");
    check(tile.layer == "host-layer" && tile.view == "host-view", "missing host metadata");
    check(tile.volume == result_.volume, "incorrect volume mode");
    for (int y = 0; y < tile.height; ++y) {
      for (int x = 0; x < tile.width; ++x) {
        check(tile.population(x, y) == result_.samples, "stale camera population");
        const auto pixel = tile.get_pixel(x, y);
        if (tile.volume) {
          check(!pixel.empty(), "missing volume intervals");
          double reconstructed_t = 1, raw_t = 0;
          for (const auto &event : pixel) {
            check(event.front >= 2 - 1e-5 && event.back <= 8 + 1e-5 && event.back > event.front &&
                      event.alpha >= 0 && event.alpha < 1,
                  "incorrect host volume interval");
            reconstructed_t *= 1 - event.alpha;
          }
          for (int i = 0; i < result_.samples; ++i) {
            const auto raw = tile.get_camera_sample(x, y, i);
            check(raw.camera.complete && raw.camera.id == uint64_t(i) && raw.camera.weight == 1 &&
                      raw.camera.events.empty() && raw.intervals.size() == 1,
                  "incorrect raw volume sample");
            raw_t += std::exp(-raw.intervals[0].optical_depth) / result_.samples;
          }
          check(std::abs(raw_t - reconstructed_t) < 1e-6, "host volume curve mismatch");
          continue;
        }
        check(!pixel.empty() && pixel.size() <= size_t(result_.samples),
              "opaque plane must reconstruct accepted sample depths");
        double transmittance = 1;
        for (const auto &event : pixel) {
          check(std::abs(event.front - 3) < 1e-5 && event.back == event.front && event.alpha > 0 &&
                    event.alpha <= 1,
                "incorrect host depth or alpha");
          transmittance *= 1 - event.alpha;
        }
        check(std::abs(transmittance - result_.expected_transmittance) < 1e-6,
              "incorrect reconstructed plane transmittance");
        for (int i = 0; i < result_.samples; ++i) {
          const auto raw = tile.get_camera_sample(x, y, i);
          check(raw.camera.complete && raw.camera.id == uint64_t(i) && raw.camera.weight == 1 &&
                    raw.camera.events.size() == 1 && raw.intervals.empty(),
                "incorrect diagnostic sample");
        }
      }
    }
    result_.retained = tile.get_pixel(0, 0);
    if (result_.fail_deep) {
      throw std::runtime_error("test deep callback failure");
    }
  }

 private:
  Result &result_;
  Progress &progress_;
  Device &device_;
};

static void run(const char *fixture, int mode, const bool cuda = false, const bool volume = false,
                const int sample_limit = 0)
{
  SessionParams params;
  const auto devices = Device::available_devices(cuda ? DEVICE_MASK_CUDA : DEVICE_MASK_CPU);
  check(!devices.empty(), "requested test device unavailable");
  params.device = params.denoise_device = devices.front();
  params.background = params.headless = true;
  params.use_auto_tile = params.use_resolution_divider = false;
  params.samples = 2;
  params.threads = 2;
  params.deep.enabled = true;
  params.deep.samples = sample_limit;
  params.deep.transparent = mode == 6;
  params.deep.volume = volume || mode == 7;
  params.deep.max_events = cuda ? 64 : 16;
  SceneParams scene_params;
  Result result;
  result.expected_transmittance = mode == 6 ? 0.5 : 0;
  result.volume = params.deep.volume;
  {
    Session session(params, scene_params);
    xml_read_file(session.scene.get(), fixture);
    auto *pass = session.scene->create_node<Pass>();
    pass->set_name(ustring("combined"));
    pass->set_type(PASS_COMBINED);
    result.supported = mode != 1;
    result.cancel_flat = mode == 2;
    result.fail_deep = mode == 3;
    result.fail_flat = mode == 4;
    result.fail_device = mode == 8;
    session.set_output_driver(make_unique<MemoryDriver>(result, session.progress, *session.device));
    BufferParams buffers;
    buffers.layer = ustring("host-layer");
    buffers.view = ustring("host-view");
    auto render = [&](int width, int height) {
      result.width = buffers.width = buffers.full_width = width;
      result.height = buffers.height = buffers.full_height = height;
      result.samples = sample_limit ? min(params.samples, sample_limit) : params.samples;
      session.scene->camera->set_full_width(width);
      session.scene->camera->set_full_height(height);
      session.scene->camera->compute_auto_viewplane();
      session.reset(params, buffers);
      session.start();
      session.wait();
    };
    if (mode == 5) {
      buffers.full_x = 1;
    }
    render(16, 12);
    if (mode == 1 || mode == 5) {
      check(session.progress.get_error() && result.flat_calls == 0 && result.deep_calls == 0,
            "unsupported host/crop must fail before rendering");
    }
    else if (mode == 2) {
      check(session.progress.get_cancel() && !session.progress.get_error() &&
                result.flat_calls == 1 && result.deep_calls == 0,
            "cancelled render must not deliver deep output");
    }
    else if (mode == 8) {
      check(session.device->have_error() && session.progress.get_error() &&
                result.flat_calls == 1 && result.deep_calls == 0 &&
                session.progress.get_error_message().find("test device failure") != string::npos,
            "device error must reach progress and prevent deep delivery");
    }
    else if (mode == 3 || mode == 4) {
      check(session.progress.get_error(), "host callback failure must reach progress");
      check(session.progress.get_error_message().find("callback failure") != string::npos,
            "lost callback error message");
      check(result.deep_calls == (mode == 3 ? 1 : 0), "incorrect failure delivery count");
    }
    else {
      if (session.progress.get_error()) {
        throw std::runtime_error(session.progress.get_error_message());
      }
      check(!session.progress.get_error() && result.deep_calls == 1, "first delivery failed");
      params.samples = 3;
      params.deep.max_events = 1;
      render(8, 6);
      if (session.progress.get_error()) {
        throw std::runtime_error(session.progress.get_error_message());
      }
      check(!session.progress.get_error() && result.deep_calls == 2, "reset delivery failed");
      if (cuda && (mode == 6 || mode == 7)) {
        /* Reuse the same GPU worker after shrinking and growing the event planes.
         * Odd dimensions exercise partially occupied capture batches. */
        for (const int capacity : {8, 2}) {
          params.deep.max_events = capacity;
          render(19, 13);
          check(!session.progress.get_error(), "CUDA capacity reset failed");
        }
      }
      const int previous_deep = result.deep_calls, previous_flat = result.flat_calls;
      params.deep.enabled = false;
      render(8, 6);
      check(!session.progress.get_error() && result.deep_calls == previous_deep &&
                result.flat_calls == previous_flat + 1,
            "disabling deep must release capture and preserve flat output");
    }
    if (mode >= 1 && mode <= 5) {
      /* Reuse after failure/cancellation. In particular, a throwing flat driver
       * must not leave PathTrace::cancel() waiting on a stale rendering flag. */
      const int previous_flat = result.flat_calls, previous_deep = result.deep_calls;
      result.supported = true;
      result.fail_flat = result.fail_deep = result.cancel_flat = false;
      buffers.full_x = 0;
      params.deep.enabled = false;
      session.progress.reset();
      render(8, 6);
      check(!session.progress.get_error() && !session.progress.get_cancel() &&
                result.flat_calls == previous_flat + 1 && result.deep_calls == previous_deep,
            "session did not recover after failure/cancellation");
      params.deep.enabled = true;
      render(8, 6);
      check(!session.progress.get_error() && !session.progress.get_cancel() &&
                result.deep_calls == previous_deep + 1,
            "deep capture did not recover after failure/cancellation");
    }
  }
  if (mode == 0 && !volume) {
    check(!result.retained.empty() && result.retained.back().alpha == 1,
          "host-owned samples must survive session destruction");
  }
}

/* Same pinned prefix-copy/drain operations used by the two deep buffers.
 * Cancellation cleanup and a pre-existing device error must both drain DMA. */
static void check_queued_deep_copy()
{
  const auto info = Device::available_devices(DEVICE_MASK_CUDA).front();
  for (const bool injected_error : {false, true}) {
    Stats stats;
    Profiler profiler;
    auto device = Device::create(info, stats, profiler, true);
    auto queue = device->gpu_queue_create();
    device_vector<uint> buffer(device.get(), "deep DMA failure test", MEM_READ_WRITE);
    constexpr size_t count = 1024 * 1024, tail = 8;
    buffer.alloc(count);
    std::fill_n(buffer.data(), count, 0xdeadbeefu);
    check(queue->pin_host_memory(buffer), "deep host pin failed");
    queue->zero_to_device(buffer);
    queue->copy_from_device_prefix(buffer, (count - tail) * sizeof(uint));
    if (injected_error) {
      device->set_error("test queued deep DMA failure");
      check(!queue->synchronize(), "injected error did not reach normal wait");
    }
    queue->drain();
    check(std::all_of(buffer.data(), buffer.data() + count - tail,
                      [](uint v) { return v == 0; }), "deep drain returned before DMA completed");
    check(std::all_of(buffer.data() + count - tail, buffer.data() + count,
                      [](uint v) { return v == 0xdeadbeefu; }), "prefix copy overwrote its tail");
    queue->unpin_host_memory(buffer);
  }
  std::cout << "PASS: pinned deep prefix copy, cancellation cleanup and queued-error drain\n";
}

int main(int argc, const char **argv)
{
  try {
    check(argc == 5 || (argc == 6 && string(argv[5]) == "CUDA"),
          "expected resource directory, three scene fixtures and optional CUDA");
    const bool cuda = argc == 6;
    log_init(nullptr);
    path_init(argv[1]);
    if (cuda)
      check_queued_deep_copy();
    for (int mode = 0; mode != 6; ++mode) {
      run(argv[2], mode, cuda);
    }
    run(argv[3], 6, cuda);
    run(argv[4], 7, cuda);
    for (int mode = 1; mode != 6; ++mode) {
      run(argv[4], mode, cuda, true);
    }
    run(argv[4], 8, cuda, true);
    for (const int limit : {1, 2, 64}) {
      run(argv[2], 0, cuda, false, limit);
      run(argv[4], 0, cuda, true, limit);
    }
    std::cout << "PASS: memory delivery, reset, disable, unsupported host, cancellation, "
                 "callback failures and crop rejection\n";
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
