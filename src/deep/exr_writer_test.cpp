/* SPDX-License-Identifier: Apache-2.0 */

#include "deep/exr_writer.h"
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/OpenEXRConfig.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ccl::deep;
namespace {
double max_curve_error = 0.0;

void require(bool value, const std::string &message)
{
  if (!value) {
    throw std::runtime_error(message);
  }
}

std::vector<PixelLedger> ledgers()
{
  std::vector<PixelLedger> p = {
      {0, 0, {{0, 1, true, {}}, {1, 1, true, {}}}},
      {1, 0, {{0, 1, true, {{2, 1}}}}},
      {2, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {}}}},
      {3, 0, {{0, 1, true, {{2, 1}}}, {1, 1, true, {{8, 1}}}}},
      {0, 1, {{0, 1, true, {{2, 0.25}, {8, 0.5}}}}},
      {1, 1, {{0, 1, true, {{2, 0.25}, {8, 0.5}, {10, 1}}}}},
      {2, 1, {{0, 1, true, {{2, 1}}}, {1, 3, true, {}}}},
      {3, 1, {{0, 1, true, {{2, 0.25}, {2, 0.5}}}}},
      {0, 2, {{0, 1, true, {{2, 0}}}}},
      {1, 2, {{0, 1, true, {}}}},
      {2, 2, {{0, 2, true, {{2, 0.25}, {8, 0.5}}}, {1, 1, true, {{8, 1}}}, {2, 1, true, {}}}},
      {3, 2, {{0, 1, true, {{2, 0.3}}}}}};
  for (int i = 0; i < 256; ++i) {
    p[9].samples[0].events.push_back({1.0 + i * 0.25, 0.0001});
  }
  return p;
}

SurfaceImage fixture(const std::vector<PixelLedger> &source)
{
  SurfaceImage image{{0, 0, 3, 2}, {0, 0, 3, 2}};
  image.frame = 1001;
  image.view = "left";
  for (const auto &p : source) {
    image.pixels.push_back(reconstruct(p));
  }
  return image;
}

double raw_transmittance(const PixelLedger &pixel, double depth)
{
  double sum = 0, weight = 0;
  for (const auto &sample : pixel.samples) {
    double t = 1;
    for (const auto &event : sample.events) {
      if (event.depth <= depth) {
        t *= 1 - event.alpha;
      }
    }
    sum += sample.weight * t;
    weight += sample.weight;
  }
  return sum / weight;
}

bool same_window(const Imath::Box2i &actual, const ImageWindow &expected)
{
  return actual.min.x == expected.min_x && actual.min.y == expected.min_y &&
         actual.max.x == expected.max_x && actual.max.y == expected.max_y;
}

/* Reader uses the OpenEXR API directly, not writer internals, and allocates
 * channels separately (including a separate ZBack array). Only known bounded
 * fixtures are read; sample counts are checked before allocating storage. */
