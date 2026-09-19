/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "deep/capture.h"
#include "session/output_driver.h"
#include <functional>

CCL_NAMESPACE_BEGIN

/* Borrow completed storage only for the synchronous host callback. */
class PathTraceDeepTile final : public OutputDriver::DeepTile {
 public:
  PathTraceDeepTile(const deep::Capture &capture,
                    string_view layer,
                    string_view view,
                    std::function<bool()> cancelled)
      : DeepTile(capture.width(), capture.height(), capture.volume(), layer, view),
        capture_(capture),
        cancelled_(std::move(cancelled))
  {
  }

  std::vector<deep::IntervalSample> get_pixel(int x, int y) const override
  {
    if (volume) {
      return capture_.reconstruct_volume_pixel(x, y);
    }
    std::vector<deep::IntervalSample> result;
    for (const auto &s : capture_.reconstruct_pixel(x, y)) {
      result.push_back({s.depth, s.depth, s.alpha});
    }
    return result;
  }
  int population(int x, int y) const override
  {
    return capture_.population(x, y);
  }
  deep::VolumeCameraSample get_camera_sample(int x, int y, int sample) const override
  {
    if (volume) {
      return capture_.volume_sample(x, y, sample);
    }
    return {{uint64_t(sample), 1.0, true, capture_.events(x, y, sample)}, {}};
  }
  bool cancelled() const override
  {
    return cancelled_();
  }

 private:
  const deep::Capture &capture_;
  const std::function<bool()> cancelled_;
};

CCL_NAMESPACE_END
