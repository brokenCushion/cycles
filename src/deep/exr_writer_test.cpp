/* SPDX-License-Identifier: Apache-2.0 */

#include "deep/exr_writer.h"
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfDoubleAttribute.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/OpenEXRConfig.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
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

void surface_depth_merging(const std::filesystem::path &directory)
{
  PixelLedger ledger{0, 0, {}};
  for (int i = 0; i < 1024; ++i)
    ledger.samples.push_back({uint64_t(i), 1, true, {{double(float(500 + i * .0001)), 1, 0, 1}}});
  const auto surface = reconstruct(ledger);
  std::vector<IntervalSample> source;
  for (const auto &s : surface) source.push_back({s.depth, s.depth, s.alpha, s.object, s.facing});
  const auto merged = merge_surface_depths(source, 1e-4, 1e-6);
  require(source.size() == 1024 && merged.size() == 3, "Sloped surface diameter merging failed");
  for (const auto &s : merged)
    require(s.back - s.front <= 1e-4 * s.front, "Adjacent-step chaining exceeded tolerance");
  require(merge_surface_depths(source, 0, 0).size() == source.size(), "Zero tolerance changed count");
  require(merge_surface_depths(source, 1e-4, 0).size() == source.size(), "Rounding exceeded allowance");
  require(interval_transmittance(merged, 499) == 1 && interval_transmittance(merged, 501) == 0,
          "Opaque merged span produced nonfinite exterior transmittance");
  const std::vector<IntervalSample> pair{{500,500,.2,0,1}, {500.01,500.01,.3,0,1}};
  auto check_barrier = [&](IntervalSample barrier, size_t at) {
    auto curve = pair; curve.insert(curve.begin()+at, barrier);
    require(merge_surface_depths(curve, 1e-4, 1e-6).size() == 3, "Depth merge crossed a barrier");
  };
  check_barrier({500.005,500.005,.1,1,1}, 1);
  check_barrier({500.005,500.005,.1,0,-1}, 1);
  check_barrier({500.005,500.006,.1,0,0}, 1);
  check_barrier({499,501,.1,1,0}, 0);
  auto unknown = pair; unknown[1].facing = 0;
  require(merge_surface_depths(unknown, 1e-4, 1e-6).size() == 2, "Synthetic step merged");
  const auto two = merge_surface_depths(pair, 1e-4, 1e-6);
  require(two.size() == 1 && std::abs(two[0].alpha - .44) < 3e-8, "Group alpha is not the product");
  require(interval_transmittance(pair, 499) == interval_transmittance(two, 499), "Front T changed");
  require(std::abs(interval_transmittance(pair, 501) - interval_transmittance(two, 501)) < 3e-8,
          "Back T changed");
  bool rejected = false;
  try { merge_surface_depths(pair, NAN, 1e-6); } catch (const std::invalid_argument &) { rejected = true; }
  require(rejected, "Nonfinite depth tolerance accepted");

  for (const bool ids : {false, true}) {
    SurfaceImage image{{0,0,0,0}, {0,0,0,0}};
    image.error = 1e-4f; image.z_tolerance = 1e-4f; image.ids = ids;
    image.object_manifest = {{0xabcdef01, "sloped surface"}};
    for (const bool volume : {false, true}) {
      const auto path = directory / (std::string("sloped_1024_") + (ids ? "ids_" : "") +
                                      (volume ? "volume.exr" : "surface.exr"));
      if (volume) write_volume_exr(path, image, {source});
      else write_deep_exr_rows(path, image, [&](int) { return std::vector<std::vector<SurfaceSample>>{surface}; });
      Imf::DeepScanLineInputFile input(path.string().c_str(), 1);
      require(input.header().typedAttribute<Imf::DoubleAttribute>("cycles:deepZTolerance").value() ==
                  double(image.z_tolerance), "Missing depth-domain header");
      unsigned count = 0; Imf::DeepFrameBuffer fb;
      fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, &count, input.header().dataWindow()));
      input.setFrameBuffer(fb); input.readPixelSampleCounts(0,0);
      require(count == 3, "Publication lost surface provenance or did not merge");
      std::vector<float> z(count), back(count), alpha(count);
      float *zp=z.data(), *bp=back.data(), *ap=alpha.data();
      for (const auto &entry : {std::pair<const char *, float **>{"Z", &zp}, {"ZBack", &bp}, {"A", &ap}})
        fb.insert(entry.first, Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(entry.second),
                                             sizeof(float *), sizeof(float *), sizeof(float)));
      input.setFrameBuffer(fb); input.readPixels(0,0);
      double t=1;
      for (size_t i=0;i<count;++i) {
        require(back[i] > z[i] && back[i]-z[i] <= image.z_tolerance*z[i], "Invalid published span");
        t *= 1-double(alpha[i]);
      }
      require(t == 0, "Opaque flattened alpha changed");
    }
  }
  std::cout << "PASS 1024-camera sloped surface, facing/object/volume barriers, span/header and alpha\n";
}