void round_trip(const std::filesystem::path &path,
                const SurfaceImage &expected,
                const std::vector<PixelLedger> &source)
{
  Imf::DeepScanLineInputFile input(path.string().c_str(), 1);
  const auto &header = input.header();
  require(input.isComplete(), "Incomplete EXR");
  require(header.type() == Imf::DEEPSCANLINE && header.version() == 1,
          "Wrong deep file type/version");
  require(same_window(header.dataWindow(), expected.data_window), "Wrong data window");
  require(same_window(header.displayWindow(), expected.display_window), "Wrong display window");
  require(header.pixelAspectRatio() == expected.pixel_aspect, "Wrong pixel aspect");
  require(header.view() == expected.view, "Wrong view");
  require(header.typedAttribute<Imf::IntAttribute>("cycles:frame").value() == expected.frame,
          "Wrong frame");
  require(header.typedAttribute<Imf::StringAttribute>("cycles:depthConvention").value() ==
              "positive_axial_camera_z",
          "Wrong depth convention");
  require(header.typedAttribute<Imf::StringAttribute>("cycles:depthUnits").value() ==
              "scene_units",
          "Wrong depth units");
  require(header.lineOrder() == Imf::INCREASING_Y, "Wrong line order");
  require(header.compression() == (expected.compression == DeepCompression::None ?
                                       Imf::NO_COMPRESSION :
                                       Imf::ZIPS_COMPRESSION),
          "Wrong compression");
  int channels = 0;
  for (auto it = header.channels().begin(); it != header.channels().end(); ++it) {
    ++channels;
  }
  require(channels == 3, "Unexpected channel count");
  for (const char *name : {"Z", "ZBack", "A"}) {
    const auto *channel = header.channels().findChannel(name);
    require(channel && channel->type == Imf::FLOAT && channel->xSampling == 1 &&
                channel->ySampling == 1,
            "Wrong channel type/sampling");
  }
  const auto dw = header.dataWindow();
  const size_t width = size_t(dw.max.x - dw.min.x + 1);
  const size_t n = expected.pixels.size();
  std::vector<unsigned int> counts(n, 0);
  Imf::DeepFrameBuffer fb;
  fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, counts.data(), dw));
  input.setFrameBuffer(fb);
  input.readPixelSampleCounts(dw.min.y, dw.max.y);
  std::vector<std::vector<float>> z(n), back(n), alpha(n);
  std::vector<float *> zp(n), bp(n), ap(n);
  for (size_t i = 0; i < n; ++i) {
    require(counts[i] == expected.pixels[i].size(), "Sample count mismatch / pixel misalignment");
    z[i].resize(counts[i]);
    back[i].resize(counts[i]);
    alpha[i].resize(counts[i]);
    zp[i] = counts[i] ? z[i].data() : nullptr;
    bp[i] = counts[i] ? back[i].data() : nullptr;
    ap[i] = counts[i] ? alpha[i].data() : nullptr;
  }
  auto insert = [&](const char *name, std::vector<float *> &pointers) {
    auto slice = Imf::Slice::Make(
        Imf::FLOAT, pointers.data(), dw, sizeof(float *), width * sizeof(float *));
    fb.insert(name,
              Imf::DeepSlice(Imf::FLOAT, slice.base, slice.xStride, slice.yStride, sizeof(float)));
  };
  insert("Z", zp);
  insert("ZBack", bp);
  insert("A", ap);
  input.setFrameBuffer(fb);
  /* Counts must be read against the active framebuffer before pixel reads. */
  input.readPixelSampleCounts(dw.min.y, dw.max.y);
  input.readPixels(dw.min.y, dw.max.y);
  for (size_t i = 0; i < n; ++i) {
    std::vector<double> boundaries = {0};
    for (size_t j = 0; j < counts[i]; ++j) {
      require(z[i][j] == float(expected.pixels[i][j].depth) && back[i][j] == z[i][j],
              "Depth or ZBack mismatch");
      require(alpha[i][j] == float(expected.pixels[i][j].alpha), "Alpha mismatch");
      boundaries.push_back(z[i][j]);
    }
    for (const auto &sample : source[i].samples) {
      for (const auto &event : sample.events) {
        boundaries.push_back(event.depth);
      }
    }
    for (double boundary : boundaries) {
      for (double depth : {std::nextafter(boundary, -INFINITY), boundary}) {
        double t = 1;
        for (size_t j = 0; j < counts[i]; ++j) {
          if (z[i][j] <= depth) {
            t *= 1 - double(alpha[i][j]);
          }
        }
        double error = std::abs(t - raw_transmittance(source[i], depth));
        require(std::isfinite(error) && error <= 1e-6,
                "Exported transmittance error exceeds 1e-6");
        max_curve_error = std::max(max_curve_error, error);
      }
    }
  }
  std::cout << "PASS round-trip: " << path.filename().string() << '\n';
}

class FaultStream : public Imf::OStream {
 public:
  explicit FaultStream(uint64_t budget) : Imf::OStream("injected-write-failure"), budget_(budget)
  {
  }
  void write(const char *, int n) override
  {
    if (uint64_t(n) > budget_) {
      throw std::runtime_error("Injected I/O failure");
    }
    budget_ -= n;
    position_ += n;
  }
  uint64_t tellp() override
  {
    return position_;
  }
  void seekp(uint64_t position) override
  {
    position_ = position;
  }

 private:
  uint64_t budget_, position_ = 0;
};

void expect_invalid(const SurfaceImage &image)
{
  FaultStream stream(std::numeric_limits<uint64_t>::max());
  try {
    write_deep_exr(stream, image);
  }
  catch (const std::invalid_argument &) {
    require(stream.tellp() == 0, "Validation wrote bytes before rejecting input");
    return;
  }
  throw std::runtime_error("Invalid image accepted");
}

