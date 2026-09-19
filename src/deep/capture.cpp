/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ccl::deep {
Capture::Capture(const int width,
                 const int height,
                 const int samples,
                 const size_t max_bytes,
                 const int max_events,
                 const bool spill,
                 const bool adaptive,
                 const bool volume)
    : width_(width), height_(height), samples_(samples), max_events_(max_events), volume_(volume)
{
  if (max_events < 0 || max_events > int(DEEP_MAX_EVENTS) || (volume && !max_events))
    throw std::invalid_argument("Deep traversal limit must be 1..64 events");
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
  stride_ = sizeof(KernelDeepResult) + capacity_ * sizeof(KernelDeepEvent);
  if (count > size_t(INT64_MAX) / stride_)
    throw std::invalid_argument("Deep spill file offsets overflow");
  if (adaptive && count / samples > max_bytes / sizeof(uint32_t))
    throw std::invalid_argument("Deep adaptive populations exceed memory budget");
  const size_t population_bytes = adaptive ? count / samples * sizeof(uint32_t) : 0;
  if (spill) {
    /* Two MiB covers 512-lane host/device events and 64 media (1,863,680 bytes),
     * fixed scratch and I/O. Beauty/shader memory remains outside this budget. */
    const uint64_t events = uint64_t(samples) * capacity_;
    const uint64_t output_events = volume ? volume_interval_limit : events;
    const uint64_t required = 2 * 1024 * 1024 +
                              (spill_page_bytes + event_page_bytes) * spill_page_count +
                              uint64_t(width) * (128 + output_events * 128) +
                              events * 512;
    if (required > max_bytes - population_bytes)
      throw std::invalid_argument("Deep scanline working set exceeds --deep-memory-mb budget");
    if (adaptive)
      populations_.assign(count / samples, 0);
    for (SpillPage &page : spill_pages_)
      page.data.resize(spill_page_bytes);
    for (EventPage &page : event_pages_)
      page.data.resize(event_page_bytes);
    stride_ = sizeof(SpillRecord);
    spill_ = std::tmpfile();
    if (!spill_)
      throw std::runtime_error("Cannot create temporary deep capture spill file");
    /* Zero explicitly means EMPTY, never an accepted miss. */
    std::array<unsigned char, 65536> zeros{};
    size_t remaining = count * stride_;
    while (remaining) {
      const size_t n = std::min(remaining, zeros.size());
      if (std::fwrite(zeros.data(), 1, n, spill_) != n) {
        std::fclose(spill_);
        spill_ = nullptr;
        throw std::runtime_error("Cannot allocate deep spill file (disk full or write failure)");
      }
      remaining -= n;
    }
    if (std::fflush(spill_) != 0) {
      std::fclose(spill_);
      spill_ = nullptr;
      throw std::runtime_error("Cannot flush deep spill file");
    }
    spill_events_ = std::tmpfile();
    if (!spill_events_) {
      std::fclose(spill_);
      spill_ = nullptr;
      throw std::runtime_error("Cannot create temporary deep event file");
    }
    return;
  }
  if (count > (max_bytes - population_bytes) / stride_)
    throw std::invalid_argument("Deep raw capture exceeds --deep-memory-mb budget");
  if (adaptive)
    populations_.assign(count / samples, 0);
  results_.resize(count);
  events_.resize(count * capacity_);
}
Capture::~Capture()
{
  if (spill_)
    std::fclose(spill_);
  if (spill_events_)
    std::fclose(spill_events_);
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

/* Workers already hold mutex_. Fixed, least-recently-used pages avoid per-sample
 * stdio read/write transitions. Eviction preserves untouched records, including
 * EMPTY markers; both CPU and downloaded GPU records use this same storage. */
void Capture::flush_page(SpillPage &page) const
{
  if (page.dirty_end == 0)
    return;
  const size_t bytes = page.dirty_end - page.dirty_begin;
  seek_record(spill_, page.first * stride_ + page.dirty_begin);
  if (std::fwrite(page.data.data() + page.dirty_begin, 1, bytes, spill_) != bytes)
    throw std::runtime_error("Deep spill page write failed");
  page.dirty_begin = spill_page_bytes;
  page.dirty_end = 0;
}
Capture::SpillPage &Capture::spill_page(const size_t index) const
{
  const size_t records = spill_page_bytes / stride_;
  const size_t number = index / records;
  const size_t first = number * records;
  /* CPU workers can visit distant pixels with identical page residues. A
   * fully associative cache keeps those active pages from evicting each other.
   * Sixteen fixed entries need neither a hash table nor further allocation. */
  SpillPage *selected = &spill_pages_[0];
  for (SpillPage &candidate : spill_pages_) {
    if (candidate.first == first) {
      selected = &candidate;
      break;
    }
    if (candidate.last_used < selected->last_used)
      selected = &candidate;
  }
  SpillPage &page = *selected;
  if (page.first != first) {
    flush_page(page);
    page.first = size_t(-1);
    const size_t bytes = std::min(records, count_ - first) * stride_;
    seek_record(spill_, first * stride_);
    if (std::fread(page.data.data(), 1, bytes, spill_) != bytes)
      throw std::runtime_error("Deep spill page read failed");
    page.first = first;
    page.bytes = bytes;
  }
  page.last_used = ++spill_clock_;
  return page;
}

void Capture::read_events(size_t offset, unsigned char *destination, size_t bytes) const
{
  if (offset > spill_event_bytes_ || bytes > spill_event_bytes_ - offset)
    throw std::runtime_error("Invalid deep event file offset");
  while (bytes) {
    const size_t first = offset / event_page_bytes * event_page_bytes;
    EventPage *selected = &event_pages_[0];
    for (EventPage &candidate : event_pages_) {
      if (candidate.first == first) {
        selected = &candidate;
        break;
      }
      if (candidate.last_used < selected->last_used)
        selected = &candidate;
    }
    EventPage &page = *selected;
    if (page.first != first) {
      page.first = size_t(-1);
      page.bytes = std::min(event_page_bytes, spill_event_bytes_ - first);
      /* Seeking also flushes any preceding append before switching to reads. */
      seek_record(spill_events_, first);
      spill_events_reading_ = true;
      if (std::fread(page.data.data(), 1, page.bytes, spill_events_) != page.bytes)
        throw std::runtime_error("Deep event file read failed");
      page.first = first;
    }
    page.last_used = ++spill_clock_;
    const size_t n = std::min(bytes, page.bytes - (offset - first));
    std::memcpy(destination, page.data.data() + offset - first, n);
    destination += n;
    offset += n;
    bytes -= n;
  }
}

void Capture::read_record(const size_t index,
                          KernelDeepResult &result,
                          KernelDeepEvent *events) const
{
  SpillRecord stored{};
  if (spill_) {
    const SpillPage &page = spill_page(index);
    const unsigned char *record = page.data.data() + (index - page.first) * stride_;
    std::memcpy(&stored, record, sizeof(stored));
    result = stored.result;
  }
  else {
    result = results_[index];
    if (events)
      std::copy_n(events_.data() + index * capacity_, capacity_, events);
  }
  if (result.count > capacity_ || result.error != DEEP_ERROR_NONE ||
      (result.status != DEEP_EMPTY && result.status != DEEP_COMPLETE))
    throw std::runtime_error("Invalid deep stored record");
  if (spill_ && events) {
    std::fill_n(events, capacity_, KernelDeepEvent{});
    read_events(size_t(stored.event_offset),
                reinterpret_cast<unsigned char *>(events),
                result.count * sizeof(*events));
  }
}
void Capture::store_record(const size_t index,
                           const KernelDeepResult &result,
                           const KernelDeepEvent *events)
{
  try {
    KernelDeepResult previous{};
    read_record(index, previous, nullptr);
    if (previous.status != DEEP_EMPTY) {
      set_error(DUPLICATE);
      return;
    }
    std::array<KernelDeepEvent, DEEP_MAX_EVENTS> record{};
    if (result.count)
      std::copy_n(events, result.count, record.data());
    if (spill_) {
      const size_t bytes = result.count * sizeof(KernelDeepEvent);
      if (spill_event_bytes_ > size_t(INT64_MAX) - bytes)
        throw std::runtime_error("Deep event file offset overflow");
      if (bytes && spill_events_reading_) {
        seek_record(spill_events_, spill_event_bytes_);
        spill_events_reading_ = false;
        /* A previously read final partial page may grow on a later batch. */
        for (EventPage &page : event_pages_) {
          page.first = size_t(-1);
          page.last_used = 0;
        }
      }
      if (bytes && std::fwrite(record.data(), 1, bytes, spill_events_) != bytes)
        throw std::runtime_error("Deep event file append failed");
      const SpillRecord stored{uint64_t(spill_event_bytes_), result};
      spill_event_bytes_ += bytes;
      SpillPage &page = spill_page(index);
      const size_t offset = (index - page.first) * stride_;
      unsigned char *destination = page.data.data() + offset;
      std::memcpy(destination, &stored, sizeof(stored));
      page.dirty_begin = std::min(page.dirty_begin, offset);
      page.dirty_end = std::max(page.dirty_end, offset + stride_);
    }
    else {
      results_[index] = result;
      std::copy_n(record.data(), capacity_, events_.data() + index * capacity_);
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
                            const KernelDeepEvent *events)
{
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
    if (!std::isfinite(e.front) || e.front <= 0 || !std::isfinite(e.back) ||
        !std::isfinite(e.surface_alpha) || !std::isfinite(e.optical_depth) ||
        (surface ? (e.back != e.front || e.surface_alpha < 0 || e.surface_alpha > 1 ||
                    e.optical_depth != 0) :
                   (e.kind != DEEP_VOLUME || !volume_ || e.back <= e.front ||
                    e.optical_depth < 0 || e.surface_alpha != 0)) ||
        (!volume_ && i && e.front < events[i - 1].front) ||
        (!max_events_ && (!surface || e.surface_alpha != 1)))
    {
      set_error(INVALID_DEPTH);
      return;
    }
  }
  const size_t index = (size_t(y) * width_ + x) * samples_ + sample;
  std::lock_guard<std::mutex> lock(mutex_);
  store_record(index, result, events);
}
void Capture::set_population(const int x, const int y, const uint32_t count)
{
  std::lock_guard<std::mutex> lock(mutex_);
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
VolumeCameraSample Capture::volume_sample(const int x, const int y, const int sample) const
{
  if (sample < 0 || sample >= population(x, y))
    throw std::out_of_range("Deep camera sample outside population");
  KernelDeepResult result{};
  std::array<KernelDeepEvent, DEEP_MAX_EVENTS> record{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    read_record((size_t(y) * width_ + x) * samples_ + sample, result, record.data());
  }
  if (error_.load() != NONE || result.status != DEEP_COMPLETE)
    throw std::runtime_error("Cannot read incomplete deep capture");
  VolumeCameraSample output{{uint64_t(sample), 1, true, {}}, {}};
  for (unsigned i = 0; i < result.count; ++i) {
    const auto &event = record[i];
    if (event.kind == DEEP_SURFACE)
      output.camera.events.push_back({event.front, event.surface_alpha});
    else
      output.intervals.push_back({event.front, event.back, event.optical_depth});
  }
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
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_.load() != NONE)
    return false;
  if (adaptive()) {
    size_t expected = 0;
    try {
      for (size_t pixel = 0; pixel < populations_.size(); ++pixel) {
        const uint32_t n = populations_[pixel];
        if (!n)
          return false;
        expected += n;
        for (uint32_t sample = 0; sample < n; ++sample) {
          KernelDeepResult result{};
          read_record(pixel * samples_ + sample, result, nullptr);
          if (result.status != DEEP_COMPLETE)
            return false;
        }
      }
    }
    catch (const std::exception &) {
      return false;
    }
    if (expected != completed_)
      return false;
  }
  else if (completed_ != count_)
    return false;
  if (spill_) {
    try {
      for (SpillPage &page : spill_pages_)
        flush_page(page);
    }
    catch (const std::exception &) {
      return false;
    }
    return std::fflush(spill_) == 0 &&
           (spill_events_reading_ || std::fflush(spill_events_) == 0);
  }
  return true;
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
  std::lock_guard<std::mutex> lock(mutex_);
  read_record((size_t(y) * width_ + x) * samples_ + sample, result, record.data());
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
  if (!volume_ || error_.load() != NONE)
    throw std::runtime_error("Invalid volume capture reconstruction");
  std::vector<VolumeCameraSample> ledger;
  for (int i = 0; i < population(x, y); ++i)
    ledger.push_back(volume_sample(x, y, i));
  return reconstruct_volume(ledger, 2e-7, volume_interval_limit);
}
}  // namespace ccl::deep
