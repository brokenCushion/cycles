/* SPDX-License-Identifier: Apache-2.0 */

#include "deep/exr_writer.h"
#include "deep/publication.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfDoubleAttribute.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStdIO.h>
#include <OpenEXR/ImfStringAttribute.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ccl::deep {
namespace {
constexpr double export_error = 1e-6;

Imath::Box2i box(const ImageWindow &w)
{
  const int64_t width = int64_t(w.max_x) - w.min_x + 1;
  const int64_t height = int64_t(w.max_y) - w.min_y + 1;
  if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max())
  {
    throw std::invalid_argument("Invalid or oversized deep image window");
  }
  return {{w.min_x, w.min_y}, {w.max_x, w.max_y}};
}

struct FloatPixel {
  std::vector<float> z, a, back;
};

/* Compare every interval of the two step functions, including either side of
 * their union of boundaries. A small Z rounding error can cause a large T error. */
void check_quantization(const std::vector<SurfaceSample> &source,
                        const FloatPixel &output,
                        const double tolerance = export_error)
{
  size_t i = 0, j = 0;
  double source_t = 1.0, output_t = 1.0;
  while (i < source.size() || j < output.z.size()) {
    const double z = std::min(i < source.size() ? source[i].depth : INFINITY,
                              j < output.z.size() ? double(output.z[j]) : INFINITY);
    while (i < source.size() && source[i].depth == z) {
      source_t *= 1.0 - source[i++].alpha;
    }
    while (j < output.z.size() && double(output.z[j]) == z) {
      output_t *= 1.0 - double(output.a[j++]);
    }
    if (std::abs(source_t - output_t) > tolerance) {
      throw std::invalid_argument("FLOAT depth/alpha exceeds deep transmittance error budget");
    }
  }
}

std::vector<FloatPixel> prepare(const SurfaceImage &image)
{
  box(image.display_window);
  const auto dw = box(image.data_window);
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  const size_t height = size_t(int64_t(dw.max.y) - dw.min.y + 1);
  if (width > std::numeric_limits<size_t>::max() / height ||
      width * height != image.pixels.size() ||
      width > std::numeric_limits<size_t>::max() / sizeof(float *))
  {
    throw std::invalid_argument("Deep image pixel count does not match data window");
  }
  if (!std::isfinite(image.pixel_aspect) || image.pixel_aspect <= 0.0f || image.view.empty() ||
      image.view.find('\0') != std::string::npos)
  {
    throw std::invalid_argument("Invalid deep pixel aspect or view identity");
  }
  if (image.compression != DeepCompression::None && image.compression != DeepCompression::Zips) {
    throw std::invalid_argument("Unsupported deep compression");
  }
  std::vector<FloatPixel> result(image.pixels.size());
  for (size_t p = 0; p < image.pixels.size(); ++p) {
    const auto &pixel = image.pixels[p];
    if (pixel.size() > std::numeric_limits<unsigned int>::max()) {
      throw std::invalid_argument("Deep sample count exceeds UINT");
    }
    double previous = 0.0;
    for (const auto &sample : pixel) {
      if (!std::isfinite(sample.depth) || sample.depth <= previous ||
          sample.depth > std::numeric_limits<float>::max() || !std::isfinite(sample.alpha) ||
          sample.alpha <= 0.0 || sample.alpha > 1.0)
      {
        throw std::invalid_argument("Invalid or unsorted reconstructed surface sample");
      }
      const float z = float(sample.depth), a = float(sample.alpha);
      if (z <= 0.0f || a <= 0.0f) {
        throw std::invalid_argument("Surface sample underflows FLOAT export");
      }
      result[p].z.push_back(z);
      result[p].a.push_back(a);
      previous = sample.depth;
    }
    check_quantization(pixel, result[p]);
  }
  return result;
}

Imf::Header make_header(const SurfaceImage &image)
{
  const auto dw = box(image.data_window);
  Imf::Header header(box(image.display_window),
                     dw,
                     image.pixel_aspect,
                     Imath::V2f(0, 0),
                     1,
                     Imf::INCREASING_Y,
                     image.compression == DeepCompression::None ? Imf::NO_COMPRESSION :
                                                                  Imf::ZIPS_COMPRESSION);
  header.setType(Imf::DEEPSCANLINE);
  header.setVersion(1);
  header.setView(image.view);
  for (const char *name : {"Z", "ZBack", "A"}) {
    header.channels().insert(name, Imf::Channel(Imf::FLOAT));
  }
  header.insert("cycles:frame", Imf::IntAttribute(image.frame));
  header.insert("cycles:depthConvention", Imf::StringAttribute("positive_axial_camera_z"));
  header.insert("cycles:depthUnits", Imf::StringAttribute("scene_units"));
  header.insert("cycles:beautyIdentity", Imf::StringAttribute(image.beauty_identity));
  header.insert(
      "cycles:maxTransmittanceError",
      Imf::DoubleAttribute(image.reduction_error ? image.reduction_error : export_error));
  /* Do not advertise deepImageState: distinct double depths can round together. */
  try {
    header.sanityCheck();
  }
  catch (const std::exception &error) {
    throw std::invalid_argument(error.what());
  }
  return header;
}