void volume_projection(const std::filesystem::path &directory)
{
  /* Independent rounding passes the export allowance here, but introduces
   * avoidable drift. Keep the cumulative-depth refit active below that limit. */
  std::vector<IntervalSample> source, rounded;
  for (int i = 0; i < 200; ++i) {
    const double front = 1200.00006 + .1 * i;
    const double back = 1200.00006 + .1 * (i + 1);
    const double alpha = -std::expm1(-.00001 * std::min(i + 1, 200 - i));
    source.push_back({front, back, alpha});
    rounded.push_back({double(float(front)), double(float(back)), double(float(alpha))});
  }
  const double drift = interval_curve_error(source, rounded);
  require(drift > 1e-7 && drift < 8.1e-7, "Projection regression does not exercise passing drift");
  const auto path = directory / "volume_projection.exr";
  SurfaceImage image{{0, 0, 0, 0}, {0, 0, 0, 0}};
  write_volume_exr(path, image, {source});

  Imf::DeepScanLineInputFile input(path.string().c_str(), 1);
  unsigned int count = 0;
  Imf::DeepFrameBuffer fb;
  fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, &count, input.header().dataWindow()));
  input.setFrameBuffer(fb);
  input.readPixelSampleCounts(0, 0);
  require(count > 0 && count <= source.size(), "Invalid projected sample count");
  std::vector<float> z(count), back(count), alpha(count);
  float *zp = z.data(), *bp = back.data(), *ap = alpha.data();
  fb.insert("Z", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(&zp),
                               sizeof(float *), sizeof(float *), sizeof(float)));
  fb.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(&bp),
                                   sizeof(float *), sizeof(float *), sizeof(float)));
  fb.insert("A", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(&ap),
                               sizeof(float *), sizeof(float *), sizeof(float)));
  input.setFrameBuffer(fb);
  input.readPixelSampleCounts(0, 0);
  input.readPixels(0, 0);
  std::vector<IntervalSample> actual;
  for (size_t i = 0; i < count; ++i)
    actual.push_back({z[i], back[i], alpha[i]});
  require(interval_curve_error(source, actual) < drift / 5,
          "Volume serialization retains avoidable depth-rounding drift");
  std::cout << "PASS cumulative extinction projection below export allowance\n";
}


void id_round_trip(const std::filesystem::path &directory)
{
  SurfaceImage image{{0, 0, 0, 0}, {0, 0, 0, 0}};
  image.ids = true;
  image.object_manifest = {{0xabcdef01, "cloud\"one\\\n"}, {0x12345678, "cloud two"}};
  const auto path = directory / "deep_ids.exr";
  write_volume_exr(path, image, {{{1, 4, -std::expm1(-.9), 0},
                                {2, 5, -std::expm1(-1.2), 1}}});
  Imf::DeepScanLineInputFile input(path.string().c_str(), 1);
  require(input.header().channels()["id"].type == Imf::UINT, "ID channel must be UINT");
  const auto manifest = input.header().typedAttribute<Imf::StringAttribute>("cycles:deepIDManifest").value();
  require(manifest.find("abcdef01") != std::string::npos &&
          manifest.find("\\u000a") != std::string::npos, "ID manifest escaping failed");
  unsigned int count = 0;
  Imf::DeepFrameBuffer fb;
  fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, &count, input.header().dataWindow()));
  input.setFrameBuffer(fb); input.readPixelSampleCounts(0, 0);
  require(count == 2, "Overlap IDs were collapsed");
  std::vector<unsigned int> ids(count); auto *ip = ids.data();
  std::vector<float> alphas(count); auto *ap = alphas.data();
  fb.insert("id", Imf::DeepSlice(Imf::UINT, reinterpret_cast<char *>(&ip), sizeof(ip), sizeof(ip), sizeof(unsigned int)));
  fb.insert("A", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(&ap), sizeof(ap), sizeof(ap), sizeof(float)));
  input.setFrameBuffer(fb); input.readPixelSampleCounts(0, 0); input.readPixels(0, 0);
  require(ids[0] == 0xabcdef01 && ids[1] == 0x12345678, "UINT IDs lost precision");
  require(std::abs((1 - alphas[0]) - std::exp(-.9)) < 1e-7 &&
          std::abs((1 - alphas[1]) - std::exp(-1.2)) < 1e-7, "ID selection alpha changed");
  image.pixels = {{{2, .2, 0}, {2, .3, 1}}};
  write_deep_exr(directory / "surface_ids.exr", image);
  require(input.header().find("cycles:deepIDHoldoutManifest") == input.header().end(),
          "Ordinary ID output acquired holdout metadata");
  image.holdout_manifest = {image.object_manifest.front()};
  const auto holdout_path = directory / "holdout_ids.exr";
  write_deep_exr(holdout_path, image);
  Imf::DeepScanLineInputFile holdout(holdout_path.string().c_str(), 1);
  const auto marked = holdout.header().typedAttribute<Imf::StringAttribute>(
      "cycles:deepIDHoldoutManifest").value();
  require(marked.find("abcdef01") != std::string::npos &&
          marked.find("12345678") == std::string::npos &&
          marked.find("\\u000a") != std::string::npos,
          "Holdout manifest subset/escaping failed");
  for (const bool ids_enabled : {true, false}) {
    image.ids = ids_enabled;
    image.holdout_manifest = {{0xdeadbeef, "unknown"}};
    bool rejected = false;
    try { write_deep_exr(directory / "invalid_holdout.exr", image); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "Invalid holdout/ID manifest accepted");
  }
  std::cout << "PASS UINT deep ID overlap, selected alpha, surface ties and manifest round-trip\n";
}

