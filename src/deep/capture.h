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
#include <memory>
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
          bool volume_grid = false,
          int export_workers = 1,
          float error = 0);
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
  VolumeCameraSample volume_sample(int x, int y, int sample,
                                   double density_tolerance = volume_density_error) const;
  std::vector<IntervalSample> reconstruct_volume_pixel(int x, int y) const;
  float error() const { return error_setting_; }
  bool volume() const
  {
    return volume_;
  }
  bool volume_grid() const
  {
    return volume_grid_;
  }
  /* Maximum total samples retained by the streaming volume writer in one row.
   * Zero applies to non-volume or reference-only in-memory captures. */
  size_t volume_row_sample_limit() const
  {
    return volume_row_sample_limit_;
  }
  int volume_export_workers() const { return volume_export_workers_; }
  /* Four times tighter curvature tolerance can require twice as many pieces.
   * This limit also drives scanline working-memory preflight. */
  static constexpr size_t volume_interval_limit = 4096;
  size_t reconstruction_limit() const
  {
    return volume_grid_ ? (samples_ > 8 ? 65536 : 16384) : volume_interval_limit;
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
  /* Successful stdio transfers, including index initialization and page rereads.
   * These are logical file I/O bytes, not physical disk traffic through OS caches. */
  struct SpillStatistics {
    uint64_t read_bytes = 0, write_bytes = 0, file_bytes = 0;
  };
  SpillStatistics spill_statistics() const;
  mutable ExportStatistics export_statistics;
  /* Sequential Y-down export: prepare on the caller thread before parallel
   * pixel reads; finish only after the row has been serialized. */
  void begin_export_row(int y) const;
  void end_export_row(int y) const;

 private:
  float error_setting_ = 0;
  size_t volume_row_sample_limit_ = 0;
  size_t volume_pixel_bytes_ = 0;
  int volume_export_workers_ = 1;
  int width_, height_, samples_;
  std::vector<KernelDeepResult> results_;
  std::vector<uint32_t> populations_;
  int max_events_;
  bool volume_;
  bool volume_grid_;
  std::vector<KernelDeepEvent> events_;
  std::vector<KernelDeepDensity> density_;
  mutable std::mutex mutex_;
  struct ReadScratch {
    std::vector<KernelDeepEvent> events;
    std::vector<KernelDeepDensity> density;
  };
  /* Capture owns worker scratch, so idle pool threads retain no buffers after
   * capture destruction. The existing per-worker preflight covers capacity. */
  mutable std::vector<std::shared_ptr<ReadScratch>> read_scratch_;
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
  static constexpr size_t event_page_bytes = 64 * 1024;
  struct EventPage {
    size_t first = size_t(-1), bytes = 0;
    uint64_t last_used = 0;
    std::vector<unsigned char> data;
  };
  struct RowFile {
    FILE *file = nullptr;
    size_t bytes = 0;
    ~RowFile() { if (file) std::fclose(file); }
  };
  struct Band {
    int first_y = 0, rows = 0;
    size_t pixels = 0, records = 0, event_bytes = 0;
    FILE *index = nullptr, *events = nullptr;
    mutable std::mutex mutex;
    mutable std::array<SpillPage, spill_page_count> pages;
    mutable EventPage event_page;
    mutable uint64_t clock = 0, read_bytes = 0, write_bytes = 0;
    mutable bool reading_events = false;
    bool rebucketed = false;
    std::vector<std::unique_ptr<RowFile>> row_files;
    ~Band() { if (index) std::fclose(index); if (events) std::fclose(events); }
  };
  std::vector<std::unique_ptr<Band>> bands_;
  int rows_per_band_ = 0;
  size_t spill_memory_bytes_ = 0;
  mutable std::vector<SpillRecord> staged_records_;
  mutable std::vector<unsigned char> staged_events_;
  mutable int staged_first_y_ = -1, staged_rows_ = 0, next_export_y_ = -1;
  mutable bool exporting_ = false, export_row_open_ = false;
  bool has_density(KernelDeepEventKind kind) const
  {
    return kind == DEEP_VOLUME_CUBIC ||
           (kind == DEEP_VOLUME && volume_grid_ && error_setting_ > 0);
  }
  void decode_index(size_t index, size_t &pixel, uint32_t &sample) const;
  Band &band_for_pixel(size_t pixel) const;
  size_t band_record_index(const Band &band, size_t pixel, uint32_t sample) const;
  void read_events(Band &band, size_t offset, unsigned char *destination, size_t bytes) const;
  SpillPage &spill_page(Band &band, size_t index) const;
  void flush_page(Band &band, SpillPage &page) const;
  void load_stream(Band &band, FILE *file, size_t bytes, int first_y, int rows) const;
  void rebucket(Band &band) const;
  size_t count_ = 0;
  std::atomic<size_t> completed_{0};
  size_t stride_ = 0, capacity_ = 0;
  size_t record_index(size_t pixel, uint32_t sample) const;
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
