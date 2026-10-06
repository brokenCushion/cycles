/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ccl::deep {
Capture::Capture(const int width,
                 const int height,
                 const int samples,
                 const size_t max_bytes,
                 const int max_events,
                 const bool spill,
                 const bool adaptive,
                 const bool volume,
                 const bool volume_grid,
                 const int export_workers,
                 const float error)
    : error_setting_(error), width_(width), height_(height), samples_(samples), max_events_(max_events), volume_(volume),
      volume_grid_(volume_grid)
{
  error_budget(error);
  if (export_workers <= 0)
    throw std::invalid_argument("Deep export requires positive worker count");
  if (max_events < 0 || max_events > int(volume_grid ? DEEP_MAX_VOLUME_EVENTS : DEEP_MAX_EVENTS) ||
      (volume && !max_events) || (volume_grid && !volume))
    throw std::invalid_argument("Invalid deep traversal capacity or volume mode");
  if (width <= 0 || height <= 0 || samples <= 0 || samples > 4096)
    throw std::invalid_argument("Deep capture requires positive dimensions and 1..4096 samples");
  size_t count = size_t(width);
  for (const size_t factor : {size_t(height), size_t(samples)}) {
    if (count > std::numeric_limits<size_t>::max() / factor)
      throw std::invalid_argument("Deep capture dimensions overflow");
    count *= factor;
  }
  count_ = count;
  capacity_ = size_t(std::max(1, max_events));
  stride_ = sizeof(KernelDeepResult) + capacity_ *
             (sizeof(KernelDeepEvent) + (volume_grid ? sizeof(KernelDeepDensity) : 0));
  if (count > size_t(INT64_MAX) / stride_)
    throw std::invalid_argument("Deep spill file offsets overflow");
  if (adaptive && count / samples > max_bytes / sizeof(uint32_t))
    throw std::invalid_argument("Deep adaptive populations exceed memory budget");
  const size_t population_bytes = adaptive ? count / samples * sizeof(uint32_t) : 0;
  if (spill) {
    /* Two MiB covers 512-lane host/device events and 64 media (1,863,680 bytes),
     * fixed scratch and I/O. Beauty/shader memory remains outside this budget. */
    const uint64_t events = uint64_t(samples) * capacity_;
    const uint64_t output_events = volume ? reconstruction_limit() : events;
    /* Volume pixel streaming retains only 12-byte FLOAT samples per row.
     * OpenEXR 3.4 ZIPS with zero workers has one line buffer: raw + consecutive
     * + ZIP scratch + compressed data, plus a transient replacement buffer.
     * 80 bytes/sample covers these copies and compression overhead. Double
     * source/quantization/curve scratch is bounded per pixel, not per row. */
    const uint64_t row_sample_bytes = volume ? 80 : 128;
    rows_per_band_ = (height - 1) / 64 + 1;
    const size_t band_count = (height - 1) / rows_per_band_ + 1;
#ifdef _WIN32
    if (band_count * 2 + size_t(rows_per_band_) + 16 > size_t(_getmaxstdio()))
      throw std::invalid_argument("Deep band files exceed stdio open-file limit");
#endif
    spill_memory_bytes_ = size_t((max_bytes - population_bytes) / 8);
    const uint64_t shared = 2 * 1024 * 1024 +
                              (spill_page_bytes * spill_page_count + event_page_bytes) * band_count +
                              spill_memory_bytes_ +
                              uint64_t(width) * 256 +
                              uint64_t(height) * 32;
    const uint64_t worker =
                              (volume ? uint64_t(capacity_) * 512 : events * 512) +
                              (volume ? output_events * 240 : 0) +
                              (volume ? uint64_t(samples) * 256 : 0) +
                              (volume_grid ? uint64_t(2) * reconstruction_limit() * 96 : 0);
    const uint64_t available = max_bytes - population_bytes;
    uint64_t fixed = shared + worker;
    if (fixed > available)
      throw std::invalid_argument("Deep scanline working set exceeds --deep-memory-mb budget");
    if (volume) {
      /* Preserve the serial row allowance. Parallel workers share the remaining
       * reconstruction reservation, rather than reducing accepted row capacity. */
      const uint64_t remaining = available - fixed;
      const uint64_t row_bytes = remaining - remaining / 2;
      uint64_t retained_curves = samples;
      if (volume_grid && samples > 8) {
        retained_curves = 0;
        for (uint64_t n = samples; n > 1; n = (n + 1) / 2)
          ++retained_curves;
      }
      /* The streaming tree can retain one maximum-sized curve per level.
       * Do not select workers using only a two-curve reconstruction allowance. */
      const uint64_t minimum_pixel = output_events * 128 *
                                     std::max(uint64_t(2), retained_curves);
      const uint64_t workers = (available - shared - row_bytes) / (worker + minimum_pixel);
      volume_export_workers_ = int(std::max(uint64_t(1),
          std::min({uint64_t(export_workers), uint64_t(width), workers})));
      fixed = shared + worker * volume_export_workers_;
      /* Reserve reconstruction separately from the EXR row. Charge actual
       * retained vector capacities before building the mixture's boundaries
       * and optical-depth indices; adaptive maximums need not all be dense. */
      volume_pixel_bytes_ = size_t((available - fixed - row_bytes) / volume_export_workers_);
      /* Reserve a fixed row capacity from the remaining budget, then enforce
       * retained FLOAT capacities. Sparse wide rows need not reserve
       * the per-pixel maximum at every pixel. Dense rows still fail closed. */
      volume_row_sample_limit_ = size_t(std::min(uint64_t(width) * output_events,
                                               row_bytes / row_sample_bytes));
      if (volume_row_sample_limit_ < output_events)
        throw std::invalid_argument("Deep volume row cannot fit one maximum-capacity pixel");
    }
    else if (uint64_t(width) * output_events * row_sample_bytes > available - fixed)
      throw std::invalid_argument("Deep scanline working set exceeds --deep-memory-mb budget");
    if (adaptive)
      populations_.assign(count / samples, 0);
    stride_ = sizeof(SpillRecord);
    std::array<unsigned char, 65536> zeros{};
    for (int y = 0; y < height; y += rows_per_band_) {
      bands_.push_back(std::make_unique<Band>());
      Band &band = *bands_.back();
      band.first_y = y;
      band.rows = std::min(rows_per_band_, height - y);
      band.pixels = size_t(width) * band.rows;
      band.records = band.pixels * samples;
      band.index = std::tmpfile();
      band.events = std::tmpfile();
      if (!band.index || !band.events)
        throw std::runtime_error("Cannot create temporary deep band files");
      for (SpillPage &page : band.pages)
        page.data.resize(spill_page_bytes);
      band.event_page.data.resize(event_page_bytes);
      size_t remaining = band.records * stride_;
      while (remaining) {
        const size_t n = std::min(remaining, zeros.size());
        if (std::fwrite(zeros.data(), 1, n, band.index) != n)
          throw std::runtime_error("Cannot allocate deep band index");
        band.write_bytes += n;
        remaining -= n;
      }
      if (std::fflush(band.index) != 0)
        throw std::runtime_error("Cannot flush deep band index");
    }
    return;
  }
  if (count > (max_bytes - population_bytes) / stride_)
    throw std::invalid_argument("Deep raw capture exceeds --deep-memory-mb budget");
  if (adaptive)
    populations_.assign(count / samples, 0);
  results_.resize(count);
  events_.resize(count * capacity_);
  if (volume_grid_)
    density_.resize(count * capacity_);
}
Capture::~Capture() = default;
Capture::SpillStatistics Capture::spill_statistics() const
{
  SpillStatistics result;
  for (const auto &item : bands_) {
    const Band &band = *item;
    const std::lock_guard<std::mutex> lock(band.mutex);
    result.read_bytes += band.read_bytes;
    result.write_bytes += band.write_bytes;
    result.file_bytes += band.records * stride_ + band.event_bytes;
  }
  return result;
}
void Capture::decode_index(const size_t index, size_t &pixel, uint32_t &sample) const
{
  const size_t pixels = size_t(width_) * height_;
  const size_t first = index / (pixels * 16) * 16;
  const size_t span = std::min(size_t(16), size_t(samples_) - first);
  const size_t remainder = index - first * pixels;
  pixel = remainder / span;
  sample = uint32_t(first + remainder % span);
}
Capture::Band &Capture::band_for_pixel(const size_t pixel) const
{
  return *bands_.at(pixel / width_ / rows_per_band_);
}
size_t Capture::band_record_index(const Band &band, const size_t pixel,
                                  const uint32_t sample) const
{
  const size_t first = size_t(sample / 16) * 16;
  const size_t span = std::min(size_t(16), size_t(samples_) - first);
  return first * band.pixels + (pixel - size_t(band.first_y) * width_) * span + sample - first;
}
size_t Capture::record_index(const size_t pixel, const uint32_t sample) const
{
  /* Group nearby pixels within native-sized sampling batches. Pixel-major
   * maximum-sample reservations amplify 24-byte writes into distant page reads. */
  const size_t first = size_t(sample / 16) * 16;
  const size_t span = std::min(size_t(16), size_t(samples_) - first);
  return first * size_t(width_) * height_ + pixel * span + sample - first;
}
namespace {
void seek_record(FILE *file, const size_t offset)
{
#ifdef _WIN32
  const int result = _fseeki64(file, int64_t(offset), SEEK_SET);
#else
  const int result = fseeko(file, off_t(offset), SEEK_SET);
#endif
  if (result != 0)
    throw std::runtime_error("Deep spill seek failed");
}
}  // namespace

/* Capture holds only the destination band's mutex. */
void Capture::flush_page(Band &band, SpillPage &page) const
{
  if (!page.dirty_end)
    return;
  const size_t bytes = page.dirty_end - page.dirty_begin;
  seek_record(band.index, page.first * stride_ + page.dirty_begin);
  if (std::fwrite(page.data.data() + page.dirty_begin, 1, bytes, band.index) != bytes)
    throw std::runtime_error("Deep spill page write failed");
  band.write_bytes += bytes;
  page.dirty_begin = spill_page_bytes;
  page.dirty_end = 0;
}
Capture::SpillPage &Capture::spill_page(Band &band, const size_t index) const
{
  const size_t records = spill_page_bytes / stride_;
  const size_t first = index / records * records;
  SpillPage *selected = &band.pages[0];
  for (SpillPage &candidate : band.pages) {
    if (candidate.first == first) {
      selected = &candidate;
      break;
    }
    if (candidate.last_used < selected->last_used)
      selected = &candidate;
  }
  SpillPage &page = *selected;
  if (page.first != first) {
    flush_page(band, page);
    page.first = size_t(-1);
    const size_t bytes = std::min(records, band.records - first) * stride_;
    seek_record(band.index, first * stride_);
    if (std::fread(page.data.data(), 1, bytes, band.index) != bytes)
      throw std::runtime_error("Deep spill page read failed");
    band.read_bytes += bytes;
    page.first = first;
    page.bytes = bytes;
  }
  page.last_used = ++band.clock;
  return page;
}
void Capture::read_events(Band &band, size_t offset,
                           unsigned char *destination, size_t bytes) const
{
  if (offset > band.event_bytes || bytes > band.event_bytes - offset)
    throw std::runtime_error("Invalid deep event file offset");
  while (bytes) {
    const size_t first = offset / event_page_bytes * event_page_bytes;
    EventPage &page = band.event_page;
    if (page.first != first) {
      page.first = size_t(-1);
      page.bytes = std::min(event_page_bytes, band.event_bytes - first);
      seek_record(band.events, first);
      band.reading_events = true;
      if (std::fread(page.data.data(), 1, page.bytes, band.events) != page.bytes)
        throw std::runtime_error("Deep event file read failed");
      band.read_bytes += page.bytes;
      page.first = first;
    }
    const size_t n = std::min(bytes, page.bytes - (offset - first));
    std::memcpy(destination, page.data.data() + offset - first, n);
    destination += n;
    offset += n;
    bytes -= n;
  }
}

void Capture::read_record(const size_t index,
                          KernelDeepResult &result,
                          KernelDeepEvent *events,
                          KernelDeepDensity *density) const
{
  SpillRecord stored{};
  Band *band = nullptr;
  if (!bands_.empty()) {
    size_t pixel;
    uint32_t sample;
    decode_index(index, pixel, sample);
    band = &band_for_pixel(pixel);
    if (exporting_) {
      const int y = int(pixel / width_);
      if (!export_row_open_ || y < staged_first_y_ || y >= staged_first_y_ + staged_rows_)
        throw std::runtime_error("Deep read outside prepared export band");
      stored = staged_records_.at((pixel - size_t(staged_first_y_) * width_) * samples_ + sample);
    }
    else {
      const size_t local = band_record_index(*band, pixel, sample);
      const SpillPage &page = spill_page(*band, local);
      std::memcpy(&stored, page.data.data() + (local - page.first) * stride_, sizeof(stored));
    }
    result = stored.result;
  }
  else {
    result = results_[index];
  }
  if (result.count > capacity_ || result.error != DEEP_ERROR_NONE ||
      (result.status != DEEP_EMPTY && result.status != DEEP_COMPLETE))
    throw std::runtime_error("Invalid deep stored record");
  if (!band) {
    if (events)
      std::copy_n(events_.data() + index * capacity_, result.count, events);
    if (density && volume_grid_)
      std::copy_n(density_.data() + index * capacity_, result.count, density);
  }
  if (band && events) {
    const auto read = [&](size_t offset, unsigned char *destination, size_t bytes) {
      if (!exporting_)
        read_events(*band, offset, destination, bytes);
      else {
        if (offset > staged_events_.size() || bytes > staged_events_.size() - offset)
          throw std::runtime_error("Invalid staged deep event offset");
        if (bytes)
          std::memcpy(destination, staged_events_.data() + offset, bytes);
      }
    };
    read(size_t(stored.event_offset), reinterpret_cast<unsigned char *>(events),
         result.count * sizeof(*events));
    if (density && volume_grid_) {
      size_t offset = size_t(stored.event_offset) + result.count * sizeof(*events);
      for (unsigned i = 0; i < result.count; ++i)
        if (has_density(events[i].kind)) {
          read(offset, reinterpret_cast<unsigned char *>(density + i), sizeof(*density));
          offset += sizeof(*density);
        }
    }
  }
}
void Capture::store_record(const size_t index,
                           const KernelDeepResult &result,
                           const KernelDeepEvent *events,
                           const KernelDeepDensity *density)
{
  try {
    KernelDeepResult previous{};
    read_record(index, previous, nullptr);
    if (previous.status != DEEP_EMPTY) {
      set_error(DUPLICATE);
      return;
    }
    if (!bands_.empty()) {
      size_t pixel;
      uint32_t sample;
      decode_index(index, pixel, sample);
      Band &band = band_for_pixel(pixel);
      const size_t event_bytes = result.count * sizeof(KernelDeepEvent);
      size_t bytes = sizeof(SpillRecord) + event_bytes;
      for (unsigned i = 0; i < result.count; ++i)
        if (has_density(events[i].kind))
          bytes += sizeof(KernelDeepDensity);
      if (band.event_bytes > size_t(INT64_MAX) - bytes)
        throw std::runtime_error("Deep event file offset overflow");
      if (band.reading_events) {
        seek_record(band.events, band.event_bytes);
        band.reading_events = false;
        band.event_page.first = size_t(-1);
      }
      /* Self-describing stream records allow sequential re-bucketing without
       * an unbounded offset catalogue. Identity is canonical pixel/sample. */
      const SpillRecord header{uint64_t(pixel * samples_ + sample), result};
      if (std::fwrite(&header, 1, sizeof(header), band.events) != sizeof(header) ||
          (event_bytes && std::fwrite(events, 1, event_bytes, band.events) != event_bytes))
        throw std::runtime_error("Deep event file append failed");
      for (unsigned i = 0; i < result.count; ++i)
        if (has_density(events[i].kind) &&
            std::fwrite(density + i, 1, sizeof(*density), band.events) != sizeof(*density))
          throw std::runtime_error("Deep density file append failed");
      band.write_bytes += bytes;
      const SpillRecord stored{uint64_t(band.event_bytes + sizeof(header)), result};
      band.event_bytes += bytes;
      const size_t local = band_record_index(band, pixel, sample);
      SpillPage &page = spill_page(band, local);
      const size_t offset = (local - page.first) * stride_;
      std::memcpy(page.data.data() + offset, &stored, sizeof(stored));
      page.dirty_begin = std::min(page.dirty_begin, offset);
      page.dirty_end = std::max(page.dirty_end, offset + stride_);
    }
    else {
      results_[index] = result;
      if (result.count)
        std::copy_n(events, result.count, events_.data() + index * capacity_);
      if (volume_grid_)
        for (unsigned i = 0; i < result.count; ++i)
          if (has_density(events[i].kind))
            density_[index * capacity_ + i] = density[i];
    }
    ++completed_;
  }
  catch (const std::exception &) {
    set_error(IO_ERROR);
  }
}
void Capture::set_error(const Error error)
{
  int expected = NONE;
  error_.compare_exchange_strong(expected, error);
}
void Capture::fail(const KernelDeepError reason)
{
  KernelDeepError expected = DEEP_ERROR_NONE;
  failure_.compare_exchange_strong(expected,
                                   reason == DEEP_ERROR_NONE ? DEEP_ERROR_STATE : reason);
  set_error(UNSUPPORTED_STATE);
}
void Capture::record(const int x, const int y, const uint32_t sample, const float depth)
{
  if (max_events_) {
    fail();
    return;
  }
  if (!std::isfinite(depth) || depth < 0) {
    set_error(INVALID_DEPTH);
    return;
  }
  const KernelDeepEvent event{DEEP_SURFACE, depth, depth, 1, 0};
  record_sample(x, y, sample, {DEEP_COMPLETE, depth == 0 ? 0u : 1u, DEEP_ERROR_NONE}, &event);
}
void Capture::record_events(const int x,
                            const int y,
                            const uint32_t sample,
                            const KernelDeepEvent *events,
                            const int count)
{
  if (count < 0) {
    fail();
    return;
  }
  record_sample(x, y, sample, {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE}, events);
}
void Capture::record_sample(const int x,
                            const int y,
                            const uint32_t sample,
                            const KernelDeepResult &result,
                            const KernelDeepEvent *events,
                            const KernelDeepDensity *density)
{
  if (exporting_) {
    fail();
    return;
  }
  if (error_.load() != NONE)
    return;
  if (result.status == DEEP_SKIPPED && result.count == 0 && result.error == DEEP_ERROR_NONE)
    return;
  if (result.status != DEEP_COMPLETE || result.error != DEEP_ERROR_NONE) {
    fail(result.error);
    return;
  }
  if (result.count > capacity_ || (result.count && !events)) {
    fail(DEEP_ERROR_CAPACITY);
    return;
  }
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample >= uint32_t(samples_)) {
    set_error(OUT_OF_RANGE);
    return;
  }
  for (unsigned i = 0; i < result.count; ++i) {
    const KernelDeepEvent &e = events[i];
    const bool surface = e.kind == DEEP_SURFACE;
    const bool cubic = e.kind == DEEP_VOLUME_CUBIC;
    if (has_density(e.kind)) {
      if (!volume_grid_ || !density || (cubic && e.optical_depth != 0)) {
        set_error(INVALID_DEPTH);
        return;
      }
      if (!std::isfinite(density[i].front) || !std::isfinite(density[i].back) ||
          density[i].front <= 0 || density[i].back <= density[i].front ||
          e.front != float(density[i].front) || e.back != float(density[i].back)) {
        set_error(INVALID_DEPTH);
        return;
      }
      for (float coefficient : density[i].optical_depth)
        if (!std::isfinite(coefficient) || coefficient < 0) {
          set_error(INVALID_DEPTH);
          return;
        }
    }
    if (!std::isfinite(e.front) || e.front <= 0 || !std::isfinite(e.back) ||
        !std::isfinite(e.surface_alpha) || !std::isfinite(e.optical_depth) ||
        (surface ? (e.back != e.front || e.surface_alpha < 0 || e.surface_alpha > 1 ||
                    e.optical_depth != 0) :
                   ((!cubic && e.kind != DEEP_VOLUME) || !volume_ ||
                    (has_density(e.kind) ? e.back < e.front : e.back <= e.front) ||
                    e.optical_depth < 0 || e.surface_alpha != 0)) ||
        (!volume_ && i && e.front < events[i - 1].front) ||
        (!max_events_ && (!surface || e.surface_alpha != 1)))
    {
      set_error(INVALID_DEPTH);
      return;
    }
  }
  const size_t index = record_index(size_t(y) * width_ + x, sample);
  std::lock_guard<std::mutex> lock(bands_.empty() ? mutex_ : band_for_pixel(size_t(y) * width_ + x).mutex);
  store_record(index, result, events, density);
}
void Capture::set_population(const int x, const int y, const uint32_t count)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (exporting_) {
    fail();
    return;
  }
  if (!adaptive() || x < 0 || y < 0 || x >= width_ || y >= height_ || count == 0 ||
      count > uint32_t(samples_))
  {
    set_error(OUT_OF_RANGE);
    return;
  }
  uint32_t &previous = populations_[size_t(y) * width_ + x];
  if (count < previous)
    set_error(OUT_OF_RANGE);
  else
    previous = count;
}
int Capture::population(const int x, const int y) const
{
  if (x < 0 || y < 0 || x >= width_ || y >= height_)
    throw std::out_of_range("Deep population outside bounds");
  return adaptive() ? int(populations_[size_t(y) * width_ + x]) : samples_;
}
VolumeCameraSample Capture::volume_sample(const int x, const int y, const int sample,
                                         const double density_tolerance) const
{
  if (sample < 0 || sample >= population(x, y))
    throw std::out_of_range("Deep camera sample outside population");
  KernelDeepResult result{};
  thread_local std::unordered_map<const Capture *, std::weak_ptr<ReadScratch>> cache;
  auto scratch = cache[this].lock();
  if (!scratch) {
    for (auto it = cache.begin(); it != cache.end();)
      it = it->second.expired() ? cache.erase(it) : std::next(it);
    scratch = std::make_shared<ReadScratch>();
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      read_scratch_.push_back(scratch);
    }
    cache[this] = scratch;
  }
  auto &events = scratch->events;
  auto &density = scratch->density;
  {
    ExportTimer timer{&export_statistics, ExportStatistics::Read};
    std::unique_lock<std::mutex> lock;
    if (!exporting_)
      lock = std::unique_lock<std::mutex>(bands_.empty() ? mutex_ : band_for_pixel(size_t(y) * width_ + x).mutex);
    const size_t index = record_index(size_t(y) * width_ + x, sample);
    read_record(index, result, nullptr);
    /* Keep the high-water size: shrinking/regrowing would zero the reused
     * tail. Only new slots initialize; payload reads overwrite every used slot. */
    if (events.size() < result.count)
      events.resize(result.count);
    if (volume_grid_ && density.size() < result.count)
      density.resize(result.count);
    read_record(index, result, events.data(), volume_grid_ ? density.data() : nullptr);
  }
  if (error_.load() != NONE || result.status != DEEP_COMPLETE)
    throw std::runtime_error("Cannot read incomplete deep capture");
  VolumeCameraSample output{{uint64_t(sample), 1, true, {}}, {}};
  const KernelDeepEvent *record = events.data();
  double opaque_depth = std::numeric_limits<double>::infinity();
  for (unsigned i = 0; i < result.count; ++i)
    if (record[i].kind == DEEP_SURFACE && record[i].surface_alpha == 1)
      opaque_depth = std::min(opaque_depth, double(record[i].front));
  std::vector<CubicDensityInterval> cubic;
  for (unsigned i = 0; i < result.count; ++i) {
    const auto &event = record[i];
    /* A fully opaque surface makes all deeper extinction invisible to every
     * depth query. This is exact occlusion, with no opacity threshold. */
    const double front = has_density(event.kind) ? density[i].front : event.front;
    if (front > opaque_depth ||
        (event.kind != DEEP_SURFACE && front == opaque_depth))
      continue;
    if (event.kind == DEEP_SURFACE)
      output.camera.events.push_back({event.front, event.surface_alpha});
    else if (event.kind == DEEP_VOLUME_CUBIC) {
      const auto &b = density[i].optical_depth;
      cubic.push_back({density[i].front, density[i].back, {b[0], b[1], b[2], b[3]}});
    }
    else if (has_density(event.kind))
      output.intervals.push_back({density[i].front, density[i].back,
          double(density[i].optical_depth[0]) + double(density[i].optical_depth[1])});
    else
      output.intervals.push_back({event.front, event.back, event.optical_depth});
  }
  if (!cubic.empty()) {
    ExportTimer timer{&export_statistics, ExportStatistics::DensityFit};
    const auto fitted = integrate_cubic_density(cubic, density_tolerance, reconstruction_limit());
    if (output.intervals.size() > reconstruction_limit() ||
        fitted.size() > reconstruction_limit() - output.intervals.size())
      throw std::runtime_error("Volume integration interval budget exceeded");
    output.intervals.insert(output.intervals.end(), fitted.begin(), fitted.end());
  }
  for (auto &interval : output.intervals)
    if (interval.back > opaque_depth) {
      interval.optical_depth *= (opaque_depth - interval.front) / (interval.back - interval.front);
      interval.back = opaque_depth;
    }
  output.intervals.erase(std::remove_if(output.intervals.begin(), output.intervals.end(),
      [](const VolumeInterval &interval) { return interval.back <= interval.front; }),
      output.intervals.end());
  return output;
}
std::vector<SurfaceEvent> Capture::events(const int x, const int y, const int sample) const
{
  if (volume_)
    throw std::runtime_error("Volume records require volume_sample");
  return volume_sample(x, y, sample).camera.events;
}
bool Capture::finalize() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  if (error_.load() != NONE)
    return false;
  if (exporting_)
    return true;
  size_t expected = count_;
  if (adaptive()) {
    expected = 0;
    for (const uint32_t n : populations_) {
      if (!n)
        return false;
      expected += n;
    }
  }
  if (completed_.load() != expected)
    return false;
  try {
    if (bands_.empty()) {
      if (adaptive())
        for (size_t pixel = 0; pixel < populations_.size(); ++pixel)
          for (uint32_t sample = 0; sample < populations_[pixel]; ++sample)
            if (results_[record_index(pixel, sample)].status != DEEP_COMPLETE)
              return false;
      return true;
    }
    for (const auto &item : bands_) {
      Band &band = *item;
      const std::lock_guard<std::mutex> band_lock(band.mutex);
      for (SpillPage &page : band.pages)
        flush_page(band, page);
      if (std::fflush(band.index) || (!band.reading_events && std::fflush(band.events)))
        return false;
      if (adaptive()) {
        /* Accepted identities plus the total completion count prove there are
         * no extra records. Avoid scanning reserved, unconverged populations. */
        const auto population_begin = populations_.begin() + size_t(band.first_y) * width_;
        const size_t maximum = *std::max_element(population_begin, population_begin + band.pixels);
        for (size_t first = 0; first < maximum; first += 16) {
          const size_t span = std::min(size_t(16), size_t(samples_) - first);
          for (size_t pixel = 0; pixel < band.pixels; ++pixel)
            for (size_t sample = 0; sample < span; ++sample) {
              if (first + sample >= populations_[size_t(band.first_y) * width_ + pixel])
                continue;
              const size_t index = first * band.pixels + pixel * span + sample;
              const SpillPage &page = spill_page(band, index);
              SpillRecord stored;
              std::memcpy(&stored, page.data.data() + (index - page.first) * stride_, sizeof(stored));
              if (stored.result.status != DEEP_COMPLETE || stored.result.count > capacity_ ||
                  stored.result.error != DEEP_ERROR_NONE)
                return false;
            }
        }
      }
    }
  }
  catch (const std::exception &) {
    return false;
  }
  return true;
}