void measured_id_publication(const std::filesystem::path &directory)
{
  SurfaceImage image{{0, 0, 0, 0}, {0, 0, 0, 0}};
  image.ids = true;
  image.error = 1e-4f;
  image.object_manifest.push_back({1, "DenseVolume"});
  std::vector<IntervalSample> dense, source;
  for (int i = 0; i < 200; ++i)
    dense.push_back({1200.00006 + .1 * i, 1200.00006 + .1 * (i + 1),
                     -std::expm1(-(.001 + i * .00001)), 0});
  source = dense;
  for (int object = 1; object <= 128; ++object) {
    image.object_manifest.push_back({unsigned(object + 1), "Surface" + std::to_string(object)});
    source.push_back({1400 + object * .125, 1400 + object * .125, .5, object});
  }
  const double allowance = error_budget(image.error).publication;
  /* The first boundary has an unavoidable FLOAT-depth rounding error.
   * Both independent rounding and cumulative projection put it at this depth,
   * proving that even the better variant cannot fit the old equal split. */
  const double boundary_error = -std::expm1(
      -.001 * (dense[0].front - double(float(dense[0].front))) / .1);
  require(boundary_error > allowance / image.object_manifest.size(),
          "Measured allocation fixture would pass equal splitting");
  const auto path = directory / "measured_id_publication.exr";
  write_volume_exr(path, image, {source});
  Imf::DeepScanLineInputFile input(path.string().c_str(), 1);
  unsigned count = 0;
  Imf::DeepFrameBuffer fb;
  fb.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, &count, input.header().dataWindow()));
  input.setFrameBuffer(fb); input.readPixelSampleCounts(0, 0);
  std::vector<unsigned> ids(count);
  std::vector<float> z(count), back(count), alpha(count);
  unsigned *ip = ids.data();
  float *zp = z.data(), *bp = back.data(), *ap = alpha.data();
  fb.insert("id", Imf::DeepSlice(Imf::UINT, reinterpret_cast<char *>(&ip), sizeof(ip), sizeof(ip), sizeof(unsigned)));
  for (auto channel : {std::make_pair("Z", &zp), std::make_pair("ZBack", &bp), std::make_pair("A", &ap)})
    fb.insert(channel.first, Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char *>(channel.second),
                                          sizeof(float *), sizeof(float *), sizeof(float)));
  input.setFrameBuffer(fb); input.readPixelSampleCounts(0, 0); input.readPixels(0, 0);
  double sum = 0;
  for (unsigned object = 0; object < image.object_manifest.size(); ++object) {
    std::vector<IntervalSample> original, actual;
    for (const auto &v : source) if (v.object == int(object)) original.push_back(v);
    for (size_t i = 0; i < count; ++i)
      if (ids[i] == object + 1) actual.push_back({z[i], back[i], alpha[i]});
    require(!actual.empty(), "Measured publication lost an object's samples");
    sum += interval_curve_error(original, actual);
    if (object) require(actual.size() == 1 && actual[0].alpha == .5,
                        "Measured publication altered an exact surface");
  }
  require(sum <= allowance, "Measured publication exceeds the unchanged total");
  require(input.header().typedAttribute<Imf::DoubleAttribute>("cycles:deepError").value() ==
              double(image.error), "Measured allocation changed effective header error");
  /* Borrowed frame metadata must preserve scanline bytes and signed windows. */
  for (const ImageWindow window : {ImageWindow{-2, -1, 1, 1}, ImageWindow{5, 7, 8, 9}}) {
    auto rows = image;
    rows.data_window = rows.display_window = window;
    const auto serial = directory / "id_rows_serial.exr";
    const auto parallel = directory / "id_rows_parallel.exr";
    rows.volume_export_workers = 1;
    write_volume_exr_pixels(serial, rows, [&](int, int) { return source; });
    rows.volume_export_workers = 4;
    write_volume_exr_pixels(parallel, rows, [&](int, int) { return source; });
    std::ifstream a(serial, std::ios::binary), b(parallel, std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(a), {}) ==
                std::string(std::istreambuf_iterator<char>(b), {}),
            "Parallel ID scanline bytes changed");
  }
  image.data_window = image.display_window = {5, 7, 5, 7};
  source.erase(source.begin(), source.begin() + dense.size());
  source.insert(source.begin(), {2.00000001, 2.00000002, .5, 0});
  bool failed = false;
  try { write_volume_exr(directory / "measured_id_failure.exr", image, {source}); }
  catch (const std::invalid_argument &error) {
    const std::string message = error.what();
    failed = message.find("pixel 5,7") != std::string::npos &&
             message.find("objects=129") != std::string::npos &&
             message.find("DenseVolume") != std::string::npos;
  }
  require(failed, "Measured publication overflow lost pixel/object/contributor diagnostics");
  require(!std::filesystem::exists(directory / "measured_id_failure.exr"),
          "Rejected measured publication created an EXR");
  image.data_window = image.display_window = {5, 7, 6, 8};
  failed = false;
  try {
    write_volume_exr_pixels(directory / "measured_id_row_failure.exr", image,
        [&](int x, int y) {
          return x == 6 && y == 8 ? source : std::vector<IntervalSample>{{1, 1, .5, 1}};
        });
  }
  catch (const std::invalid_argument &error) {
    failed = std::string(error.what()).find("pixel 6,8") != std::string::npos;
  }
  require(failed && !std::filesystem::exists(directory / "measured_id_row_failure.exr"),
          "Borrowed ID metadata lost the scanline failure coordinate or atomic publication");
  std::cout << "PASS measured ID publication: equal split fails; final sum=" << sum
            << " <= " << allowance << ", explicit contributor diagnostics\n";
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
  bad.deep_samples = -1;
  expect_invalid(bad);
  bad = valid;
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
    FaultStream row_stream(budget);
    threw = false;
    try {
      write_deep_exr_rows(row_stream, valid, [&](int y) {
        const size_t offset = size_t(y - valid.data_window.min_y) * 4;
        return std::vector<std::vector<SurfaceSample>>(valid.pixels.begin() + offset,
                                                       valid.pixels.begin() + offset + 4);
      });
    }
    catch (const std::exception &) {
      threw = true;
    }
    require(threw, "Injected scanline writer failure was swallowed");
    FaultStream volume_stream(budget);
    SurfaceImage volume_image = valid;
    volume_image.pixels.clear();
    threw = false;
    try {
      write_volume_exr_pixels(volume_stream, volume_image, [](int, int) {
        std::vector<IntervalSample> intervals;
        for (int i = 0; i < 128; ++i)
          intervals.push_back({double(i + 1), i + 1.5, .1});
        return intervals;
      });
    }
    catch (const std::exception &error) {
      require(std::string(error.what()).find("Injected I/O failure") != std::string::npos,
              "Volume fault test failed before reaching the stream");
      threw = true;
    }
    require(threw, "Injected volume writer failure was swallowed");
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
    std::filesystem::create_directories(directory / "settings");
    for (float error : {0.f, 1e-4f, 1e-3f}) {
      auto image = base;
      image.error = error;
      const auto path = directory / "settings" / ("setting_" + std::to_string(error) + ".exr");
      write_deep_exr(path, image);
      Imf::DeepScanLineInputFile input(path.string().c_str());
      const auto *attribute = input.header().findTypedAttribute<Imf::DoubleAttribute>("cycles:deepError");
      require(error ? attribute && attribute->value() == double(error) : attribute == nullptr,
              "Effective error header/strict header preservation mismatch");
    }
    for (int count : {0, 1, 64}) {
      auto image = base;
      image.deep_samples = count;
      const auto path = directory / "settings" / ("samples_" + std::to_string(count) + ".exr");
      write_deep_exr(path, image);
      Imf::DeepScanLineInputFile input(path.string().c_str());
      const auto *attribute = input.header().findTypedAttribute<Imf::IntAttribute>("cycles:deepSamples");
      require(count ? attribute && attribute->value() == count : attribute == nullptr,
              "Deep sample limit header/legacy preservation mismatch");
    }
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
        write_deep_exr_rows(path, image, [&](int y) {
          const size_t offset = size_t(y - image.data_window.min_y) * 4;
          return std::vector<std::vector<SurfaceSample>>(image.pixels.begin() + offset,
                                                         image.pixels.begin() + offset + 4);
        });
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
    volume_projection(directory);
    id_round_trip(directory);
    measured_id_publication(directory);
    surface_depth_merging(directory);
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
