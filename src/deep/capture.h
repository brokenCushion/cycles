/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include "deep/reducer.h"
#include "deep/volume.h"
#include "kernel/deep/types.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <memory>
#include <unordered_set>
#include <vector>
#include <string>
#include <utility>

namespace ccl::deep {
/* Unit-weight camera capture with fixed or independently counted populations.
 * The renderer selects disk spill;
 * in-memory mode is retained for reference tests. Spill uses checked fixed
 * offsets for (pixel, sample) identities and explicit completion markers.
 * Its budget preflights a conservative scanline/pixel export working set.
 * In-memory mode's budget covers raw capture only. Reads require joined workers. */
class Capture {
  friend struct CaptureTestAccess;
  friend struct ReducerTestAccess;
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
          float error = 0,
          int sample_limit = 0,
          bool ids = false,
          const std::string &spill_dir = std::string(),
          size_t reserved_bytes = 0,
          bool retain_samples = true);
  ~Capture();
  void open_diagnostics(const std::string &path);
  bool diagnostics() const { return diagnostics_.is_open(); }
  void diagnostic_hit(int x, int y, uint32_t sample, const KernelDeepHit &hit);
  void record(int x, int y, uint32_t sample, float depth, int object = -1);
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
  /* Grid-volume spill captures with more than 8 samples reduce each pixel during
   * render (the export-time balanced tree, run online): volume_sample, one-ray
   * fit and sibling combines at ingest, so intermediate storage is about
   * popcount(n) curves per pixel. CYCLES_DEEP_VOLUME_REDUCE=0 restores the raw
   * path. retain_samples also keeps the raw per-sample spill (diagnostic
   * records need it); otherwise volume_sample is unavailable. */
  bool reducing() const
  {
    return bool(reducer_);
  }
  bool retains_samples() const
  {
    return !reducer_ || retain_samples_;
  }
  /* Numeric surface spill captures accumulate each pixel's running sum curve
   * at ingest (SurfaceAccumulator): no per-sample spill, exported bytes equal
   * reconstruct() of the raw records while no capacity merge happens. With
   * retain_samples (records/diagnostics) the raw spill is also kept. Strict
   * surfaces, 8 or fewer samples and CYCLES_DEEP_SURFACE_REDUCE=0 keep the
   * raw path only;
   * CYCLES_DEEP_SURFACE_CAPACITY sets the per-pixel breakpoint capacity
   * (default 4096). */
  bool surface_reducing() const
  {
    return bool(surface_policy_);
  }
  /* The standalone writer applies --deep-reduce (spending E - F): surface
   * capacity merges then get no T allowance. Set before rendering. */
  bool export_reduce = false;
  /* T allowance per accepted sample for surface ledger merges: E - F when the
   * writer spends only F (exact numeric output), else 0 (compact/reduce,
   * whose writers spend up to E themselves). */
  double surface_merge_budget() const;
  SurfaceAccumulatorSettings surface_settings() const;
  const SurfaceAccumulatorStatistics &surface_statistics() const
  {
    return surface_statistics_;
  }
  /* Between render passes (workers joined): wait for queued ingest, compact
   * node streams (one band per thread), enforce the reducer memory share.
   * Errors are recorded as capture failures. */
  void end_pass(const std::function<bool()> &cancelled = {});
  /* Parallel host ingest for single-producer callers (the GPU consume loop).
   * With an online reducer, record_sample validates the record (and stores
   * the raw spill when it is retained) on the caller thread, then copies it
   * into a queue; `threads` host workers reduce it while the caller prepares
   * the next device batch. Pixels are partitioned (pixel % threads), so each
   * pixel is reduced by one worker; reducer results depend only on which
   * samples arrived, so output bytes equal synchronous ingest. Queue bytes are
   * charged to the reducer's memory share and bounded (back-pressure).
   * finalize(), end_pass() and destruction wait for (or, on destruction,
   * discard) queued records. Call before the first sample; 0 restores
   * synchronous ingest. Without an online reducer it has no effect.
   * CYCLES_DEEP_INGEST_THREADS overrides the count (0 = synchronous) and
   * CYCLES_DEEP_INGEST_CHUNK the record chunk size in bytes (validation). */
  void set_ingest_threads(int threads);
  int ingest_threads() const;
  /* Log line for the online reducer (empty when not reducing). */
  std::string reducer_summary() const;
  std::vector<std::pair<uint32_t, std::string>> object_manifest;
  std::vector<std::pair<uint32_t, std::string>> holdout_manifest;
  void set_object_manifest(std::vector<std::pair<uint32_t, std::string>> manifest)
  { object_manifest = std::move(manifest); ids_ = true; }
  bool ids() const { return ids_; }
  float error() const { return error_setting_; }
  float z_tolerance = 0;
  bool compact = false; /* Compact output size; affects only the EXR writer. */
  bool retain_for_validation = false; /* Same-capture companion export, disk only. */
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
  /* Any recorded capture error is final; renderers poll this to stop early. */
  bool failed() const
  {
    return error_.load() != NONE;
  }
  bool finalize() const;
  /* The first error wins, including its I/O cause (errno, bytes, file) or numbers. */
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
  int sample_limit() const { return sample_limit_; }
  /* Successful stdio transfers, including index page rereads.
   * These are logical file I/O bytes, not physical disk traffic through OS caches.
   * file_bytes is the logical index plus the stored (possibly compressed) event
   * stream; event_raw_bytes is the uncompressed event stream. */
  struct SpillStatistics {
    uint64_t read_bytes = 0, write_bytes = 0, file_bytes = 0;
    uint64_t index_bytes = 0, event_raw_bytes = 0, event_file_bytes = 0;
    /* Online volume reducer node streams (included in file_bytes). */
    uint64_t node_file_bytes = 0, node_file_peak = 0, node_ram_peak = 0;
  };
  SpillStatistics spill_statistics() const;
  /* Spill files are delete-on-close files in this directory: the constructor
   * argument, else CYCLES_DEEP_SPILL_DIR, else the system temp directory. */
  const std::string &spill_directory() const
  {
    return spill_directory_;
  }
  /* Lossless zstd level of the append stream; 0 stores raw records. */
  int spill_compression() const
  {
    return compression_;
  }
  bool spill_index_sparse() const
  {
    return index_sparse_;
  }
  /* Whether this build links the zstd spill codec. */
  static bool spill_compression_supported();
  /* Compression was requested but its capture state did not fit the budget. */
  bool spill_compression_limited() const
  {
    return compression_limited_;
  }
  /* Free-space projection: event bytes per accepted camera sample extrapolated
   * to the sample count, plus the index, the largest export re-bucket copy, a 5%
   * margin and a 256 MiB reserve. Non-adaptive captures fail when it does not
   * fit; adaptive captures (whose final count is only bounded) get a warning.
   * Any capture fails when free space or the optional CYCLES_DEEP_SPILL_MAX_MB
   * quota falls below the reserve. Call only between render passes. Returns a
   * warning, or an empty string. */
  static constexpr uint64_t spill_reserve_bytes = uint64_t(256) * 1024 * 1024;
  std::string check_spill_space();
  /* Diagnostic interval-count histograms for grid volumes, gathered during export
   * when enabled (debug log level or CYCLES_DEEP_HISTOGRAM). Log only. */
  bool collect_histogram = false;
  std::string volume_histogram() const;
  /* Distinct pixels whose volume curve needed more than reconstruction_limit()
   * samples and had similar neighbouring samples merged to fit (as MoonRay
   * does) instead of failing. Read after export for one warning per render. */
  size_t budget_merged_pixels() const
  {
    std::lock_guard<std::mutex> lock(budget_merged_mutex_);
    return budget_merged_pixels_.size();
  }
  mutable ExportStatistics export_statistics;
  float volume_shader_error = 0; /* Total stated bound; error() is the proven half. */
  bool volume_shader_adaptive = false;
  std::atomic<uint64_t> shader_evaluations{0};
  /* Camera samples that took a kernel resource fallback instead of failing
   * (KernelDeepFallback): more events than the event capacity, merged to fit
   * as MoonRay does; or volume shader steps coarsened to fit the step and
   * evaluation limits. Count each accepted sample once; one warning each. */
  std::atomic<uint64_t> event_merged_samples{0}, step_coarsened_samples{0};
  void record_fallbacks(const unsigned int fallbacks)
  {
    if (fallbacks & DEEP_FALLBACK_MERGED)
      event_merged_samples.fetch_add(1, std::memory_order_relaxed);
    if (fallbacks & DEEP_FALLBACK_COARSENED)
      step_coarsened_samples.fetch_add(1, std::memory_order_relaxed);
  }
  float volume_step_min = 0, volume_step_max = 0;
  /* Sequential Y-down export: prepare on the caller thread before parallel
   * pixel reads; finish only after the row has been serialized. */
  void begin_export_row(int y) const;
  void end_export_row(int y) const;

