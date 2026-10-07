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
  void begin_row(int y) const override { capture_.begin_export_row(y); }
  void end_row(int y) const override { capture_.end_export_row(y); }
  size_t volume_row_sample_limit() const override
  {
    return capture_.volume_row_sample_limit();
  }
  float error() const override { return capture_.error(); }
  int sample_limit() const override { return capture_.sample_limit(); }
  int volume_export_workers() const override { return capture_.volume_export_workers(); }
  deep::ExportStatistics *export_statistics() const override { return &capture_.export_statistics; }
  deep::VolumeCameraSample get_camera_sample(int x, int y, int sample) const override
  {
    if (volume) {
      /* Include diagnostic decoding in the same outer scope as its read/fit timers. */
      deep::ExportTimer timer{&capture_.export_statistics, deep::ExportStatistics::Pixel};
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