void serialize(Imf::OStream &stream, const Imf::Header &header, std::vector<FloatPixel> &pixels)
{
  const auto dw = header.dataWindow();
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  const int height = int(int64_t(dw.max.y) - dw.min.y + 1);
  std::vector<unsigned int> counts;
  std::vector<float *> z, a, back;
  for (auto &pixel : pixels) {
    counts.push_back(static_cast<unsigned int>(pixel.z.size()));
    z.push_back(pixel.z.empty() ? nullptr : pixel.z.data());
    a.push_back(pixel.a.empty() ? nullptr : pixel.a.data());
    back.push_back(pixel.back.empty() ? z.back() : pixel.back.data());
  }
  Imf::DeepFrameBuffer fb;
  fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, counts.data(), dw));
  /* Slice::Make handles signed window offsets without user-code pointer
   * subtraction outside the allocated array; the deep grid stores pointers. */
  const auto zs = Imf::Slice::Make(
      Imf::FLOAT, z.data(), dw, sizeof(float *), width * sizeof(float *));
  const auto as = Imf::Slice::Make(
      Imf::FLOAT, a.data(), dw, sizeof(float *), width * sizeof(float *));
  const auto bs = Imf::Slice::Make(
      Imf::FLOAT, back.data(), dw, sizeof(float *), width * sizeof(float *));
  fb.insert("Z", Imf::DeepSlice(Imf::FLOAT, zs.base, zs.xStride, zs.yStride, sizeof(float)));
  fb.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, bs.base, bs.xStride, bs.yStride, sizeof(float)));
  fb.insert("A", Imf::DeepSlice(Imf::FLOAT, as.base, as.xStride, as.yStride, sizeof(float)));
  Imf::DeepScanLineOutputFile file(stream, header, 1);
  file.setFrameBuffer(fb);
  file.writePixels(height);
}
}  // namespace

void write_deep_exr(const std::filesystem::path &path, const SurfaceImage &image)
{
  auto pixels = prepare(image);
  const auto header = make_header(image);
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    serialize(stream, header, pixels);
  }
  output.flush();
  output.close();
  publication.publish();
}

void write_deep_exr(Imf::OStream &stream, const SurfaceImage &image)
{
  auto pixels = prepare(image);
  const auto header = make_header(image);
  serialize(stream, header, pixels);
}

static std::vector<FloatPixel> prepare_volume(
    const SurfaceImage &image, const std::vector<std::vector<IntervalSample>> &source)
{
  if (!image.pixels.empty() || image.reduction_error != 0)
    throw std::invalid_argument("Volume fixture writer requires empty surface pixels and no reduction");
  SurfaceImage metadata = image;
  metadata.pixels.resize(source.size());
  auto pixels = prepare(metadata);
  for (size_t p = 0; p < source.size(); ++p) {
    if (source[p].size() > std::numeric_limits<unsigned int>::max())
      throw std::invalid_argument("Volume sample count exceeds UINT");
    std::vector<IntervalSample> quantized;
    for (const auto &s : source[p]) {
      quantized.push_back({double(float(s.front)), double(float(s.back)), double(float(s.alpha))});
      if (s.back > s.front && quantized.back().back <= quantized.back().front)
        throw std::invalid_argument("Volume interval collapses under FLOAT export");
      pixels[p].z.push_back(float(s.front));
      pixels[p].back.push_back(float(s.back));
      pixels[p].a.push_back(float(s.alpha));
    }
    if (interval_curve_error(source[p], quantized) > export_error - 2e-7)
      throw std::invalid_argument("FLOAT volume curve exceeds transmittance error budget");
  }
  return pixels;
}

void write_volume_exr(const std::filesystem::path &path,
                      const SurfaceImage &image,
                      const std::vector<std::vector<IntervalSample>> &source)
{
  auto pixels = prepare_volume(image, source);
  auto header = make_header(image);
  header.insert("cycles:deepScope", Imf::StringAttribute("analytic_volume_reference"));
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    serialize(stream, header, pixels);
  }
  output.flush();
  output.close();
  publication.publish();
}