 private:
  std::ofstream diagnostics_;
  std::string spill_directory_;
  int compression_ = 0;
  bool compression_limited_ = false;
  bool index_sparse_ = false;
  uint64_t index_total_bytes_ = 0, spill_quota_bytes_ = 0;
  bool projection_warned_ = false;
  /* reserved_bytes: host allocations already taken from the user budget before
   * max_bytes; added back when a message states the minimum deep_memory_mb. */
  size_t max_bytes_ = 0, population_bytes_ = 0, reserved_bytes_ = 0;
  /* Stored spill bytes written (event streams, node streams and compaction
   * copies); drives the in-render free-space floor. Compaction gives node
   * stream bytes back (spill_released_), so the quota charges live bytes. */
  mutable std::atomic<uint64_t> spill_written_{0}, spill_released_{0};
  /* Interval-count histograms: per-ray curves and balanced-tree nodes by level. */
  static constexpr int histogram_buckets = 18, histogram_levels = 13;
  struct Histogram {
    std::atomic<uint64_t> count[histogram_buckets] = {}, total{0}, items{0}, maximum{0};
  };
  mutable Histogram ray_histogram_, level_histogram_[histogram_levels];
  bool ids_ = false;
  float error_setting_ = 0;
  size_t volume_row_sample_limit_ = 0;
  size_t volume_pixel_bytes_ = 0;
  int volume_export_workers_ = 1;
  int width_, height_, samples_;
  int sample_limit_ = 0;
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
  /* Also the compression block size (raw bytes per zstd block). */
  static constexpr size_t event_page_bytes = 64 * 1024;
  /* Export decoding of a compressed stream: stored blocks per batch and
   * decoder threads (one zstd context each). */
  static constexpr size_t decode_batch_blocks = 32;
  static constexpr unsigned decode_threads = 8;
  struct EventPage {
    size_t first = size_t(-1), bytes = 0;
    uint64_t last_used = 0;
    uint64_t next_block = 0; /* Compressed stream: file offset after this block. */
    std::vector<unsigned char> data;
  };
  /* Append-only stream. Raw mode stores records directly. Compressed mode stores
   * {u32 raw, u32 stored} block headers; stored == raw marks an uncompressed block.
   * raw_bytes are logical stream offsets, identical in both modes. */
  struct SpillStream {
    FILE *file = nullptr;
    std::string path;
    uint64_t raw_bytes = 0, file_bytes = 0;
    bool reading = false;
    bool raw = false; /* Always raw, e.g. transient re-bucket row files. */
    std::vector<unsigned char> pending;
    ~SpillStream()
    {
      close();
    }
    void close()
    {
      if (file)
        std::fclose(file);
      file = nullptr;
      std::vector<unsigned char>().swap(pending);
    }
  };
  struct Band {
    int first_y = 0, rows = 0;
    size_t pixels = 0, records = 0, event_bytes = 0;
    FILE *index = nullptr;
    std::string index_path;
    SpillStream events;
    mutable std::mutex mutex;
    mutable std::array<SpillPage, spill_page_count> pages;
    mutable EventPage event_page;
    mutable uint64_t clock = 0, read_bytes = 0, write_bytes = 0;
    bool rebucketed = false;
    std::vector<std::unique_ptr<SpillStream>> row_files;
    /* Compressed mode: zstd contexts and one block of stored-data scratch. */
    void *compressor = nullptr, *decompressor = nullptr;
    std::vector<unsigned char> block;
    ~Band();
  };
  std::vector<std::unique_ptr<Band>> bands_;
  /* Raw stream bytes per image row (8 bytes per row) for the export preflight. */
  std::vector<uint64_t> row_event_bytes_;
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
  void load_stream(Band &band, SpillStream &stream, int first_y, int rows) const;
  void rebucket(Band &band) const;
  FILE *open_spill_file(const char *kind, uint64_t sparse_bytes, bool &sparse,
                        std::string &path) const;
  /* Stream I/O under the band mutex; bytes count toward the band's statistics. */
  void append(Band &band, SpillStream &stream, const void *data, size_t bytes) const;
  void flush_stream(Band &band, SpillStream &stream) const;
  /* Decodes one stored block at `offset` (raw <= event_page_bytes) into
   * `destination`; returns the next block's file offset. */
  uint64_t read_block(Band &band, SpillStream &stream, uint64_t offset,
                      unsigned char *destination, size_t &raw) const;
  /* Records stored stream bytes; throws when free space falls below the reserve. */
  void spill_written(uint64_t bytes) const;
  size_t count_ = 0;
  std::atomic<size_t> completed_{0};
  size_t stride_ = 0, capacity_ = 0;
  size_t record_index(size_t pixel, uint32_t sample) const;
  void read_record(size_t index, KernelDeepResult &result, KernelDeepEvent *events,
                   KernelDeepDensity *density = nullptr) const;
  void store_record(size_t index, const KernelDeepResult &result, const KernelDeepEvent *events,
                    const KernelDeepDensity *density);
  /* Online volume reduction (see reducing()). */
  class VolumePolicy;
  friend class VolumePolicy;
  std::unique_ptr<VolumePolicy> volume_policy_;
  std::unique_ptr<PixelReducer> reducer_;
  bool retain_samples_ = true;
  size_t reduction_levels_ = 1;
  size_t node_limit_ = 0; /* Test override of reconstruction_limit() for nodes. */
  /* New reducer with this RAM share and ledger step (before any sample). */
  void make_reducer(size_t ram, double step);
  VolumeCameraSample build_volume_sample(uint64_t sample, const KernelDeepResult &result,
                                         const KernelDeepEvent *record,
                                         const KernelDeepDensity *density,
                                         double density_tolerance,
                                         ExportStatistics *statistics) const;
  std::vector<IntervalSample> fit_volume(std::vector<VolumeCameraSample> samples,
                                         double tolerance, size_t limit, double reduction,
                                         size_t bytes, int x, int y,
                                         ExportStatistics *statistics) const;
  VolumeCameraSample combine_volume(VolumeCameraSample left, VolumeCameraSample right,
                                    size_t levels, int x, int y,
                                    ExportStatistics *statistics) const;
  void record_reduced(size_t pixel, uint32_t sample, const KernelDeepResult &result,
                      const KernelDeepEvent *events, const KernelDeepDensity *density);
  /* The online-reducer part of record_reduced (no raw spill). */
  void reduce_record(size_t pixel, uint32_t sample, const KernelDeepResult &result,
                     const KernelDeepEvent *events, const KernelDeepDensity *density);
  /* Export decode scratch for a compressed stream of `raw_bytes`: one batch of
   * stored blocks (up to 32 x 64 KiB) and up to 8 decoder contexts, about
   * 2.8 MiB for streams of 32 blocks or more; 0 for raw streams. */
  uint64_t export_decode_bytes(uint64_t raw_bytes) const;
  std::vector<IntervalSample> reconstruct_reduced_pixel(int x, int y) const;
  std::vector<SurfaceSample> reconstruct_surface_pixel(int x, int y) const;
  /* Online surface accumulation (see surface_reducing()). */
  class SurfacePolicy;
  friend class SurfacePolicy;
  std::unique_ptr<SurfacePolicy> surface_policy_;
  size_t surface_capacity_ = 4096;
  mutable SurfaceAccumulatorStatistics surface_statistics_;
  static void histogram_record(Histogram &histogram, size_t n);
  mutable std::atomic<int> error_{0};
  std::atomic<KernelDeepError> failure_{DEEP_ERROR_NONE};
  enum Error {
    NONE = 0, OUT_OF_RANGE, DUPLICATE, INVALID_DEPTH, UNSUPPORTED_STATE, IO_ERROR, SPILL_SPACE,
    REDUCTION
  };
  mutable std::mutex error_mutex_;
  mutable std::mutex budget_merged_mutex_;
  mutable std::unordered_set<uint64_t> budget_merged_pixels_;
  /* Written once, before error_ is published. */
  mutable std::string error_detail_;
  mutable std::string incomplete_detail_; /* error_message() text for incomplete reducers. */
  void set_error(Error error, const std::string &detail = std::string()) const;
  /* Parallel host ingest (see set_ingest_threads). Declared last: destroyed
   * (workers joined) before the reducer and spill state. */
  class Ingest;
  friend class Ingest;
  std::unique_ptr<Ingest> ingest_;
  void drain_ingest() const;
};
}  // namespace ccl::deep