void Capture::load_stream(Band &band, FILE *file, const size_t bytes,
                           const int first_y, const int rows) const
{
  const size_t records = size_t(width_) * rows * samples_;
  if (bytes > spill_memory_bytes_ || records > (spill_memory_bytes_ - bytes) / sizeof(SpillRecord))
    throw std::runtime_error("Deep export row exceeds --deep-memory-mb budget");
  staged_records_ = std::vector<SpillRecord>(records);
  staged_events_ = std::vector<unsigned char>(bytes);
  if (staged_events_.capacity() + staged_records_.capacity() * sizeof(SpillRecord) > spill_memory_bytes_)
    throw std::runtime_error("Deep staged vector capacities exceed memory budget");
  seek_record(file, 0);
  if (bytes && std::fread(staged_events_.data(), 1, bytes, file) != bytes)
    throw std::runtime_error("Deep sequential band read failed");
  band.read_bytes += bytes;
  size_t offset = 0;
  while (offset < bytes) {
    if (bytes - offset < sizeof(SpillRecord))
      throw std::runtime_error("Truncated deep stream header");
    SpillRecord header;
    std::memcpy(&header, staged_events_.data() + offset, sizeof(header));
    offset += sizeof(header);
    const uint64_t base = uint64_t(first_y) * width_ * samples_;
    if (header.event_offset < base || header.event_offset - base >= records ||
        header.result.status != DEEP_COMPLETE || header.result.error != DEEP_ERROR_NONE ||
        header.result.count > capacity_)
      throw std::runtime_error("Invalid deep stream identity or completion");
    SpillRecord &stored = staged_records_[size_t(header.event_offset - base)];
    if (stored.result.status != DEEP_EMPTY)
      throw std::runtime_error("Duplicate deep stream identity");
    stored = {uint64_t(offset), header.result};
    const size_t event_bytes = header.result.count * sizeof(KernelDeepEvent);
    if (event_bytes > bytes - offset)
      throw std::runtime_error("Truncated deep stream events");
    size_t density_bytes = 0;
    for (unsigned i = 0; i < header.result.count; ++i) {
      KernelDeepEvent event;
      std::memcpy(&event, staged_events_.data() + offset + i * sizeof(event), sizeof(event));
      if (has_density(event.kind))
        density_bytes += sizeof(KernelDeepDensity);
    }
    offset += event_bytes;
    if (density_bytes > bytes - offset)
      throw std::runtime_error("Truncated deep stream density");
    offset += density_bytes;
  }
  staged_first_y_ = first_y;
  staged_rows_ = rows;
}

