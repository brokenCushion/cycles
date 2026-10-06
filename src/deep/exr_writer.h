/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "deep/reconstruction.h"
#include "deep/volume.h"
#include <OpenEXR/ImfForward.h>
#include <filesystem>
#include <functional>
#include <string>

namespace ccl::deep {

struct ImageWindow {
  int min_x, min_y, max_x, max_y;
};

enum class DeepCompression { None, Zips };

struct SurfaceImage {
  ImageWindow display_window;
  ImageWindow data_window;
  float pixel_aspect = 1.0f;
  int frame = 1;
  std::string view = "main";
  DeepCompression compression = DeepCompression::None;
  std::string beauty_identity;
  double reduction_error = 0.0;
  /* Streaming volume row capacity supplied by capture memory preflight.
   * Zero leaves synthetic/non-renderer callers responsible for their memory. */
  size_t volume_row_sample_limit = 0;
  int volume_export_workers = 1;
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
void write_volume_exr_pixels(const std::filesystem::path &path,
                           const SurfaceImage &image,
                           const VolumePixelProvider &pixel,
                           const std::function<void()> &before_publish = {});
/* Caller owns stream flush/close; shared with the atomic path and fault tests. */
void write_volume_exr_pixels(Imf::OStream &stream,
                            const SurfaceImage &image,
                            const VolumePixelProvider &pixel);
/* Streaming production path. Callback supplies one increasing-file-Y row.
 * FLOAT export and optional reduction are checked together against the
 * original curve before each row is written. Final name is atomic. */
using RowProvider = std::function<std::vector<std::vector<SurfaceSample>>(int)>;
void write_deep_exr_rows(const std::filesystem::path &path,
                         const SurfaceImage &image,
                         const RowProvider &row,
                         const std::function<void()> &before_publish = {});
void write_deep_exr_rows(Imf::OStream &stream, const SurfaceImage &image, const RowProvider &row);
std::vector<SurfaceSample> reduce_surface(const std::vector<SurfaceSample> &source,
                                          double tolerance);

}  // namespace ccl::deep
