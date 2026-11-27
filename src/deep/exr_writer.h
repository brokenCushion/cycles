/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "deep/reconstruction.h"
#include "deep/volume.h"
#include <OpenEXR/ImfForward.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ccl::deep {

struct ImageWindow {
  int min_x, min_y, max_x, max_y;
};

enum class DeepCompression { None, Zips };

/* Compact output counters, filled by the writers when SurfaceImage::compact
 * is set. Counts are per file; errors are per-pixel maxima. Diagnostics only. */
struct CompactStatistics {
  std::atomic<uint64_t> pixels{0}, samples_in{0}, samples_out{0};
  /* Surface depth merge: accepted groups, groups the allowance rejected, and
   * samples removed by accepted groups. */
  std::atomic<uint64_t> merge_groups{0}, merge_rejected{0}, merge_removed{0};
  /* Volume curve simplification: intervals before/after, curves that could
   * not be simplified within their allowance (kept unsimplified). */
  std::atomic<uint64_t> volume_in{0}, volume_out{0}, volume_skipped{0};
  /* Half alpha: pixels per rounding level (0 nearest, 1 error feedback,
   * 2..4 feedback after splitting samples above alpha .45/.12/.03). */
  std::atomic<uint64_t> half_level[5]{};
  /* Maxima stored as IEEE bit patterns (nonnegative doubles order as uint). */
  std::atomic<uint64_t> max_quantization{0}, max_merge{0}, max_half{0}, max_total{0};
  void update(std::atomic<uint64_t> &slot, double value);
  static double value(const std::atomic<uint64_t> &slot);
};

/* Static per-file compact decisions, made before any row is written. */
struct CompactPlan {
  bool enabled = false;
  bool half = false;  /* HALF alpha channel; otherwise FLOAT. */
  bool zback = true;  /* False: point-sample surface file, ZBack omitted (opt-in). */
  double ledger = 0;  /* Per-pixel publication allowance all compact costs share. */
  double half_reserve = 0;      /* Part of the ledger kept for half rounding. */
  double volume_allowance = 0;  /* Volume simplification per-pixel allowance. */
};

struct SurfaceImage {
  ImageWindow display_window;
  ImageWindow data_window;
  float pixel_aspect = 1.0f;
  int frame = 1;
  std::string view = "main";
  DeepCompression compression = DeepCompression::None;
  std::string beauty_identity;
  float error = 0; /* Strict omits new attributes to preserve legacy headers. */
    float volume_shader_error = 0; /* Shader stepping is stated, not proven. */
    bool volume_shader_adaptive = false;
  float volume_step_min = 0, volume_step_max = 0;
  float z_tolerance = 0; /* Depth-domain approximation, independent of error. */
  bool ids = false;
  std::vector<std::pair<uint32_t, std::string>> object_manifest;
  std::vector<std::pair<uint32_t, std::string>> holdout_manifest;
  int deep_samples = 0; /* 0 omits the header for legacy all-sample identity. */
  double reduction_error = 0.0;
  ExportStatistics *export_statistics = nullptr;
  /* Streaming volume row capacity supplied by capture memory preflight.
   * Zero leaves synthetic/non-renderer callers responsible for their memory. */
  size_t volume_row_sample_limit = 0;
  int volume_export_workers = 1;
  /* Opt-in compact output (surface merge as points, volume simplification,
   * HALF alpha). False keeps exact bytes. */
  bool compact = false;
  CompactStatistics *compact_statistics = nullptr;
  /* Increasing image y, then x, starting at data_window.min. No implicit flip.
   * Each pixel contains strictly depth-sorted reconstructed point samples. */
  std::vector<std::vector<SurfaceSample>> pixels;
};

/* Whole-image convenience serializer, retained for synthetic fixtures.
 * Emits FLOAT Z/ZBack/A and zero samples for empty pixels. Validates input and
 * the complete post-FLOAT transmittance error (<= 1e-6) before writing bytes.
 * Throws on invalid input, unacceptable quantization, allocation or I/O errors.
 * Path overload checks flush/close and atomically replaces the destination.
 * Stream overload leaves flush/close to the caller (also enables fault tests). */
void write_deep_exr(const std::filesystem::path &path, const SurfaceImage &image);
void write_deep_exr(Imf::OStream &stream, const SurfaceImage &image);
/* M8 analytic reference output. Metadata/windows come from image (whose surface
 * pixels must be empty). Checks FLOAT curve error at all depths; atomic output.
 * Whole-image fixtures only; renderer/storage integration is a separate gate. */
void write_volume_exr(const std::filesystem::path &path,
                      const SurfaceImage &image,
                      const std::vector<std::vector<IntervalSample>> &pixels);
/* Quantize one pixel at a time; retain only FLOAT samples for the scanline.
 * Callback coordinates are file x/y. With multiple export workers the provider
 * must support concurrent calls; callbacks join before writing the row. */
using VolumePixelProvider = std::function<std::vector<IntervalSample>(int, int)>;
using RowCallback = std::function<void(int)>;
void write_volume_exr_pixels(const std::filesystem::path &path,
                           const SurfaceImage &image,
                           const VolumePixelProvider &pixel,
                           const std::function<void()> &before_publish = {},
                           const RowCallback &begin_row = {},
                           const RowCallback &end_row = {});
/* Caller owns stream flush/close; shared with the atomic path and fault tests. */
void write_volume_exr_pixels(Imf::OStream &stream,
                            const SurfaceImage &image,
                            const VolumePixelProvider &pixel,
                            const RowCallback &begin_row = {},
                            const RowCallback &end_row = {});
/* Streaming production path. Callback supplies one increasing-file-Y row.
 * FLOAT export and optional reduction are checked together against the
 * original curve before each row is written. Final name is atomic. */
using RowProvider = std::function<std::vector<std::vector<SurfaceSample>>(int)>;
void write_deep_exr_rows(const std::filesystem::path &path,
                         const SurfaceImage &image,
                         const RowProvider &row,
                         const std::function<void()> &before_publish = {},
                         const RowCallback &begin_row = {},
                         const RowCallback &end_row = {});
void write_deep_exr_rows(Imf::OStream &stream, const SurfaceImage &image, const RowProvider &row,
                         const RowCallback &begin_row = {}, const RowCallback &end_row = {});
/* Plan for a surface (volume=false) or volume file. Throws on invalid error. */
CompactPlan compact_plan(const SurfaceImage &image, bool volume);
/* One log line describing the plan and counters. */
std::string compact_summary(const CompactPlan &plan, const CompactStatistics &statistics);
/* Round a sorted single curve's alphas to HALF (exposed for tests). level 0:
 * nearest; 1: error feedback against the exact running transmittance; 2..4:
 * feedback after splitting samples above alpha .45/.12/.03 exactly (intervals
 * by depth, points into coincident copies). Samples rounding to 0 are dropped. */
std::vector<IntervalSample> round_half_alpha(const std::vector<IntervalSample> &curve, int level);
std::vector<SurfaceSample> reduce_surface(const std::vector<SurfaceSample> &source,
                                          double tolerance);

}  // namespace ccl::deep
