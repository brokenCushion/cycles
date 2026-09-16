/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ccl::deep {
OpaqueCapture::OpaqueCapture(const int width,
                             const int height,
                             const int samples,
                             const size_t max_bytes)
    : width_(width), height_(height), samples_(samples)
{
  if (width <= 0 || height <= 0 || samples <= 0 || samples > 4096) {
    throw std::invalid_argument("Deep capture requires positive dimensions and 1..4096 samples");
  }
  size_t count = size_t(width);
  for (const size_t factor : {size_t(height), size_t(samples)}) {
    if (count > std::numeric_limits<size_t>::max() / factor)
      throw std::invalid_argument("Deep capture dimensions overflow");
    count *= factor;
  }
  if (count > max_bytes / sizeof(float))
    throw std::invalid_argument("Deep raw capture exceeds --deep-memory-mb budget");
  values_.assign(count, -1.0f);
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
  if (values_[index] != -1.0f)
    set_error(DUPLICATE);
  else
    values_[index] = depth;
}
bool OpaqueCapture::finalize() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_.load() != NONE)
    return false;
  for (const float value : values_)
    if (value == -1.0f)
      return false;
  return true;
}
const char *OpaqueCapture::error_message() const
{
  switch (error_.load()) {
    case OUT_OF_RANGE:
      return "camera sample outside capture bounds";
    case DUPLICATE:
      return "camera sample recorded more than once";
    case INVALID_DEPTH:
      return "camera intersection depth is invalid";
    case UNSUPPORTED_STATE:
      return "unsupported camera capture state or primitive";
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
  return values_[index];
}
std::vector<SurfaceSample> OpaqueCapture::reconstruct_pixel(const int x, const int y) const
{
  if (error_.load() != NONE)
    throw std::runtime_error(error_message());
  PixelLedger ledger{x, y, {}};
  ledger.samples.reserve(samples_);
  for (int sample = 0; sample < samples_; ++sample) {
    const float depth = value(x, y, sample);
    if (depth < 0)
      throw std::runtime_error("Cannot reconstruct incomplete deep capture");
    CameraSample camera_sample{uint64_t(sample), 1.0, true, {}};
    if (depth > 0)
      camera_sample.events.push_back({depth, 1.0});
    ledger.samples.push_back(std::move(camera_sample));
  }
  return reconstruct(ledger);
}
}  // namespace ccl::deep