void failures(const SurfaceImage &valid, const std::filesystem::path &directory)
{
  auto bad = valid;
  bad.pixels.pop_back();
  expect_invalid(bad);
  bad = valid;
  bad.data_window.max_x = bad.data_window.min_x - 1;
  expect_invalid(bad);
  bad = valid;
  bad.display_window = {0, 0, -1, 0};
  expect_invalid(bad);
  bad = valid;
  bad.data_window = {INT_MIN, 0, INT_MAX, 0};
  expect_invalid(bad);
  bad = valid;
  bad.pixel_aspect = 0;
  expect_invalid(bad);
  bad = valid;
  bad.pixel_aspect = NAN;
  expect_invalid(bad);
  bad = valid;
  bad.view = "";
  expect_invalid(bad);
  bad = valid;
  bad.compression = static_cast<DeepCompression>(999);
  expect_invalid(bad);
  for (double depth : {0.0,
                       -1.0,
                       double(INFINITY),
                       double(NAN),
                       std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::min()})
  {
    bad = valid;
    bad.pixels[1][0].depth = depth;
    expect_invalid(bad);
  }
  for (double a :
       {0.0, -0.1, 1.1, double(INFINITY), double(NAN), std::numeric_limits<double>::min()})
  {
    bad = valid;
    bad.pixels[1][0].alpha = a;
    expect_invalid(bad);
  }
  bad = valid;
  std::reverse(bad.pixels[3].begin(), bad.pixels[3].end());
  expect_invalid(bad);
  bad = valid;
  bad.pixels[3][1].depth = bad.pixels[3][0].depth;
  expect_invalid(bad);
  /* FLOAT rounding shifts this opaque boundary: near-zero depth error is not
   * equivalent to near-zero transmittance error. */
  bad = valid;
  bad.pixels[1][0].depth = 2.00000001;
  expect_invalid(bad);
  for (uint64_t budget : {uint64_t(0), uint64_t(64), uint64_t(2048)}) {
    FaultStream stream(budget);
    bool threw = false;
    try {
      write_deep_exr(stream, valid);
    }
    catch (const std::exception &) {
      threw = true;
    }
    require(threw, "Injected writer failure was swallowed");
  }
  /* Opening a directory as a file is a portable open failure without relying
   * on machine-specific permissions or filling a real disk. */
  bool threw = false;
  try {
    write_deep_exr(directory, valid);
  }
  catch (const std::exception &) {
    threw = true;
  }
  require(threw, "Open failure was swallowed");
  const auto protected_file = directory / "validation-preserves-existing.txt";
  {
    std::ofstream f(protected_file);
    f << "preserve";
  }
  threw = false;
  try {
    write_deep_exr(protected_file, bad);
  }
  catch (const std::invalid_argument &) {
    threw = true;
  }
  std::ifstream f(protected_file);
  std::string contents;
  f >> contents;
  require(threw && contents == "preserve", "Validation truncated existing output");
  std::cout << "PASS invalid inputs, quantization guard, open and injected write failures\n";
}
}  // namespace

int main(int argc, char **argv)
{
  try {
    require(argc == 2, "Usage: cycles_deep_exr_test OUTPUT_DIRECTORY");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    const auto source = ledgers();
    const auto base = fixture(source);
    for (const auto compression : {DeepCompression::None, DeepCompression::Zips}) {
      const std::string suffix = compression == DeepCompression::None ? "none" : "zips";
      for (int window = 0; window < 3; ++window) {
        auto image = base;
        image.compression = compression;
        if (window == 1) {
          image.data_window = {-2, -1, 1, 1};
          image.display_window = {-4, -3, 5, 6};
          image.pixel_aspect = 1.5f;
        }
        if (window == 2) {
          image.data_window = {5, 7, 8, 9};
          image.display_window = {0, 0, 11, 13};
          image.pixel_aspect = 0.75f;
        }
        const auto path = directory /
                          ("surfaces_" + std::to_string(window) + "_" + suffix + ".exr");
        write_deep_exr(path, image);
        round_trip(path, image, source);
      }
      auto empty_source = source;
      for (auto &pixel : empty_source) {
        for (auto &sample : pixel.samples) {
          sample.events.clear();
        }
      }
      auto empty = fixture(empty_source);
      empty.compression = compression;
      const auto path = directory / ("empty_" + suffix + ".exr");
      write_deep_exr(path, empty);
      round_trip(path, empty, empty_source);
    }
    failures(base, directory);
    std::ofstream manifest(directory / "expected_pixels.csv");
    manifest.exceptions(std::ios::badbit | std::ios::failbit);
    manifest << "file_x,file_y,samples,flattened_alpha\n" << std::setprecision(17);
    for (size_t i = 0; i < source.size(); ++i) {
      manifest << source[i].x << ',' << source[i].y << ',' << base.pixels[i].size() << ','
               << 1 - raw_transmittance(source[i], INFINITY) << '\n';
    }
    manifest.close();
    std::cout << "OpenEXR " << OPENEXR_VERSION_STRING << '\n'
              << std::setprecision(17)
              << "Maximum absolute exported transmittance error: " << max_curve_error << '\n';
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
