/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "deep/reconstruction.h"
#include <OpenEXR/ImfForward.h>
#include <filesystem>
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
  /* Increasing image y, then x, starting at data_window.min. No implicit flip.
   * Each pixel contains strictly depth-sorted reconstructed point samples. */
  std::vector<std::vector<SurfaceSample>> pixels;
};

/* Prototype serializer, not renderer publication/storage infrastructure.
 * Emits FLOAT Z/ZBack/A and zero samples for empty pixels. Validates input and
 * the complete post-FLOAT transmittance error (<= 1e-6) before writing bytes.
 * Throws on invalid input, unacceptable quantization, allocation or I/O errors.
 * Path overload checks flush/close; failed I/O can leave a partial file.
 * Stream overload leaves flush/close to the caller (also enables fault tests). */
void write_deep_exr(const std::filesystem::path &path, const SurfaceImage &image);
void write_deep_exr(Imf::OStream &stream, const SurfaceImage &image);

}  // namespace ccl::deep
