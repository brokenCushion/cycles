/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ccl::deep {
/* Zero is a completed miss, -1 is unrecorded. The byte limit covers raw
 * capture only, not reconstruction or EXR staging. Reads require joined workers. */
class OpaqueCapture {
 public:
  OpaqueCapture(int width, int height, int samples, size_t max_bytes);
  void record(int x, int y, uint32_t sample, float depth);
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
  mutable std::mutex mutex_;
  std::atomic<int> error_{0};
  enum Error { NONE = 0, OUT_OF_RANGE, DUPLICATE, INVALID_DEPTH, UNSUPPORTED_STATE };
  void set_error(Error error);
};
}  // namespace ccl::deep
