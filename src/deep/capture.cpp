/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ccl::deep {
OpaqueCapture::OpaqueCapture(const int width,
                             const int height,
                             const int samples,
                             const size_t max_bytes,
                             const int max_events,
                             const bool spill)
    : width_(width), height_(height), samples_(samples), max_events_(max_events)
{
  if (max_events < 0 || max_events > 64)
    throw std::invalid_argument("Deep traversal limit must be 1..64 events");
  if (width <= 0 || height <= 0 || samples <= 0 || samples > 4096) {
    throw std::invalid_argument("Deep capture requires positive dimensions and 1..4096 samples");
  }
  size_t count = size_t(width);
  for (const size_t factor : {size_t(height), size_t(samples)}) {
    if (count > std::numeric_limits<size_t>::max() / factor)
      throw std::invalid_argument("Deep capture dimensions overflow");
    count *= factor;
  }
  const size_t stride = 1 + 2 * size_t(max_events);
  count_ = count;
  stride_ = stride;
  if (count > size_t(INT64_MAX) / (sizeof(float) * stride))
    throw std::invalid_argument("Deep spill file offsets overflow");
  if (spill) {
    /* Conservative working-set reservation: one row of reconstructed/FLOAT
     * samples, one pixel ledger and sorting workspace, and I/O buffers.
     * The renderer's beauty/shader memory is outside this deep-only budget. */
    const uint64_t events = uint64_t(samples) * std::max(1, max_events);
    const uint64_t required = 1024 * 1024 + uint64_t(width) * (128 + events * 128) + events * 512;
    if (required > max_bytes)
      throw std::invalid_argument("Deep scanline working set exceeds --deep-memory-mb budget");
    spill_ = std::tmpfile();
    if (!spill_)
      throw std::runtime_error("Cannot create temporary deep capture spill file");
    /* Initialize explicitly: no filesystem sparse-hole assumptions. A zero
     * completion marker means absent; transparent markers are count + 1. */
    std::array<float, 16384> zeros{};
    size_t remaining = count * stride;
    while (remaining) {
      const size_t n = std::min(remaining, zeros.size());
      if (std::fwrite(zeros.data(), sizeof(float), n, spill_) != n) {
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
    return;
  }
  if (count > max_bytes / (sizeof(float) * stride))
    throw std::invalid_argument("Deep raw capture exceeds --deep-memory-mb budget");
  values_.assign(count, -1.0f);
  events_.resize(count * 2 * size_t(max_events));
}
OpaqueCapture::~OpaqueCapture()
{
  if (spill_)
    std::fclose(spill_);
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
void OpaqueCapture::read_record(const size_t index, float *record) const
{
  seek_record(spill_, index * stride_ * sizeof(float));
  if (std::fread(record, sizeof(float), stride_, spill_) != stride_)
    throw std::runtime_error("Deep spill read failed");
}
void OpaqueCapture::store_record(const size_t index, const float *record)
{
  try {
    std::array<float, 129> previous{};
    read_record(index, previous.data());
    if (previous[0] != 0) {
      set_error(DUPLICATE);
      return;
    }
    seek_record(spill_, index * stride_ * sizeof(float));
    if (std::fwrite(record, sizeof(float), stride_, spill_) != stride_)
      throw std::runtime_error("Deep spill write failed");
    ++completed_;
  }
  catch (const std::exception &) {
    set_error(IO_ERROR);
  }
}
void OpaqueCapture::set_error(const Error error)
{
  int expected = NONE;
  error_.compare_exchange_strong(expected, error);
}
void OpaqueCapture::fail()
{
  set_error(UNSUPPORTED_STATE);
}
void OpaqueCapture::record(const int x, const int y, const uint32_t sample, const float depth)
{
  if (max_events_) {
    fail();
    return;
  }
  if (error_.load() != NONE)
    return;
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample >= uint32_t(samples_)) {
    set_error(OUT_OF_RANGE);
    return;
  }
  if (!std::isfinite(depth) || depth < 0.0f) {
    set_error(INVALID_DEPTH);
    return;
  }
  const size_t index = (size_t(y) * size_t(width_) + size_t(x)) * size_t(samples_) + sample;
  std::lock_guard<std::mutex> lock(mutex_);
  if (spill_) {
    /* Opaque hits use negative depth; a completed miss uses +1. This keeps
     * completion distinct without losing precision through depth+1 encoding. */
    const float marker = depth == 0 ? 1.0f : -depth;
    store_record(index, &marker);
    return;
  }
  if (values_[index] != -1.0f)
    set_error(DUPLICATE);
  else
    values_[index] = depth;
}
void OpaqueCapture::record_events(
    const int x, const int y, const uint32_t sample, const float *events, const int count)
{
  if (!max_events_ || count < 0 || count > max_events_ || (count && !events)) {
    fail();
    return;
  }
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample >= uint32_t(samples_)) {
    set_error(OUT_OF_RANGE);
    return;
  }
  for (int i = 0; i < count; ++i) {
    const float z = events[2 * i], a = events[2 * i + 1];
    if (!std::isfinite(z) || z <= 0 || !std::isfinite(a) || a < 0 || a > 1 ||
        (i && z < events[2 * (i - 1)]))
    {
      set_error(INVALID_DEPTH);
      return;
    }
  }
  const size_t index = (size_t(y) * width_ + x) * samples_ + sample;
  std::lock_guard<std::mutex> lock(mutex_);
  if (spill_) {
    std::array<float, 129> record{};
    record[0] = float(count + 1);
    if (count)
      std::copy(events, events + 2 * count, record.data() + 1);
    store_record(index, record.data());
    return;
  }
  if (values_[index] != -1.0f) {
    set_error(DUPLICATE);
    return;
  }
  for (int i = 0; i < 2 * count; ++i)
    events_[index * 2 * max_events_ + i] = events[i];
  values_[index] = float(count);
}
std::vector<SurfaceEvent> OpaqueCapture::events(const int x, const int y, const int sample) const
{
  const float v = value(x, y, sample);
  if (v < 0)
    throw std::runtime_error("Cannot read incomplete deep capture");
  if (!max_events_)
    return v == 0 ? std::vector<SurfaceEvent>{} : std::vector<SurfaceEvent>{{v, 1}};
  const size_t offset = ((size_t(y) * width_ + x) * samples_ + sample) * 2 * max_events_;
  std::vector<SurfaceEvent> result;
  if (spill_) {
    std::array<float, 129> record{};
    std::lock_guard<std::mutex> lock(mutex_);
    read_record((size_t(y) * width_ + x) * samples_ + sample, record.data());
    result.reserve(int(v));
    for (int i = 0; i < int(v); ++i)
      result.push_back({record[1 + 2 * i], record[2 + 2 * i]});
    return result;
  }
  for (int i = 0; i < int(v); ++i)
    result.push_back({events_[offset + 2 * i], events_[offset + 2 * i + 1]});
  return result;
}
bool OpaqueCapture::finalize() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_.load() != NONE)
    return false;
  if (spill_)
    return completed_ == count_ && std::fflush(spill_) == 0;
  for (const float value : values_)
    if (value == -1.0f)
      return false;
  return true;
}
const char *OpaqueCapture::error_message() const
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
      return "unsupported camera capture state or primitive, non-scalar opacity, texture cache "
             "miss, or deep traversal limit";
    default:
      return finalize() ? "" : "camera capture is incomplete";
  }
}
float OpaqueCapture::value(const int x, const int y, const int sample) const
{
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample < 0 || sample >= samples_)
    throw std::out_of_range("Deep capture read outside bounds");
  const size_t index = (size_t(y) * size_t(width_) + size_t(x)) * size_t(samples_) +
                       size_t(sample);
  if (spill_) {
    std::array<float, 129> record{};
    std::lock_guard<std::mutex> lock(mutex_);
    read_record(index, record.data());
    if (record[0] == 0)
      return -1;
    return max_events_ ? record[0] - 1 : (record[0] == 1 ? 0 : -record[0]);
  }
  return values_[index];
}
std::vector<SurfaceSample> OpaqueCapture::reconstruct_pixel(const int x, const int y) const
{
  if (error_.load() != NONE)
    throw std::runtime_error(error_message());
  PixelLedger ledger{x, y, {}};
  ledger.samples.reserve(samples_);
  for (int sample = 0; sample < samples_; ++sample) {
    CameraSample camera_sample{uint64_t(sample), 1.0, true, events(x, y, sample)};
    ledger.samples.push_back(std::move(camera_sample));
  }
  return reconstruct(ledger);
}
}  // namespace ccl::deep
