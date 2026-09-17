/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

namespace ccl::deep {
/* Unit-weight fixed-sample CPU capture. The renderer selects disk spill;
 * in-memory mode is retained for reference tests. Spill uses checked fixed
 * offsets for (pixel, sample) identities and explicit completion markers.
 * Its budget preflights a conservative scanline/pixel export working set.
 * In-memory mode's budget covers raw capture only. Reads require joined workers. */
class OpaqueCapture {
 public:
  OpaqueCapture(int width,
                int height,
                int samples,
                size_t max_bytes,
                int max_events = 0,
                bool spill = false);
  ~OpaqueCapture();
  void record(int x, int y, uint32_t sample, float depth);
  /* Publish only a complete traversal. Interleaved positive depth and local alpha. */
  void record_events(int x, int y, uint32_t sample, const float *events, int count);
  std::vector<SurfaceEvent> events(int x, int y, int sample) const;
  int max_events() const
  {
    return max_events_;
  }
  void fail();
  bool finalize() const;
  const char *error_message() const;
  float value(int x, int y, int sample) const;
  std::vector<SurfaceSample> reconstruct_pixel(int x, int y) const;
  int width() const
  {
    return width_;
  }
  int height() const
  {
    return height_;
  }
  int samples() const
  {
    return samples_;
  }

 private:
  int width_, height_, samples_;
  std::vector<float> values_;
  int max_events_;
  std::vector<float> events_;
  mutable std::mutex mutex_;
  FILE *spill_ = nullptr;
  size_t count_ = 0, completed_ = 0;
  size_t stride_ = 0;
  void read_record(size_t index, float *record) const;
  void store_record(size_t index, const float *record);
  std::atomic<int> error_{0};
  enum Error { NONE = 0, OUT_OF_RANGE, DUPLICATE, INVALID_DEPTH, UNSUPPORTED_STATE, IO_ERROR };
  void set_error(Error error);
};
}  // namespace ccl::deep
