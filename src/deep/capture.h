/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include "deep/volume.h"
#include "kernel/deep/types.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

namespace ccl::deep {
/* Unit-weight camera capture with fixed or independently counted populations.
 * The renderer selects disk spill;
 * in-memory mode is retained for reference tests. Spill uses checked fixed
 * offsets for (pixel, sample) identities and explicit completion markers.
 * Its budget preflights a conservative scanline/pixel export working set.
 * In-memory mode's budget covers raw capture only. Reads require joined workers. */
class Capture {
 public:
  Capture(int width,
          int height,
          int samples,
          size_t max_bytes,
          int max_events = 0,
          bool spill = false,
          bool adaptive = false,
          bool volume = false,
          bool volume_grid = false);
  ~Capture();
  void record(int x, int y, uint32_t sample, float depth);
  /* Only complete records count as accepted camera samples. */
  void record_sample(int x,
                     int y,
                     uint32_t sample,
                     const KernelDeepResult &result,
                     const KernelDeepEvent *events,
                     const KernelDeepDensity *density = nullptr);
  void record_events(int x, int y, uint32_t sample, const KernelDeepEvent *events, int count);
  std::vector<SurfaceEvent> events(int x, int y, int sample) const;
  /* Event kind selects local surface alpha or integrated volume optical depth. */
  VolumeCameraSample volume_sample(int x, int y, int sample) const;
  std::vector<IntervalSample> reconstruct_volume_pixel(int x, int y) const;
  bool volume() const
  {
    return volume_;
  }
  bool volume_grid() const
  {
    return volume_grid_;
  }
  /* Four times tighter curvature tolerance can require twice as many pieces.
   * This limit also drives scanline working-memory preflight. */
  static constexpr size_t volume_interval_limit = 4096;
  size_t reconstruction_limit() const
  {
    return volume_grid_ ? 16384 : volume_interval_limit;
  }
  /* Independent film counter, updated after each pixel batch. Read after workers join. */
  void set_population(int x, int y, uint32_t count);
  int population(int x, int y) const;
  bool adaptive() const
  {
    return !populations_.empty();
  }
  int max_events() const
  {
    return max_events_;
  }
  void fail(KernelDeepError reason = DEEP_ERROR_STATE);
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
  std::vector<KernelDeepResult> results_;
  std::vector<uint32_t> populations_;
  int max_events_;
  bool volume_;
  bool volume_grid_;
  std::vector<KernelDeepEvent> events_;
  std::vector<KernelDeepDensity> density_;
  mutable std::mutex mutex_;
  FILE *spill_ = nullptr;
  FILE *spill_events_ = nullptr;
  /* Fixed identity index plus a sequential append stream of actual events. */
  struct SpillRecord {
    uint64_t event_offset;
    KernelDeepResult result;
  };
  /* Small pages avoid reading other samples reserved for later render batches.
   * A 64 KiB page can span multiple pixels and amplify both reads and writes. */
  static constexpr size_t spill_page_bytes = 4 * 1024;
  static constexpr size_t spill_page_count = 16;
  struct SpillPage {
    size_t first = size_t(-1);
    size_t bytes = 0;
    size_t dirty_begin = spill_page_bytes, dirty_end = 0;
    uint64_t last_used = 0;
    std::vector<unsigned char> data;
  };
  mutable std::array<SpillPage, spill_page_count> spill_pages_;
  mutable uint64_t spill_clock_ = 0;
  static constexpr size_t event_page_bytes = 64 * 1024;
  struct EventPage {
    size_t first = size_t(-1), bytes = 0;
    uint64_t last_used = 0;
    std::vector<unsigned char> data;
  };
  mutable std::array<EventPage, spill_page_count> event_pages_;
  size_t spill_event_bytes_ = 0;
  mutable bool spill_events_reading_ = false;
  void read_events(size_t offset, unsigned char *destination, size_t bytes) const;
  SpillPage &spill_page(size_t index) const;
  void flush_page(SpillPage &page) const;
  size_t count_ = 0, completed_ = 0;
  size_t stride_ = 0, capacity_ = 0;
  void read_record(size_t index, KernelDeepResult &result, KernelDeepEvent *events,
                   KernelDeepDensity *density = nullptr) const;
  void store_record(size_t index, const KernelDeepResult &result, const KernelDeepEvent *events,
                    const KernelDeepDensity *density);
  std::atomic<int> error_{0};
  std::atomic<KernelDeepError> failure_{DEEP_ERROR_NONE};
  enum Error { NONE = 0, OUT_OF_RANGE, DUPLICATE, INVALID_DEPTH, UNSUPPORTED_STATE, IO_ERROR };
  void set_error(Error error);
};
}  // namespace ccl::deep