void Capture::rebucket(Band &band) const
{
  std::vector<KernelDeepEvent> events;
  std::vector<KernelDeepDensity> density;
  if (capacity_ * (sizeof(KernelDeepEvent) + sizeof(KernelDeepDensity)) > spill_memory_bytes_)
    throw std::runtime_error("Deep re-bucket scratch exceeds memory budget");
  for (int y = 0; y < band.rows; ++y) {
    band.row_files.push_back(std::make_unique<RowFile>());
    band.row_files.back()->file = std::tmpfile();
    if (!band.row_files.back()->file)
      throw std::runtime_error("Cannot create deep row file");
  }
  seek_record(band.events, 0);
  size_t remaining = band.event_bytes;
  while (remaining) {
    SpillRecord header;
    if (remaining < sizeof(header) || std::fread(&header, 1, sizeof(header), band.events) != sizeof(header))
      throw std::runtime_error("Deep re-bucket header read failed");
    remaining -= sizeof(header);
    const size_t row = size_t(header.event_offset / samples_ / width_);
    if (row < size_t(band.first_y) || row >= size_t(band.first_y + band.rows) ||
        header.result.count > capacity_ || header.result.status != DEEP_COMPLETE ||
        header.result.error != DEEP_ERROR_NONE)
      throw std::runtime_error("Invalid deep re-bucket record");
    const size_t event_bytes = header.result.count * sizeof(KernelDeepEvent);
    if (events.size() < header.result.count)
      events.resize(header.result.count);
    if (event_bytes > remaining ||
        (event_bytes && std::fread(events.data(), 1, event_bytes, band.events) != event_bytes))
      throw std::runtime_error("Deep re-bucket event read failed");
    remaining -= event_bytes;
    size_t densities = 0;
    for (unsigned i = 0; i < header.result.count; ++i)
      densities += has_density(events[i].kind);
    const size_t density_bytes = densities * sizeof(KernelDeepDensity);
    if (density.size() < densities)
      density.resize(densities);
    if (density_bytes > remaining ||
        (density_bytes && std::fread(density.data(), 1, density_bytes, band.events) != density_bytes))
      throw std::runtime_error("Deep re-bucket density read failed");
    remaining -= density_bytes;
    RowFile &row_file = *band.row_files[row - band.first_y];
    if (std::fwrite(&header, 1, sizeof(header), row_file.file) != sizeof(header) ||
        (event_bytes && std::fwrite(events.data(), 1, event_bytes, row_file.file) != event_bytes) ||
        (density_bytes && std::fwrite(density.data(), 1, density_bytes, row_file.file) != density_bytes))
      throw std::runtime_error("Deep re-bucket write failed");
    const size_t transferred = sizeof(header) + event_bytes + density_bytes;
    band.read_bytes += transferred;
    band.write_bytes += transferred;
    row_file.bytes += transferred;
  }
  for (const auto &row : band.row_files)
    if (std::fflush(row->file))
      throw std::runtime_error("Deep row file flush failed");
  std::fclose(band.index);
  std::fclose(band.events);
  band.index = band.events = nullptr;
  band.rebucketed = true;
}

