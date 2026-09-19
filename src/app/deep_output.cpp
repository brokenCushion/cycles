/* SPDX-License-Identifier: Apache-2.0 */
#include "app/deep_output.h"
#include "deep/exr_writer.h"
#include "deep/publication.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

CCL_NAMESPACE_BEGIN
static void write_deep_tile(const OutputDriver::DeepTile &tile,
                        const string &path,
                        const string &records_path,
                        const string &beauty_path,
                        const bool reduce,
                        const std::function<bool()> &cancelled)
{
  deep::SurfaceImage image;
  image.display_window = {0, 0, tile.width - 1, tile.height - 1};
  image.data_window = image.display_window;
  image.compression = deep::DeepCompression::Zips;
  image.reduction_error = reduce ? 1e-3 : 0;
  const auto check_cancel = [&] {
    if (cancelled())
      throw std::runtime_error("Deep export cancelled; final EXR was not replaced");
  };
  check_cancel();
  if (!beauty_path.empty()) {
    /* Pair the exact already-written beauty bytes, including its header,
     * with this deep export. FNV-1a is an identity checksum, not security. */
    std::ifstream beauty(beauty_path, std::ios::binary);
    if (!beauty)
      throw std::runtime_error("Cannot read required beauty output for deep pairing");
    uint64_t hash = UINT64_C(14695981039346656037), bytes = 0;
    char buffer[65536];
    while (beauty) {
      check_cancel();
      beauty.read(buffer, sizeof(buffer));
      for (std::streamsize i = 0; i < beauty.gcount(); ++i) {
        hash ^= static_cast<unsigned char>(buffer[i]);
        hash *= UINT64_C(1099511628211);
        ++bytes;
      }
    }
    if (!beauty.eof())
      throw std::runtime_error("Beauty identity read failed");
    image.beauty_identity = "fnv1a64:" + std::to_string(hash) + ":bytes:" + std::to_string(bytes) +
                            ":path:" + beauty_path;
  }
  if (!records_path.empty()) {
    deep::AtomicOutput publication(records_path);
    std::ofstream records(publication.temporary());
    records.exceptions(std::ios::badbit | std::ios::failbit);
    records << (tile.volume ? "file_x,file_y,sample,front,back,value,kind,event\n" :
                                    "file_x,file_y,sample,depth,alpha,event\n")
            << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (int y = 0; y < tile.height; ++y) {
      check_cancel();
      for (int x = 0; x < tile.width; ++x)
        for (int sample = 0; sample < tile.population(x, tile.height - 1 - y); ++sample) {
          if (tile.volume) {
            const auto v = tile.get_camera_sample(x, tile.height - 1 - y, sample);
            size_t event = 0;
            for (const auto &interval : v.intervals)
              records << x << ',' << y << ',' << sample << ',' << interval.front << ','
                      << interval.back << ',' << interval.optical_depth << ",volume," << event++ << '\n';
            for (const auto &surface : v.camera.events)
              records << x << ',' << y << ',' << sample << ',' << surface.depth << ','
                      << surface.depth << ',' << surface.alpha << ",surface," << event++ << '\n';
            if (!event)
              records << x << ',' << y << ',' << sample << ",0,0,0,miss,-1\n";
            continue;
          }
          const auto events = tile.get_camera_sample(x, tile.height - 1 - y, sample).camera.events;
          if (events.empty())
            records << x << ',' << y << ',' << sample << ",0,0,-1\n";
          for (size_t i = 0; i < events.size(); ++i)
            records << x << ',' << y << ',' << sample << ',' << events[i].depth << ','
                    << events[i].alpha << ',' << i << '\n';
        }
    }
    records.close();
    check_cancel();
    publication.publish();
  }
  if (tile.volume) {
    deep::write_volume_exr_rows(path, image, [&](const int y) {
      check_cancel();
      std::vector<std::vector<deep::IntervalSample>> row;
      row.reserve(tile.width);
      for (int x = 0; x < tile.width; ++x)
        row.push_back(tile.get_pixel(x, tile.height - 1 - y));
      check_cancel();
      return row;
    }, check_cancel);
    return;
  }
  deep::write_deep_exr_rows(
      path,
      image,
      [&](const int y) {
        check_cancel();
        std::vector<std::vector<deep::SurfaceSample>> row;
        row.reserve(tile.width);
        for (int x = 0; x < tile.width; ++x) {
          std::vector<deep::SurfaceSample> pixel;
          for (const auto &sample : tile.get_pixel(x, tile.height - 1 - y))
            pixel.push_back({sample.front, sample.alpha});
          row.push_back(std::move(pixel));
        }
        check_cancel();
        return row;
      },
      check_cancel);
}
DeepOutputDriver::DeepOutputDriver(const string_view beauty_path, const string_view pass,
                                    LogFunction log, const string_view deep_path,
                                    const string_view records_path, const bool reduce)
    : OIIOOutputDriver(beauty_path, pass, std::move(log)),
      deep_path_(deep_path), records_path_(records_path), reduce_(reduce) {}

void DeepOutputDriver::write_render_tile(const Tile &tile)
{
  if (!filepath_.empty()) {
    written_ = false;
    OIIOOutputDriver::write_render_tile(tile);
  }
}

void DeepOutputDriver::write_deep_render_tile(const DeepTile &tile)
{
  if (!filepath_.empty() && !written_) {
    throw std::runtime_error("Required beauty output was not successfully written and closed");
  }
  if (tile.volume && reduce_) {
    throw std::invalid_argument("Deep: surface reduction is unsupported for volumes");
  }
  write_deep_tile(tile, deep_path_, records_path_, filepath_, reduce_,
                  [&tile] { return tile.cancelled(); });
}
CCL_NAMESPACE_END
