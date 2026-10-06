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
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

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
  const auto budget = error_budget(image.error);
  if (image.error && image.reduction_error && image.reduction_error != budget.effective)
    throw std::invalid_argument("Surface reduction must use the shared deep error setting");
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
      Imf::DoubleAttribute(image.error ? budget.effective :
                          (image.reduction_error ? image.reduction_error : export_error)));
  if (image.error)
    header.insert("cycles:deepError", Imf::DoubleAttribute(budget.effective));
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

/* Refit continuous volume runs at representable depth boundaries. Rounding
 * depths while retaining every local alpha shifts extinction between cells.
 * Sampling the original cumulative optical depth avoids that drift. Surface
 * steps and gaps remain separate; the full-curve check still decides acceptance. */
static std::vector<IntervalSample> project_volume_depths(const std::vector<IntervalSample> &source)
{
  std::vector<IntervalSample> result;
  result.reserve(source.size());
  for (size_t begin = 0; begin < source.size();) {
    if (source[begin].front == source[begin].back) {
      result.push_back({double(float(source[begin].front)), double(float(source[begin].back)),
                        double(float(source[begin].alpha))});
      ++begin;
      continue;
    }
    size_t end = begin + 1;
    while (end < source.size() && source[end].front == source[end - 1].back &&
           source[end].back > source[end].front)
      ++end;
    double total = 0;
    for (size_t i = begin; i < end; ++i)
      total -= std::log1p(-source[i].alpha);
    double front = double(float(source[begin].front));
    const double last = double(float(source[end - 1].back));
    if (front == last) {
      const float alpha = float(-std::expm1(-total));
      if (alpha > 0)
        result.push_back({front, last, alpha});
    }
    else {
      size_t cursor = begin;
      double completed = 0, published = 0;
      for (size_t i = begin; i < end; ++i) {
        const double back = double(float(source[i].back));
        if (back == front)
          continue;
        while (cursor < end && source[cursor].back <= back)
          completed -= std::log1p(-source[cursor++].alpha);
        double target = completed;
        if (back == last)
          target = total;
        else if (cursor < end) {
          const auto &s = source[cursor];
          target -= std::log1p(-s.alpha) *
                    std::clamp((back - s.front) / (s.back - s.front), 0.0, 1.0);
        }
        const float alpha = std::min(float(-std::expm1(-std::max(0.0, target - published))),
                                     std::nextafter(1.0f, 0.0f));
        if (alpha > 0) {
          result.push_back({front, back, alpha});
          published -= std::log1p(-double(alpha));
        }
        front = back;
      }
    }
    begin = end;
  }
  return result;
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
    /* Preserve cumulative extinction at FLOAT boundaries even when independent
     * rounding would pass this pixel's error allowance. Otherwise two nearby
     * device curves can drift in opposite directions during serialization. */
    auto quantized = project_volume_depths(source[p]);
    /* Reserve 1e-7 for cubic fitting, 5e-8 for sample reconstruction and
     * 4e-8 for FLOAT nonnegative density controls. For normal coefficients,
     * rounding changes tau by at most 2^-24 relatively; the corresponding
     * absolute exp(-tau) error is below 2.2e-8. The reserve also covers
     * subnormal contributions from the bounded number of cell records. */
    double error = interval_curve_error(source[p], quantized);
    const auto budget = error_budget(image.error);
    const double allowance = budget.publication;
    if (quantized.size() > 64) {
      /* Fewer small alpha contributions reduce consumer FLOAT accumulation
       * error as well as storage. Spend part of the existing export allowance;
       * the original source still checks EVERY boundary and interior extremum.
       * Do not merge steps/gaps or weaken the frame's 1e-6 curve budget. */
      const auto reduced = reduce_interval_curve(quantized, budget.coalescing);
      if (reduced.size() < quantized.size()) {
        auto projected = project_volume_depths(reduced);
        const double candidate_error = interval_curve_error(source[p], projected);
        if (candidate_error <= allowance) {
          quantized = std::move(projected);
          error = candidate_error;
        }
      }
    }
    if (error > allowance) {
      std::vector<IntervalSample> rounded;
      rounded.reserve(source[p].size());
      for (const auto &s : source[p]) {
        rounded.push_back({double(float(s.front)), double(float(s.back)), double(float(s.alpha))});
      }
      const double rounded_error = interval_curve_error(source[p], rounded);
      if (rounded_error < error) {
        error = rounded_error;
        quantized = std::move(rounded);
      }
    }
    if (error > allowance) {
      const size_t width = size_t(image.data_window.max_x) - image.data_window.min_x + 1;
      std::ostringstream message;
      message << "FLOAT volume curve exceeds transmittance error budget at pixel "
              << int64_t(image.data_window.min_x) + int64_t(p % width) << ','
              << int64_t(image.data_window.min_y) + int64_t(p / width)
              << ": " << error << " > " << allowance;
      throw std::invalid_argument(message.str());
    }
    pixels[p].z.reserve(quantized.size());
    pixels[p].back.reserve(quantized.size());
    pixels[p].a.reserve(quantized.size());
    for (const auto &s : quantized) {
      pixels[p].z.push_back(float(s.front));
      pixels[p].back.push_back(float(s.back));
      pixels[p].a.push_back(float(s.alpha));
    }
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

void write_volume_exr_pixels(Imf::OStream &stream,
                            const SurfaceImage &image,
                            const VolumePixelProvider &pixel,
                            const RowCallback &begin_row, const RowCallback &end_row)
{
  if (image.volume_export_workers <= 0)
    throw std::invalid_argument("Deep export requires positive worker count");
  auto header = make_header(image);
  header.insert("cycles:deepScope", Imf::StringAttribute("native_scalar_extinction"));
  const auto dw = header.dataWindow();
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  tbb::task_arena arena(image.volume_export_workers);
  tbb::task_group_context context;
  context.capture_fp_settings();
  {
    /* Rows are submitted synchronously. Zero workers keeps one OpenEXR line
     * buffer instead of the two allocated for one worker (capture budget). */
    Imf::DeepScanLineOutputFile file(stream, header, 0);
    for (int64_t y = dw.min.y; y <= dw.max.y; ++y) {
      if (begin_row) begin_row(int(y));
      std::vector<FloatPixel> pixels(width);
      std::atomic<size_t> row_samples{0};
      const auto prepare_pixel = [&](const size_t x) {
        SurfaceImage metadata = image;
        const int file_x = int(int64_t(dw.min.x) + x);
        metadata.data_window = {file_x, int(y), file_x, int(y)};
        std::vector<std::vector<IntervalSample>> source(1);
        source[0] = pixel(file_x, int(y));
        std::vector<FloatPixel> converted;
        {
          ExportTimer timer{image.export_statistics, ExportStatistics::Quantize};
          converted = prepare_volume(metadata, source);
        }
        /* Capture reserves DOUBLE fitting scratch separately. The scanline
         * retains only the final FLOAT arrays, including their capacities. */
        const size_t retained = std::max({converted[0].z.capacity(),
                                         converted[0].back.capacity(),
                                         converted[0].a.capacity()});
        size_t previous = row_samples.load(std::memory_order_relaxed);
        do {
          const size_t limit = image.volume_row_sample_limit ?
                                   image.volume_row_sample_limit : SIZE_MAX;
          if (retained > limit - previous)
            throw std::runtime_error("Deep volume scanline exceeds memory budget at row " +
                                     std::to_string(y) + "; increase deep-memory-mb");
        } while (!row_samples.compare_exchange_weak(previous, previous + retained,
                                                    std::memory_order_relaxed));
        pixels[x] = std::move(converted[0]);
      };
      if (image.volume_export_workers == 1)
        for (size_t x = 0; x < width; ++x)
          prepare_pixel(x);
      else
        arena.execute([&] { tbb::parallel_for(size_t(0), width, prepare_pixel, context); });
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
      {
        ExportTimer timer{image.export_statistics, ExportStatistics::Serialize};
        file.writePixels(1);
      }
      if (end_row) end_row(int(y));
    }
  }
}

void write_volume_exr_pixels(const std::filesystem::path &path,
                            const SurfaceImage &image,
                            const VolumePixelProvider &pixel,
                            const std::function<void()> &before_publish,
                            const RowCallback &begin_row, const RowCallback &end_row)
{
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    write_volume_exr_pixels(stream, image, pixel, begin_row, end_row);
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
  if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1e-2)
    throw std::invalid_argument("Deep reduction tolerance must be 0..0.01");
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

void write_deep_exr_rows(Imf::OStream &stream, const SurfaceImage &image, const RowProvider &row,
                         const RowCallback &begin_row, const RowCallback &end_row)
{
  if (!std::isfinite(image.reduction_error) || image.reduction_error < 0 ||
      image.reduction_error > 1e-2 ||
      (image.reduction_error > 0 && image.reduction_error <= export_error))
    throw std::invalid_argument(
        "Deep reduction error must be zero or greater than 1e-6, up to 0.01");
  const auto header = make_header(image);
  const auto dw = header.dataWindow();
  const size_t width = size_t(int64_t(dw.max.x) - dw.min.x + 1);
  {
    Imf::DeepScanLineOutputFile file(stream, header, 1);
    for (int64_t y = dw.min.y; y <= dw.max.y; ++y) {
      if (begin_row) begin_row(int(y));
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
      {
        ExportTimer timer{image.export_statistics, ExportStatistics::Serialize};
        file.writePixels(1);
      }
      if (end_row) end_row(int(y));
    }
  }
}

void write_deep_exr_rows(const std::filesystem::path &path,
                         const SurfaceImage &image,
                         const RowProvider &row,
                         const std::function<void()> &before_publish,
                         const RowCallback &begin_row, const RowCallback &end_row)
{
  AtomicOutput publication(path);
  std::ofstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output.open(publication.temporary(), std::ios::binary | std::ios::trunc);
  {
    Imf::StdOFStream stream(output, path.string().c_str());
    write_deep_exr_rows(stream, image, row, begin_row, end_row);
  }
  output.flush();
  output.close();
  if (before_publish)
    before_publish();
  publication.publish();
}

}  // namespace ccl::deep