void Capture::begin_export_row(const int y) const
{
  ExportTimer timer{&export_statistics, ExportStatistics::Staging};
  if (bands_.empty())
    return;
  if (!exporting_) {
    if (!finalize())
      throw std::runtime_error("Cannot export incomplete deep capture");
    exporting_ = true;
    next_export_y_ = height_ - 1;
  }
  if (export_row_open_ || y != next_export_y_)
    throw std::runtime_error("Deep export rows must be sequential Y-down");
  Band &band = band_for_pixel(size_t(y) * width_);
  if (staged_first_y_ < 0) {
    const size_t index_bytes = band.records * sizeof(SpillRecord);
    if (!band.rebucketed && (band.event_bytes > spill_memory_bytes_ ||
        index_bytes > spill_memory_bytes_ - band.event_bytes))
      rebucket(band);
    if (band.rebucketed) {
      const RowFile &row = *band.row_files[y - band.first_y];
      load_stream(band, row.file, row.bytes, y, 1);
    }
    else
      load_stream(band, band.events, band.event_bytes, band.first_y, band.rows);
  }
  export_row_open_ = true;
}
void Capture::end_export_row(const int y) const
{
  if (bands_.empty())
    return;
  if (!export_row_open_ || y != next_export_y_)
    throw std::runtime_error("Deep export row completion mismatch");
  Band &band = band_for_pixel(size_t(y) * width_);
  if (band.rebucketed || y == band.first_y) {
    std::vector<SpillRecord>().swap(staged_records_);
    std::vector<unsigned char>().swap(staged_events_);
    staged_first_y_ = -1;
    staged_rows_ = 0;
    if (band.rebucketed)
      band.row_files[y - band.first_y].reset();
    else {
      std::fclose(band.index);
      std::fclose(band.events);
      band.index = band.events = nullptr;
    }
  }
  export_row_open_ = false;
  --next_export_y_;
}
const char *Capture::error_message() const
{
  switch (error_.load()) {
    case IO_ERROR:
      return "deep spill I/O failed (disk full, seek, read or write error)";
    case OUT_OF_RANGE:
      return "camera sample outside capture bounds";
    case DUPLICATE:
      return "camera sample recorded more than once";
    case INVALID_DEPTH:
      return "camera intersection depth or opacity is invalid";
    case UNSUPPORTED_STATE:
      switch (failure_.load()) {
        case DEEP_ERROR_CAPACITY:
          return "deep traversal limit exceeded";
        case DEEP_ERROR_EVENT_CAPACITY:
          return "deep camera sample event capacity exceeded";
        case DEEP_ERROR_GRID_STEPS:
          return "deep grid traversal step limit exceeded";
        case DEEP_ERROR_BOUNDARY_CAPACITY:
          return "deep coincident or initial boundary capacity exceeded";
        case DEEP_ERROR_MEDIA_CAPACITY:
          return "deep camera sample medium capacity exceeded";
        case DEEP_ERROR_CACHE_MISS:
          return "deep capture texture cache miss";
        case DEEP_ERROR_PRIMITIVE:
          return "unsupported deep capture primitive";
        case DEEP_ERROR_DEPTH:
          return "invalid deep capture depth";
        case DEEP_ERROR_EXTINCTION:
          return "invalid or non-scalar deep extinction";
        case DEEP_ERROR_MEDIUM:
          return "unresolved deep volume boundary state";
        case DEEP_ERROR_PROGRESS:
          return "deep ray traversal did not advance";
        default:
          return "unsupported or incomplete camera capture state";
      }
    default:
      return finalize() ? "" : "camera capture is incomplete";
  }
}
float Capture::value(const int x, const int y, const int sample) const
{
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample < 0 || sample >= samples_)
    throw std::out_of_range("Deep capture read outside bounds");
  KernelDeepResult result{};
  std::array<KernelDeepEvent, DEEP_MAX_EVENTS> record{};
  std::unique_lock<std::mutex> lock;
  if (!exporting_)
    lock = std::unique_lock<std::mutex>(bands_.empty() ? mutex_ : band_for_pixel(size_t(y) * width_ + x).mutex);
  read_record(record_index(size_t(y) * width_ + x, sample), result,
              max_events_ ? nullptr : record.data());
  if (result.status != DEEP_COMPLETE)
    return -1;
  return max_events_ ? float(result.count) : (result.count ? record[0].front : 0);
}
std::vector<SurfaceSample> Capture::reconstruct_pixel(const int x, const int y) const
{
  if (error_.load() != NONE)
    throw std::runtime_error(error_message());
  PixelLedger ledger{x, y, {}};
  for (int sample = 0; sample < population(x, y); ++sample)
    ledger.samples.push_back({uint64_t(sample), 1, true, events(x, y, sample)});
  return reconstruct(ledger);
}
std::vector<IntervalSample> Capture::reconstruct_volume_pixel(const int x, const int y) const
{
  ExportTimer pixel_timer{&export_statistics, ExportStatistics::Pixel};
  auto fit = [&](std::vector<VolumeCameraSample> samples, double tolerance, size_t limit,
                 double reduction, size_t bytes = std::numeric_limits<size_t>::max()) {
    ExportTimer timer{&export_statistics, ExportStatistics::MixtureFit};
    return reconstruct_volume(std::move(samples), tolerance, limit, reduction, bytes);
  };
  if (!volume_ || error_.load() != NONE)
    throw std::runtime_error("Invalid volume capture reconstruction");
  const auto budget = error_budget(error_setting_);
  std::vector<VolumeCameraSample> ledger;
  const int cameras = population(x, y);
  const bool compact_ray = volume_grid_ && samples_ > 8;
  size_t levels = 1;
  for (size_t n = cameras; n > 2; n = (n + 1) / 2)
    ++levels;
  ledger.reserve(compact_ray ? levels : cameras);
  std::vector<size_t> populations;
  populations.reserve(levels);
  auto from_curve = [](const std::vector<IntervalSample> &curve, uint64_t id, double weight) {
    VolumeCameraSample sample{{id, weight, true, {}}, {}};
    const size_t surfaces = std::count_if(curve.begin(), curve.end(),
        [](const IntervalSample &span) { return span.front == span.back; });
    sample.camera.events.reserve(surfaces);
    sample.intervals.reserve(curve.size() - surfaces);
    for (const auto &span : curve)
      if (span.front == span.back)
        sample.camera.events.push_back({span.front, span.alpha});
      else
        sample.intervals.push_back({span.front, span.back, -std::log1p(-span.alpha)});
    return sample;
  };
  auto bytes = [](const VolumeCameraSample &sample) {
    return (sample.intervals.capacity() + sample.camera.events.capacity()) * 128;
  };
  auto combine = [&](VolumeCameraSample left, VolumeCameraSample right) {
    const uint64_t id = left.camera.id;
    const double weight = left.camera.weight + right.camera.weight;
    std::vector<VolumeCameraSample> pair;
    pair.push_back(std::move(left));
    pair.push_back(std::move(right));
    const auto curve = fit(std::move(pair),
                                          budget.reconstruction / (2 * levels),
                                          reconstruction_limit(),
                                          budget.reconstruction / (2 * levels));
    return from_curve(curve, id, weight);
  };
  size_t retained_bytes = 0;
  for (int i = 0; i < cameras; ++i) {
    auto sample = volume_sample(
        x, y, i, compact_ray ? budget.density / 2 : budget.density);
    if (compact_ray && error_setting_ == 0) {
      /* Split the existing density allowance between integration and one-ray
       * reduction. Convex averaging preserves this per-ray absolute T bound. */
      const auto compact = fit(
          {sample}, budget.reconstruction, reconstruction_limit(), budget.density / 2);
      sample = from_curve(compact, sample.camera.id, sample.camera.weight);
    }
    size_t population = 1;
    /* Equal-sized adjacent groups form a balanced tree while the camera ledger
     * is read. Keep only logarithmically many curves, not every camera ray. */
    while (compact_ray && !populations.empty() && populations.back() == population) {
      retained_bytes -= bytes(ledger.back());
      sample = combine(std::move(ledger.back()), std::move(sample));
      ledger.pop_back();
      populations.pop_back();
      population *= 2;
    }
    /* 128 bytes per retained slot covers the source, growing boundary vector,
     * optical-depth prefixes and even one run per interval. Single-ray fitting
     * scratch and ledger objects are reserved independently in the preflight. */
    const size_t sample_bytes = bytes(sample);
    if (volume_pixel_bytes_ && sample_bytes > volume_pixel_bytes_ - retained_bytes)
      throw std::runtime_error(
          "Deep volume pixel reconstruction exceeds --deep-memory-mb budget: retained " +
          std::to_string(retained_bytes + sample_bytes) + ", budget " +
          std::to_string(volume_pixel_bytes_) + ", pixel " + std::to_string(x) + "," +
          std::to_string(y));
    retained_bytes += sample_bytes;
    ledger.push_back(std::move(sample));
    if (compact_ray)
      populations.push_back(population);
  }
  if (compact_ray && !ledger.empty()) {
    auto sample = std::move(ledger.back());
    ledger.pop_back();
    /* Fold the remaining smaller groups from the right. No camera traverses
     * more than ceil(log2(population)) fitting levels. */
    while (!ledger.empty()) {
      sample = combine(std::move(ledger.back()), std::move(sample));
      ledger.pop_back();
    }
    ledger.push_back(std::move(sample));
  }
  /* Share the existing reconstruction allowance between mixture fitting and
   * streaming reduction; the total published error budget is unchanged. */
  return fit(std::move(ledger),
                            budget.reconstruction / 2,
                            reconstruction_limit(),
                            compact_ray ? 0 : budget.reconstruction / 2,
                            volume_pixel_bytes_ ? volume_pixel_bytes_ :
                                                  std::numeric_limits<size_t>::max());
}
}  // namespace ccl::deep