void write_volume_exr_rows(const std::filesystem::path &path,
                           const SurfaceImage &image,
                           const VolumeRowProvider &row,
                           const std::function<void()> &before_publish)
{
  auto header = make_header(image);
  header.insert("cycles:deepScope", Imf::StringAttribute("cpu_homogeneous_absorption"));
  const auto dw = header.dataWindow();
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    Imf::DeepScanLineOutputFile file(stream, header, 1);
    for (int64_t y = dw.min.y; y <= dw.max.y; ++y) {
      SurfaceImage metadata = image;
      metadata.data_window = {dw.min.x, int(y), dw.max.x, int(y)};
      auto pixels = prepare_volume(metadata, row(int(y)));
      std::vector<unsigned int> counts(width);
      std::vector<float *> z(width), back(width), a(width);
      for (size_t x = 0; x < width; ++x) {
        counts[x] = unsigned(pixels[x].z.size());
        z[x] = pixels[x].z.data();
        back[x] = pixels[x].back.data();
        a[x] = pixels[x].a.data();
      }
      const Imath::Box2i bounds({dw.min.x, int(y)}, {dw.max.x, int(y)});
      Imf::DeepFrameBuffer fb;
      fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, counts.data(), bounds));
      for (const auto &channel : {std::make_pair("Z", &z), std::make_pair("ZBack", &back),
                                  std::make_pair("A", &a)}) {
        const auto s = Imf::Slice::Make(Imf::FLOAT, channel.second->data(), bounds,
                                        sizeof(float *), width * sizeof(float *));
        fb.insert(channel.first, Imf::DeepSlice(Imf::FLOAT, s.base, s.xStride, s.yStride, sizeof(float)));
      }
      file.setFrameBuffer(fb);
      file.writePixels(1);
    }
  }
  output.flush();
  output.close();
  if (before_publish)
    before_publish();
  publication.publish();
}

std::vector<SurfaceSample> reduce_surface(const std::vector<SurfaceSample> &source,
                                          const double tolerance)
{
  if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1e-3)
    throw std::invalid_argument("Deep reduction tolerance must be 0..0.001");
  if (tolerance == 0)
    return source;
  std::vector<SurfaceSample> result;
  result.reserve(source.size());
  double reference = 1, emitted = 1;
  for (size_t i = 0; i < source.size(); ++i) {
    reference *= 1 - source[i].alpha;
    /* Delay small steps, retaining original depths and exact final opacity.
     * This bound is global, not a per-merge allowance that can accumulate. */
    if (emitted - reference > tolerance || i + 1 == source.size()) {
      if (emitted > reference)
        result.push_back({source[i].depth, 1 - reference / emitted});
      emitted = reference;
    }
  }
  return result;
}

void write_deep_exr_rows(Imf::OStream &stream, const SurfaceImage &image, const RowProvider &row)
{
  if (!std::isfinite(image.reduction_error) || image.reduction_error < 0 ||
      image.reduction_error > 1e-3 ||
      (image.reduction_error > 0 && image.reduction_error <= export_error))
    throw std::invalid_argument(
        "Deep reduction error must be zero or greater than 1e-6, up to 0.001");
  const auto header = make_header(image);
  const auto dw = header.dataWindow();
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  {
    Imf::DeepScanLineOutputFile file(stream, header, 1);
    for (int64_t y = dw.min.y; y <= dw.max.y; ++y) {
      SurfaceImage scanline;
      scanline.display_window = image.display_window;
      scanline.pixel_aspect = image.pixel_aspect;
      scanline.view = image.view;
      scanline.compression = image.compression;
      scanline.data_window = {dw.min.x, int(y), dw.max.x, int(y)};
      scanline.pixels = row(int(y));
      auto pixels = prepare(scanline);
      if (image.reduction_error > 0) {
        for (size_t x = 0; x < width; ++x) {
          const auto reduced = reduce_surface(scanline.pixels[x],
                                              image.reduction_error - export_error);
          pixels[x].z.clear();
          pixels[x].a.clear();
          for (const auto &sample : reduced) {
            pixels[x].z.push_back(float(sample.depth));
            pixels[x].a.push_back(float(sample.alpha));
          }
          check_quantization(scanline.pixels[x], pixels[x], image.reduction_error);
        }
      }
      std::vector<unsigned int> counts(width);
      std::vector<float *> z(width), a(width);
      for (size_t x = 0; x < width; ++x) {
        counts[x] = unsigned(pixels[x].z.size());
        z[x] = pixels[x].z.data();
        a[x] = pixels[x].a.data();
      }
      const Imath::Box2i bounds({dw.min.x, int(y)}, {dw.max.x, int(y)});
      Imf::DeepFrameBuffer fb;
      fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, counts.data(), bounds));
      const auto zs = Imf::Slice::Make(
          Imf::FLOAT, z.data(), bounds, sizeof(float *), width * sizeof(float *));
      const auto as = Imf::Slice::Make(
          Imf::FLOAT, a.data(), bounds, sizeof(float *), width * sizeof(float *));
      fb.insert("Z", Imf::DeepSlice(Imf::FLOAT, zs.base, zs.xStride, zs.yStride, sizeof(float)));
      fb.insert("ZBack",
                Imf::DeepSlice(Imf::FLOAT, zs.base, zs.xStride, zs.yStride, sizeof(float)));
      fb.insert("A", Imf::DeepSlice(Imf::FLOAT, as.base, as.xStride, as.yStride, sizeof(float)));
      file.setFrameBuffer(fb);
      file.writePixels(1);
    }
  }
}

void write_deep_exr_rows(const std::filesystem::path &path,
                         const SurfaceImage &image,
                         const RowProvider &row,
                         const std::function<void()> &before_publish)
{
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    write_deep_exr_rows(stream, image, row);
  }
  output.flush();
  output.close();
  if (before_publish)
    before_publish();
  publication.publish();
}

}  // namespace ccl::deep
