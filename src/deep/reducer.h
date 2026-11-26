/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ccl::deep {

/* Aligned camera-sample range [k << level, (k + 1) << level). Each pixel's
 * nodes are disjoint. Two siblings (same level, k and k ^ 1) merge into their
 * parent only when both are present, so the node set and every merge operand
 * depend only on which samples arrived, never on arrival order or batching. */
struct NodeKey {
  uint32_t k = 0;
  uint32_t level = 0;
  uint64_t first() const
  {
    return uint64_t(k) << level;
  }
  uint64_t end() const
  {
    return (uint64_t(k) + 1) << level;
  }
  bool covers(const uint64_t sample) const
  {
    return sample >= first() && sample < end();
  }
};

/* A camera sample that was already accepted (exact duplicate detection: every
 * accepted sample is covered by exactly one live node of its pixel). */
struct DuplicateSample : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/* Node stream I/O failure (errno text, bytes and file in the message). */
struct ReducerIoError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/* Merges two aligned sibling payloads. `left` covers the lower samples. The
 * payload encoding belongs to the policy; the store only moves bytes. Called
 * concurrently for different pixels, without store-wide locks. */
class ReducerPolicy {
 public:
  virtual ~ReducerPolicy() = default;
  virtual std::vector<unsigned char> combine(size_t pixel,
                                             const NodeKey &parent,
                                             const unsigned char *left,
                                             size_t left_bytes,
                                             const unsigned char *right,
                                             size_t right_bytes) = 0;
};

/* Host spill hooks: create a delete-on-close file in the spill directory, and
 * account stored bytes (may throw when free space falls below the reserve). */
struct ReducerIo {
  std::function<FILE *(std::string &path)> open;
  std::function<void(uint64_t bytes)> written;
};

/* Per-pixel online reducer: pixel state, node RAM arena and per-band node
 * stream. Volumes use it now; surfaces can reuse it with another policy.
 *
 * - add(): a population-1 leaf; merges aligned siblings bottom-up (binary
 *   counter keyed by sample range). Thread-safe across pixels; one pixel is
 *   serialized by a striped lock, and policy work runs under that lock only.
 * - finish_pixel(): enforces the RAM limit after a worker's pixel batch.
 * - compact(): between render passes (no concurrent add); rewrites band node
 *   streams that are mostly dead and enforces the RAM limit.
 * - stage()/export_nodes()/release(): export reads only surviving nodes.
 *
 * RAM (payloads, pixel state and node tables) is held within ram_limit by
 * evicting node payloads to the band's append-only node stream (one zstd frame
 * per node when compression > 0). Evicted nodes are read back when a sibling
 * arrives; their file bytes then become dead until the band is compacted. */
class PixelReducer {
 public:
  struct Statistics {
    uint64_t ram_bytes = 0, ram_peak = 0, ram_limit = 0;
    uint64_t file_bytes = 0, live_file_bytes = 0, raw_written = 0, stored_written = 0;
    uint64_t file_read_bytes = 0, file_peak = 0, evicted_nodes = 0, reloaded_nodes = 0;
    uint64_t compactions = 0, live_nodes = 0, merges = 0;
    double merge_seconds = 0;
  };
  struct ExportNode {
    NodeKey key;
    double error;
    const unsigned char *payload;
    size_t bytes;
  };

  PixelReducer(int width,
               int height,
               int rows_per_band,
               uint32_t samples,
               size_t ram_limit,
               double step_error,
               int compression,
               ReducerIo io,
               ReducerPolicy &policy);
  ~PixelReducer();
  /* Resident pixel state and locks; the constructor requires it to fit half
   * of ram_limit (callers may fall back to another path when it does not). */
  static uint64_t fixed_bytes(int width, int height);
  PixelReducer(const PixelReducer &) = delete;
  PixelReducer &operator=(const PixelReducer &) = delete;

  void add(size_t pixel, uint32_t sample, std::vector<unsigned char> leaf, double leaf_error = 0);
  void finish_pixel(size_t pixel);
  void compact(const std::function<bool()> &cancelled = {});
  /* Single-threaded export: evict resident nodes until `bytes` more fit. */
  void reserve(uint64_t bytes);
  /* Dead stream bytes a band must exceed (and its live bytes) to be rewritten. */
  void set_compaction_threshold(uint64_t bytes)
  {
    compaction_threshold_ = bytes;
  }
  uint32_t accepted(size_t pixel) const;
  /* True when the pixel's nodes are exactly the binary decomposition of
   * [0, population): all samples present, none outside. */
  bool complete(size_t pixel, uint32_t population) const;
  /* Load the evicted nodes of [first, first + count) into RAM; returns their
   * decoded bytes. Single-threaded export preparation. */
  uint64_t staging_bytes(size_t first, size_t count) const;
  void stage(size_t first, size_t count);
  void release(size_t first, size_t count);
  /* Nodes of a staged (or, before export, any) pixel in ascending sample
   * order. Pointers remain valid until release() or the next add/evict of the
   * pixel. Before export, evicted nodes are copied into `scratch`. */
  void export_nodes(size_t pixel,
                    std::vector<ExportNode> &out,
                    std::vector<std::vector<unsigned char>> &scratch) const;
  Statistics statistics() const;
  /* Bytes of live node payloads (RAM plus evicted, decoded) and pixels with nodes. */
  uint64_t live_bytes() const;
  double step_error() const
  {
    return step_error_;
  }
  size_t ram_limit() const
  {
    return ram_limit_;
  }

 private:
  struct Node {
    NodeKey key;
    double error = 0;
    uint32_t raw = 0, stored = 0;
    uint64_t offset = 0;
    std::unique_ptr<unsigned char[]> ram; /* Null when evicted. */
  };
  struct Pixel {
    std::unique_ptr<Node[]> nodes;
    uint32_t accepted = 0;
    uint16_t count = 0, capacity = 0;
  };
  struct Band {
    FILE *file = nullptr;
    std::string path;
    uint64_t file_bytes = 0, live_bytes = 0;
    void *decompressor = nullptr;
    std::vector<unsigned char> scratch;
    mutable std::mutex mutex;
    ~Band();
  };
  static constexpr size_t stripe_count = 4096;
  static constexpr size_t node_overhead = 16; /* Allocator bookkeeping per payload. */

  std::mutex &stripe(size_t pixel) const
  {
    return stripes_[pixel % stripe_count];
  }
  Band &band(size_t pixel) const
  {
    return *bands_[pixel / size_t(width_) / size_t(rows_per_band_)];
  }
  void charge(int64_t bytes);
  void insert(Pixel &pixel, Node node);
  Node take(Pixel &pixel, uint16_t index);
  void load(size_t pixel, Node &node, std::vector<unsigned char> &destination) const;
  void evict_node(size_t pixel, Node &node);
  void evict();
  void evict_to(int64_t target);
  void compact_band(size_t index, const std::function<bool()> &cancelled);

  int width_, height_, rows_per_band_;
  uint32_t samples_;
  size_t ram_limit_;
  double step_error_;
  int compression_;
  ReducerIo io_;
  ReducerPolicy &policy_;
  std::vector<Pixel> pixels_;
  std::vector<std::unique_ptr<Band>> bands_;
  mutable std::unique_ptr<std::mutex[]> stripes_;
  std::atomic<int64_t> ram_{0};
  std::atomic<uint64_t> ram_peak_{0};
  std::atomic<int64_t> evict_threshold_{0};
  std::mutex evict_mutex_;
  void *compressor_ = nullptr;
  std::vector<unsigned char> compress_scratch_;
  size_t evict_cursor_ = 0;
  uint64_t compaction_threshold_ = uint64_t(64) * 1024 * 1024;
  mutable std::atomic<uint64_t> raw_written_{0}, stored_written_{0}, read_bytes_{0},
      evicted_{0}, reloaded_{0}, compactions_{0}, merges_{0}, merge_ns_{0}, file_peak_{0};
};

/* One captured surface event of a camera sample (FLOAT capture values). */
struct SurfaceEventRecord {
  float depth = 0, alpha = 0;
  int32_t object = -1;
  int8_t facing = 0;
};

/* Online surface accumulator (bounded surface capture). Per pixel it keeps the
 * running sum curve S(z) = sum_s T_s(z) as sorted breakpoints, one per
 * reconstruct() group key (depth, plus object with IDs). Each breakpoint holds
 * the compensated (Kahan) partial sum of the folded samples' transmittance in
 * sample order, so folding samples 0..N-1 in order performs exactly the
 * floating-point operations of reconstruct(): every exported depth, alpha,
 * object and facing is bit-identical when no capacity merge happens.
 *
 * Kahan summation is order dependent, so samples fold strictly in sample
 * order. As PixelReducer payloads: a node starting at sample 0 holds the folded
 * state; any other node holds its samples' raw sorted events. Combining a
 * prefix with its right sibling folds the sibling's samples; other combines
 * concatenate. The result depends only on which samples arrived.
 *
 * Capacity: when a pixel exceeds `capacity` breakpoints it merges adjacent
 * breakpoints of one object with equal nonzero facing (the merge_surface_depths
 * rule), each merge moving one step onto its neighbour with err = err_a +
 * err_b + cost, accepted only while err <= budget * folded samples. Pairs
 * within the depth tolerance (group diameter <= z_tolerance * front) go
 * first and keep the front; otherwise the smaller step moves (min cost).
 * Exported T error is at most max(err) / N <= budget. A pixel with no legal
 * merge (or budget 0) keeps growing and stays exact. */
struct SurfaceAccumulatorSettings {
  bool ids = false;
  size_t capacity = 4096;
  double budget = 0;      /* Per-sample absolute T allowance for ledger merges. */
  double z_tolerance = 0; /* Relative depth tolerance for preferred merges; 0 disables. */
};

struct SurfaceAccumulatorStatistics {
  std::atomic<uint64_t> folded{0}, capacity_merges{0}, depth_merges{0}, overflow_folds{0};
  std::atomic<uint64_t> max_breakpoints{0};
  std::atomic<uint64_t> max_ledger{0}; /* IEEE bits of max exported err / N. */
  void raise(std::atomic<uint64_t> &slot, uint64_t value);
  void raise(std::atomic<uint64_t> &slot, double value);
  static double value(const std::atomic<uint64_t> &slot);
};

class SurfaceAccumulator {
 public:
  struct Breakpoint {
    float depth = 0;
    float span = 0; /* Deepest original depth merged into this breakpoint. */
    int32_t object = -1;
    int8_t facing = 0;
    bool mixed = false; /* Events of the key disagree on object or facing. */
    double value = 0, correction = 0; /* Kahan state over folded samples. */
    double err = 0;                   /* Ledger in sum units (merges only). */
  };
  struct State {
    uint32_t count = 0;  /* Folded samples. */
    bool merged = false; /* Any merge happened: err/span are encoded. */
    std::vector<Breakpoint> points;
  };
  struct Result {
    std::vector<SurfaceSample> samples;
    double ledger = 0; /* Max exported T error bound, max(err) / N. */
    uint32_t count = 0;
    size_t breakpoints = 0;
  };

  /* Leaf payload of one camera sample. Drops alpha <= 0 events (they form no
   * reconstruct() boundary) and sorts the rest in reconstruct() order. */
  static std::vector<unsigned char> encode_sample(std::vector<SurfaceEventRecord> events,
                                                  bool ids);
  /* PixelReducer combine: fold when parent.k == 0, else concatenate. */
  static std::vector<unsigned char> combine(const NodeKey &parent,
                                            const unsigned char *left,
                                            size_t left_bytes,
                                            const unsigned char *right,
                                            size_t right_bytes,
                                            const SurfaceAccumulatorSettings &settings,
                                            SurfaceAccumulatorStatistics *statistics);
  /* Export: nodes in ascending sample order covering [0, population). Returns
   * reconstruct()'s samples (bit-identical without merges) and the ledger. */
  static Result finish(const std::vector<std::pair<const unsigned char *, size_t>> &nodes,
                       uint32_t population,
                       const SurfaceAccumulatorSettings &settings,
                       SurfaceAccumulatorStatistics *statistics);
  /* Payload helpers (tests): folded state of a payload and its encoding. */
  static State decode_state(const unsigned char *data, size_t bytes,
                            const SurfaceAccumulatorSettings &settings,
                            SurfaceAccumulatorStatistics *statistics);
  static std::vector<unsigned char> encode_state(const State &state);
  /* Fold one sample's sorted events (as stored in a leaf) into the state. */
  static void fold(State &state, const SurfaceEventRecord *events, size_t count,
                   const SurfaceAccumulatorSettings &settings,
                   SurfaceAccumulatorStatistics *statistics);
  static std::vector<SurfaceSample> curve(const State &state, bool ids);
};

}  // namespace ccl::deep
