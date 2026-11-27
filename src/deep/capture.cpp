/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#ifndef CCL_NAMESPACE_BEGIN
#  define CCL_NAMESPACE_BEGIN namespace ccl {
#  define CCL_NAMESPACE_END }
#endif
#include "kernel/deep/merge.h"
#include <map>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#ifdef WITH_CYCLES_DEEP_ZSTD
/* Static library: size estimates for the memory preflight. */
#  define ZSTD_STATIC_LINKING_ONLY
#  include <zstd.h>
#endif
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOGDI
#    define NOGDI
#  endif
#  include <windows.h>
#  include <winioctl.h>
#  include <fcntl.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace ccl::deep {
namespace {
constexpr uint64_t mebibyte = uint64_t(1024) * 1024;

std::string error_text(const int code)
{
#ifdef _WIN32
  char buffer[256] = {};
  strerror_s(buffer, sizeof(buffer), code);
  return buffer;
#else
  return std::strerror(code);
#endif
}

/* First-error I/O cause: operation, errno text, bytes attempted and the file. */
[[noreturn]] void io_failure(const std::string &operation, const std::string &path,
                             const size_t bytes, const int code)
{
  throw std::runtime_error(operation + " failed: " + error_text(code) + " (errno " +
                           std::to_string(code) + "), " + std::to_string(bytes) +
                           " bytes, file " + path);
}

/* Small persistent pool: the caller and helper threads share indices of one batch. */
class BatchPool {
 public:
  explicit BatchPool(const unsigned threads)
  {
    for (unsigned i = 1; i < threads; ++i)
      threads_.emplace_back([this] { loop(); });
  }
  ~BatchPool()
  {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    for (std::thread &thread : threads_)
      thread.join();
  }
  void run(const size_t count, std::function<void(size_t)> task)
  {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      task_ = std::move(task);
      count_ = count;
      next_ = 0;
      busy_ = threads_.size();
      error_ = nullptr;
      ++generation_;
    }
    wake_.notify_all();
    work();
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return busy_ == 0; });
    if (error_)
      std::rethrow_exception(error_);
  }

 private:
  void work()
  {
    for (size_t i; (i = next_.fetch_add(1)) < count_;) {
      try {
        task_(i);
      }
      catch (...) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!error_)
          error_ = std::current_exception();
      }
    }
  }
  void loop()
  {
    uint64_t seen = 0;
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_)
          return;
        seen = generation_;
      }
      work();
      const std::lock_guard<std::mutex> lock(mutex_);
      if (--busy_ == 0)
        done_.notify_one();
    }
  }
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable wake_, done_;
  std::function<void(size_t)> task_;
  std::atomic<size_t> next_{0};
  size_t count_ = 0, busy_ = 0;
  uint64_t generation_ = 0;
  std::exception_ptr error_;
  bool stop_ = false;
};

/* Free space below the reserve; reported as its own capture error. */
struct SpaceError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/* Smallest --deep-memory-mb whose available bytes A (budget minus adaptive
 * populations) satisfy A - floor(A / 8) >= needed: the staging eighth is
 * charged on top of the needed fixed working set. */
uint64_t minimum_memory_mb(const uint64_t needed, const size_t population_bytes)
{
  const uint64_t available = needed + (needed + 6) / 7 + 8;
  return (available + population_bytes + mebibyte - 1) / mebibyte;
}

/* Curve back to a camera sample: surfaces as events, spans as optical depth.
 * The online reducer's copy of the export-time tree's from_curve (same arithmetic). */
VolumeCameraSample curve_sample(const std::vector<IntervalSample> &curve, const uint64_t id,
                                const double weight)
{
  VolumeCameraSample sample{{id, weight, true, {}}, {}};
  const size_t surfaces = std::count_if(curve.begin(), curve.end(),
      [](const IntervalSample &span) { return span.front == span.back; });
  sample.camera.events.reserve(surfaces);
  sample.intervals.reserve(curve.size() - surfaces);
  for (const auto &span : curve)
    if (span.front == span.back)
      sample.camera.events.push_back({span.front, span.alpha, span.object, span.facing});
    else
      sample.intervals.push_back({span.front, span.back, -std::log1p(-span.alpha), span.object});
  return sample;
}

/* Exact node payload: {u32 intervals, u32 events}, then intervals
 * {f64 front, f64 back, f64 optical depth, i32 object} and events
 * {f64 depth, f64 alpha, i32 object, i32 facing}, in order. */
constexpr size_t node_interval_bytes = 28, node_event_bytes = 24;
std::vector<unsigned char> encode_volume(const VolumeCameraSample &sample)
{
  const uint32_t counts[2] = {uint32_t(sample.intervals.size()),
                              uint32_t(sample.camera.events.size())};
  std::vector<unsigned char> bytes(8 + counts[0] * node_interval_bytes +
                                   counts[1] * node_event_bytes);
  unsigned char *out = bytes.data();
  std::memcpy(out, counts, 8);
  out += 8;
  for (const auto &v : sample.intervals) {
    std::memcpy(out, &v.front, 8);
    std::memcpy(out + 8, &v.back, 8);
    std::memcpy(out + 16, &v.optical_depth, 8);
    std::memcpy(out + 24, &v.object, 4);
    out += node_interval_bytes;
  }
  for (const auto &e : sample.camera.events) {
    std::memcpy(out, &e.depth, 8);
    std::memcpy(out + 8, &e.alpha, 8);
    std::memcpy(out + 16, &e.object, 4);
    std::memcpy(out + 20, &e.facing, 4);
    out += node_event_bytes;
  }
  return bytes;
}
VolumeCameraSample decode_volume(const unsigned char *in, const size_t bytes, const uint64_t id,
                                 const double weight)
{
  uint32_t counts[2];
  if (bytes < 8)
    throw std::runtime_error("Truncated deep volume node");
  std::memcpy(counts, in, 8);
  if (bytes != 8 + uint64_t(counts[0]) * node_interval_bytes +
                   uint64_t(counts[1]) * node_event_bytes)
    throw std::runtime_error("Invalid deep volume node size");
  in += 8;
  VolumeCameraSample sample{{id, weight, true, {}}, {}};
  sample.camera.events.reserve(counts[1]);
  sample.intervals.reserve(counts[0]);
  for (uint32_t i = 0; i < counts[0]; ++i, in += node_interval_bytes) {
    VolumeInterval v;
    std::memcpy(&v.front, in, 8);
    std::memcpy(&v.back, in + 8, 8);
    std::memcpy(&v.optical_depth, in + 16, 8);
    std::memcpy(&v.object, in + 24, 4);
    sample.intervals.push_back(v);
  }
  for (uint32_t i = 0; i < counts[1]; ++i, in += node_event_bytes) {
    SurfaceEvent e;
    std::memcpy(&e.depth, in, 8);
    std::memcpy(&e.alpha, in + 8, 8);
    std::memcpy(&e.object, in + 16, 4);
    std::memcpy(&e.facing, in + 20, 4);
    sample.camera.events.push_back(e);
  }
  return sample;
}

#ifdef _WIN32
std::wstring widen(const std::string &text)
{
  if (text.empty())
    return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring result(size_t(std::max(n, 0)), L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), n);
  return result;
}
std::string narrow(const std::wstring &text)
{
  if (text.empty())
    return std::string();
  const int n = WideCharToMultiByte(
      CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(size_t(std::max(n, 0)), '\0');
  if (n > 0)
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), int(text.size()), result.data(), n, nullptr, nullptr);
  return result;
}
std::filesystem::path native_path(const std::string &text)
{
  return std::filesystem::path(widen(text));
}
std::string utf8_path(const std::filesystem::path &path)
{
  return narrow(path.wstring());
}
std::string environment(const wchar_t *name)
{
  wchar_t *value = nullptr;
  size_t length = 0;
  if (_wdupenv_s(&value, &length, name) != 0 || !value)
    return std::string();
  const std::string result = narrow(value);
  std::free(value);
  return result;
}
std::string system_message(const DWORD code)
{
  wchar_t *buffer = nullptr;
  const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0,
                                 nullptr);
  std::wstring text = n ? std::wstring(buffer, n) : std::wstring(L"unknown error");
  if (buffer)
    LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ' ||
                           text.back() == L'.'))
    text.pop_back();
  return narrow(text);
}
#  define CYCLES_DEEP_ENV(name) environment(L##name)
#else
std::filesystem::path native_path(const std::string &text)
{
  return std::filesystem::path(text);
}
std::string utf8_path(const std::filesystem::path &path)
{
  return path.string();
}
std::string environment(const char *name)
{
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
}
#  define CYCLES_DEEP_ENV(name) environment(name)
#endif
}  // namespace

bool Capture::spill_compression_supported()
{
#ifdef WITH_CYCLES_DEEP_ZSTD
  return true;
#else
  return false;
#endif
}

/* Online volume reduction: the export-time combine on encoded nodes. Parent
 * and children are aligned ranges, so ids and weights follow from the key. */
class Capture::VolumePolicy final : public ReducerPolicy {
 public:
  explicit VolumePolicy(const Capture &capture) : capture_(capture) {}
  std::vector<unsigned char> combine(const size_t pixel,
                                     const NodeKey &parent,
                                     const unsigned char *left,
                                     const size_t left_bytes,
                                     const unsigned char *right,
                                     const size_t right_bytes) override
  {
    const uint64_t half = uint64_t(1) << (parent.level - 1);
    const int x = int(pixel % size_t(capture_.width_)), y = int(pixel / size_t(capture_.width_));
    VolumeCameraSample merged = capture_.combine_volume(
        decode_volume(left, left_bytes, parent.first(), double(half)),
        decode_volume(right, right_bytes, parent.first() + half, double(half)),
        capture_.reduction_levels_, x, y, nullptr);
    const size_t n = merged.intervals.size() + merged.camera.events.size();
    const size_t limit = capture_.node_limit_ ? capture_.node_limit_ :
                                                capture_.reconstruction_limit();
    if (n > limit)
      throw std::runtime_error("Deep volume node exceeds the reconstruction limit: " +
                               std::to_string(n) + " intervals, limit " +
                               std::to_string(limit) + ", level " +
                               std::to_string(parent.level) + ", pixel " + std::to_string(x) +
                               "," + std::to_string(y));
    return encode_volume(merged);
  }

 private:
  const Capture &capture_;
};

/* Surface payloads: prefix nodes fold, other nodes concatenate raw samples. */
class Capture::SurfacePolicy final : public ReducerPolicy {
 public:
  explicit SurfacePolicy(const Capture &capture) : capture_(capture) {}
  std::vector<unsigned char> combine(const size_t,
                                     const NodeKey &parent,
                                     const unsigned char *left,
                                     const size_t left_bytes,
                                     const unsigned char *right,
                                     const size_t right_bytes) override
  {
    return SurfaceAccumulator::combine(parent, left, left_bytes, right, right_bytes,
                                       capture_.surface_settings(),
                                       &capture_.surface_statistics_);
  }

 private:
  const Capture &capture_;
};

/* Parallel host ingest (see Capture::set_ingest_threads). The producer copies
 * each record into its pixel's worker chunk; full chunks are queued to that
 * worker. Every chunk byte is charged to the reducer's memory share, and the
 * queued (including in-process) bytes are capped: the producer waits when the
 * cap is reached. Records of one pixel always go to the same worker, so one
 * pixel is never reduced by two threads at once. */
class Capture::Ingest {
 public:
  Ingest(Capture &capture, const unsigned threads, const size_t chunk_bytes,
         const size_t queue_bytes)
      : capture_(capture), chunk_bytes_(chunk_bytes), queue_bytes_(queue_bytes)
  {
    for (unsigned i = 0; i < threads; ++i)
      workers_.push_back(std::make_unique<Worker>());
    try {
      for (auto &item : workers_) {
        Worker *worker = item.get();
        worker->thread = std::thread([this, worker] { loop(*worker); });
      }
    }
    catch (...) {
      shutdown();
      throw;
    }
  }
  ~Ingest()
  {
    shutdown();
  }
  unsigned threads() const
  {
    return unsigned(workers_.size());
  }
  size_t chunk_bytes() const
  {
    return chunk_bytes_;
  }
  size_t queue_bytes() const
  {
    return queue_bytes_;
  }
  void push(const size_t pixel, const uint32_t sample, const KernelDeepResult &result,
            const KernelDeepEvent *events, const KernelDeepDensity *density)
  {
    const size_t event_bytes = size_t(result.count) * sizeof(KernelDeepEvent);
    const size_t density_bytes = density ? size_t(result.count) * sizeof(KernelDeepDensity) : 0;
    const size_t bytes = header_bytes + align(event_bytes) + align(density_bytes);
    const std::lock_guard<std::mutex> lock(producer_);
    Worker &w = *workers_[pixel % workers_.size()];
    if (!w.open.empty() && w.open.size() + bytes > w.open.capacity())
      submit(w);
    if (w.open.capacity() == 0) {
      w.open.reserve(std::max(chunk_bytes_, bytes));
      capture_.reducer_->account(int64_t(w.open.capacity()));
    }
    const size_t at = w.open.size();
    w.open.resize(at + bytes);
    unsigned char *out = w.open.data() + at;
    Header header{};
    header.pixel = uint64_t(pixel);
    header.sample = sample;
    header.density = density ? 1u : 0u;
    header.result = result;
    std::memcpy(out, &header, sizeof(header));
    if (event_bytes)
      std::memcpy(out + header_bytes, events, event_bytes);
    if (density_bytes)
      std::memcpy(out + header_bytes + align(event_bytes), density, density_bytes);
    ++records_;
    if (w.open.size() >= chunk_bytes_)
      submit(w);
  }
  /* Queues every partial chunk and waits until all workers are idle. */
  void drain()
  {
    {
      const std::lock_guard<std::mutex> lock(producer_);
      for (auto &w : workers_)
        if (!w->open.empty())
          submit(*w);
    }
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [&] {
      if (stop_.load())
        return true;
      if (busy_)
        return false;
      for (const auto &w : workers_)
        if (!w->queue.empty())
          return false;
      return true;
    });
  }
  std::string summary() const
  {
    std::ostringstream out;
    out << " ingest_threads=" << workers_.size() << " ingest_records=" << records_.load()
        << " ingest_chunk_bytes=" << chunk_bytes_ << " ingest_queue_bytes=" << queue_bytes_
        << " ingest_queue_peak=" << peak_.load()
        << " ingest_producer_wait_seconds=" << wait_ns_.load() * 1e-9;
    return out.str();
  }

 private:
  struct Header {
    uint64_t pixel;
    uint32_t sample, density;
    KernelDeepResult result;
  };
  static constexpr size_t align(const size_t bytes)
  {
    return (bytes + 15) & ~size_t(15);
  }
  static constexpr size_t header_bytes = (sizeof(Header) + 15) & ~size_t(15);
  struct Worker {
    std::deque<std::vector<unsigned char>> queue;
    std::vector<unsigned char> open; /* Producer-owned (under producer_). */
    std::condition_variable wake;
    std::thread thread;
  };

  /* Caller holds producer_. Waits while the queue cap is reached. */
  void submit(Worker &w)
  {
    std::vector<unsigned char> chunk;
    chunk.swap(w.open);
    const size_t bytes = chunk.capacity();
    std::unique_lock<std::mutex> lock(mutex_);
    if (!(stop_.load() || queued_ == 0 || queued_ + bytes <= queue_bytes_)) {
      const auto start = std::chrono::steady_clock::now();
      space_.wait(lock, [&] { return stop_.load() || queued_ == 0 || queued_ + bytes <= queue_bytes_; });
      wait_ns_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count());
    }
    if (stop_.load()) {
      lock.unlock();
      capture_.reducer_->account(-int64_t(bytes));
      return;
    }
    queued_ += bytes;
    if (queued_ > peak_.load())
      peak_.store(queued_);
    w.queue.push_back(std::move(chunk));
    w.wake.notify_one();
  }
  void process(const std::vector<unsigned char> &chunk)
  {
    size_t at = 0;
    while (at < chunk.size() && !stop_.load()) {
      Header header;
      std::memcpy(&header, chunk.data() + at, sizeof(header));
      const size_t event_bytes = size_t(header.result.count) * sizeof(KernelDeepEvent);
      const auto *events = reinterpret_cast<const KernelDeepEvent *>(chunk.data() + at +
                                                                     header_bytes);
      const auto *density = header.density ?
                                reinterpret_cast<const KernelDeepDensity *>(
                                    chunk.data() + at + header_bytes + align(event_bytes)) :
                                nullptr;
      if (!capture_.failed())
        capture_.reduce_record(size_t(header.pixel), header.sample, header.result,
                               header.result.count ? events : nullptr, density);
      at += header_bytes + align(event_bytes) +
            (header.density ? align(size_t(header.result.count) * sizeof(KernelDeepDensity)) : 0);
    }
  }
  void loop(Worker &w)
  {
    while (true) {
      std::vector<unsigned char> chunk;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        w.wake.wait(lock, [&] { return stop_.load() || !w.queue.empty(); });
        if (stop_.load())
          return;
        chunk.swap(w.queue.front());
        w.queue.pop_front();
        ++busy_;
      }
      try {
        process(chunk);
      }
      catch (const std::exception &error) {
        capture_.set_error(REDUCTION, std::string("parallel ingest worker: ") + error.what());
      }
      const size_t bytes = chunk.capacity();
      std::vector<unsigned char>().swap(chunk);
      capture_.reducer_->account(-int64_t(bytes));
      {
        const std::lock_guard<std::mutex> lock(mutex_);
        queued_ -= bytes;
        --busy_;
      }
      space_.notify_all();
      idle_.notify_all();
    }
  }
  /* Stops and joins every worker, discarding queued records (destruction). */
  void shutdown()
  {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      stop_.store(true);
    }
    for (auto &w : workers_)
      w->wake.notify_all();
    space_.notify_all();
    idle_.notify_all();
    for (auto &w : workers_)
      if (w->thread.joinable())
        w->thread.join();
    int64_t bytes = 0;
    for (auto &w : workers_) {
      for (const auto &chunk : w->queue)
        bytes += int64_t(chunk.capacity());
      bytes += int64_t(w->open.capacity());
      w->queue.clear();
      std::vector<unsigned char>().swap(w->open);
    }
    if (bytes && capture_.reducer_)
      capture_.reducer_->account(-bytes);
  }

  Capture &capture_;
  const size_t chunk_bytes_, queue_bytes_;
  std::vector<std::unique_ptr<Worker>> workers_;
  std::mutex producer_, mutex_;
  std::condition_variable space_, idle_;
  size_t queued_ = 0, busy_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> records_{0}, peak_{0}, wait_ns_{0};
};

Capture::Band::~Band()
{
  if (index)
    std::fclose(index);
#ifdef WITH_CYCLES_DEEP_ZSTD
  ZSTD_freeCCtx(static_cast<ZSTD_CCtx *>(compressor));
  ZSTD_freeDCtx(static_cast<ZSTD_DCtx *>(decompressor));
#endif
}

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
                 const float error,
                 const int sample_limit,
                 const bool ids,
                 const std::string &spill_dir,
                 const size_t reserved_bytes,
                 const bool retain_samples)
    : error_setting_(error), width_(width), height_(height), samples_(samples), max_events_(max_events), volume_(volume),
      volume_grid_(volume_grid)
{
  if (sample_limit < 0 || (sample_limit && sample_limit != samples))
    throw std::invalid_argument("Invalid deep sample limit");
  sample_limit_ = sample_limit;
  ids_ = ids;
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
  max_bytes_ = max_bytes;
  population_bytes_ = population_bytes;
  reserved_bytes_ = reserved_bytes;
  if (spill) {
    /* Explicit directory, then CYCLES_DEEP_SPILL_DIR, then the system temp directory. */
    spill_directory_ = spill_dir;
    if (spill_directory_.empty())
      spill_directory_ = CYCLES_DEEP_ENV("CYCLES_DEEP_SPILL_DIR");
    if (spill_directory_.empty()) {
      std::error_code ec;
      spill_directory_ = utf8_path(std::filesystem::temp_directory_path(ec));
      if (ec)
        throw std::invalid_argument("Cannot determine the system temp directory for deep spill: " +
                                    ec.message());
    }
    {
      std::error_code ec;
      if (!std::filesystem::is_directory(native_path(spill_directory_), ec))
        throw std::invalid_argument("Deep spill directory does not exist or is not a directory: " +
                                    spill_directory_);
    }
    const std::string compression = CYCLES_DEEP_ENV("CYCLES_DEEP_SPILL_COMPRESSION");
#ifdef WITH_CYCLES_DEEP_ZSTD
    compression_ = 1;
    if (!compression.empty()) {
      char *end = nullptr;
      const long level = std::strtol(compression.c_str(), &end, 10);
      if (*end || level < 0 || level > ZSTD_maxCLevel())
        throw std::invalid_argument("CYCLES_DEEP_SPILL_COMPRESSION must be 0 (raw) or a zstd level");
      compression_ = int(level);
    }
#endif
    const std::string quota = CYCLES_DEEP_ENV("CYCLES_DEEP_SPILL_MAX_MB");
    if (!quota.empty()) {
      char *end = nullptr;
      const unsigned long long megabytes = std::strtoull(quota.c_str(), &end, 10);
      if (*end || megabytes == 0 || megabytes > UINT64_MAX / mebibyte)
        throw std::invalid_argument("CYCLES_DEEP_SPILL_MAX_MB must be a positive MiB count");
      spill_quota_bytes_ = megabytes * mebibyte;
    }
    /* Two MiB covers 480-lane host/device events and 64 media (1,994,496 bytes),
     * fixed scratch and I/O. Beauty/shader memory remains outside this budget.
     * Export decode scratch of a compressed band stream (export_decode_bytes,
     * about 2.8 MiB) is charged to the staging allowance when the band is
     * staged; a band that does not fit with it is re-bucketed into raw rows. */
    const uint64_t events = uint64_t(samples) * capacity_;
    const uint64_t output_events = volume ? reconstruction_limit() : events;
    /* Volume pixel streaming retains only 12-byte FLOAT samples per row.
     * OpenEXR 3.4 ZIPS with zero workers has one line buffer: raw + consecutive
     * + ZIP scratch + compressed data, plus a transient replacement buffer.
     * 80 bytes/sample covers these copies and compression overhead. Double
     * source/quantization/curve scratch is bounded per pixel, not per row. */
    const uint64_t row_sample_bytes = volume ? (ids ? 128 : 80) : 128;
    rows_per_band_ = (height - 1) / 64 + 1;
    const size_t band_count = (height - 1) / rows_per_band_ + 1;
#ifdef _WIN32
    if (band_count * (volume_grid && samples > 8 ? 4 : 2) + size_t(rows_per_band_) + 16 >
        size_t(_getmaxstdio()))
      throw std::invalid_argument("Deep band files exceed stdio open-file limit");
#endif
    spill_memory_bytes_ = size_t((max_bytes - population_bytes) / 8);
#ifdef WITH_CYCLES_DEEP_ZSTD
    /* Each band's compressed stream holds a pending block, a stored-block buffer
     * and a zstd context until finalize. Export staging is idle during capture,
     * so this state uses the staging allowance; when it does not fit, spill stays
     * raw. Export decoding (one context plus one block) is fixed scratch. */
    if (compression_ &&
        (2 * event_page_bytes + 1024 + ZSTD_estimateCCtxSize_usingCParams(ZSTD_getCParams(
                                           compression_, event_page_bytes, 0))) *
                band_count >
            spill_memory_bytes_)
    {
      compression_ = 0;
      compression_limited_ = true;
    }
#endif
    const uint64_t shared = 2 * 1024 * 1024 +
                              (spill_page_bytes * spill_page_count + event_page_bytes) * band_count +
                              spill_memory_bytes_ +
                              uint64_t(width) * 256 +
                              uint64_t(height) * 32;
    const uint64_t worker =
                              (volume ? uint64_t(capacity_) * 512 : events * 512) +
                              (volume ? output_events * (ids ? 512 : 240) : 0) +
                              (volume ? uint64_t(samples) * 256 : 0) +
                              (volume_grid ? uint64_t(2) * reconstruction_limit() * 96 : 0);
    const uint64_t available = max_bytes - population_bytes;
    uint64_t fixed = shared + worker;
    const auto working_set_failure = [&](const uint64_t row) {
      const uint64_t needed = fixed - spill_memory_bytes_ + row;
      return std::invalid_argument(
          "Deep scanline working set exceeds --deep-memory-mb budget: needs " +
          std::to_string(fixed + row) + " bytes (fixed " + std::to_string(fixed - spill_memory_bytes_) +
          " + staging " + std::to_string(spill_memory_bytes_) + " + row " + std::to_string(row) +
          "), available " + std::to_string(available) + " bytes; minimum deep_memory_mb " +
          std::to_string(minimum_memory_mb(needed, population_bytes + reserved_bytes)));
    };
    if (fixed > available)
      throw working_set_failure(0);
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
        throw std::invalid_argument(
            "Deep volume row cannot fit one maximum-capacity pixel: row capacity " +
            std::to_string(volume_row_sample_limit_) + " samples (" + std::to_string(row_bytes) +
            " bytes at " + std::to_string(row_sample_bytes) + " bytes/sample), pixel maximum " +
            std::to_string(output_events) + " samples; increase --deep-memory-mb");
    }
    else if (uint64_t(width) * output_events * row_sample_bytes > available - fixed)
      throw working_set_failure(uint64_t(width) * output_events * row_sample_bytes);
    if (adaptive)
      populations_.assign(count / samples, 0);
    row_event_bytes_.assign(height, 0);
    stride_ = sizeof(SpillRecord);
    std::array<unsigned char, 65536> zeros{};
    index_sparse_ = true;
    /* Grid volumes above 8 samples reduce online; the raw spill remains only
     * when per-sample records are requested (or reduction is disabled). */
    const bool reduce_volume = volume_grid && samples > 8 &&
                               CYCLES_DEEP_ENV("CYCLES_DEEP_VOLUME_REDUCE") != "0";
    /* Numeric surfaces accumulate online; requested per-sample records keep
     * the raw spill next to it (as for volumes), export reads the curve.
     * Strict surfaces keep the raw path (no merge allowance, and the raw path
     * is their byte-identity reference); so do 8 or fewer samples, whose raw
     * list is already bounded (as for volumes). The reducer's resident pixel
     * state must fit half its share; otherwise the raw path is used. */
    const size_t surface_share = retain_samples ? spill_memory_bytes_ / 2 : spill_memory_bytes_;
    bool reduce_surface = !volume && error > 0 && samples > 8 &&
                          CYCLES_DEEP_ENV("CYCLES_DEEP_SURFACE_REDUCE") != "0" &&
                          PixelReducer::fixed_bytes(width, height) <= surface_share / 2;
    if (reduce_surface) {
      const std::string capacity = CYCLES_DEEP_ENV("CYCLES_DEEP_SURFACE_CAPACITY");
      if (!capacity.empty()) {
        char *end = nullptr;
        const unsigned long long value = std::strtoull(capacity.c_str(), &end, 10);
        if (*end || value < 2 || value > 1000000)
          throw std::invalid_argument("CYCLES_DEEP_SURFACE_CAPACITY must be 2..1000000 breakpoints");
        surface_capacity_ = size_t(value);
      }
    }
    const bool raw_spill = !(reduce_volume || reduce_surface) || retain_samples;
    retain_samples_ = raw_spill;
    for (int y = 0; y < height; y += rows_per_band_) {
      bands_.push_back(std::make_unique<Band>());
      Band &band = *bands_.back();
      band.first_y = y;
      band.rows = std::min(rows_per_band_, height - y);
      band.pixels = size_t(width) * band.rows;
      if (!raw_spill)
        continue;
      band.records = band.pixels * samples;
      index_total_bytes_ += band.records * stride_;
      /* The fixed index is a sparse file of its final size, so unwritten pages read
       * as zeros without being written up front. Without sparse support, zero-fill. */
      bool sparse = false, unused = false;
      band.index = open_spill_file("index", band.records * stride_, sparse, band.index_path);
      band.events.file = open_spill_file("events", 0, unused, band.events.path);
      for (SpillPage &page : band.pages)
        page.data.resize(spill_page_bytes);
      band.event_page.data.resize(event_page_bytes);
      size_t remaining = sparse ? 0 : band.records * stride_;
      index_sparse_ = index_sparse_ && sparse;
      while (remaining) {
        const size_t n = std::min(remaining, zeros.size());
        if (std::fwrite(zeros.data(), 1, n, band.index) != n)
          io_failure("Deep band index allocation", band.index_path, n, errno);
        band.write_bytes += n;
        remaining -= n;
      }
      if (std::fflush(band.index) != 0)
        io_failure("Deep band index flush", band.index_path, 0, errno);
    }
    if (reduce_volume) {
      /* Same per-level allowance as the export-time tree. Levels come from the
       * sample cap, which equals the population without adaptive sampling. */
      for (size_t n = samples; n > 2; n = (n + 1) / 2)
        ++reduction_levels_;
      volume_policy_ = std::make_unique<VolumePolicy>(*this);
      /* The reducer uses the export staging share, idle during capture; with a
       * retained raw spill both share it. */
      make_reducer(raw_spill ? spill_memory_bytes_ / 2 : spill_memory_bytes_,
                   error_budget(error).reconstruction / (2 * reduction_levels_));
    }
    if (reduce_surface) {
      /* The ledger lives in the payload; the reducer's per-level step is 0. */
      surface_policy_ = std::make_unique<SurfacePolicy>(*this);
      make_reducer(surface_share, 0);
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
Capture::~Capture()
{
  /* Join ingest workers before the reducer and spill state go away. */
  ingest_.reset();
}
void Capture::make_reducer(const size_t ram, const double step)
{
  const int threads = ingest_threads();
  if (ingest_) {
    ingest_->drain();
    ingest_.reset();
  }
  ReducerIo io;
  io.open = [this](std::string &path) {
    bool unused = false;
    return open_spill_file("nodes", 0, unused, path);
  };
  io.written = [this](const uint64_t bytes) { spill_written(bytes); };
  io.released = [this](const uint64_t bytes) { spill_released_ += bytes; };
  reducer_.reset();
  ReducerPolicy &policy = volume_policy_ ? static_cast<ReducerPolicy &>(*volume_policy_) :
                                           static_cast<ReducerPolicy &>(*surface_policy_);
  reducer_ = std::make_unique<PixelReducer>(width_, height_, rows_per_band_, uint32_t(samples_),
                                            ram, step, compression_, std::move(io), policy);
  if (threads)
    set_ingest_threads(threads);
}
void Capture::set_ingest_threads(int threads)
{
  if (ingest_) {
    ingest_->drain();
    ingest_.reset();
  }
  const std::string text = CYCLES_DEEP_ENV("CYCLES_DEEP_INGEST_THREADS");
  if (!text.empty()) {
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (*end || value < 0 || value > 256)
      throw std::invalid_argument("CYCLES_DEEP_INGEST_THREADS must be 0 (synchronous) to 256");
    threads = int(value);
  }
  if (!reducer_ || threads <= 0 || exporting_)
    return;
  threads = std::min(threads, 64);
  /* Queue: an eighth of the reducer share, 64 KiB..64 MiB; chunks let every worker
   * hold work while the producer fills the next one. */
  const size_t queue = std::clamp<size_t>(reducer_->ram_limit() / 8, 64 * 1024, 64 * mebibyte);
  size_t chunk = std::clamp<size_t>(queue / (4 * size_t(threads)), 4096, 256 * 1024);
  const std::string chunk_text = CYCLES_DEEP_ENV("CYCLES_DEEP_INGEST_CHUNK");
  if (!chunk_text.empty()) {
    char *end = nullptr;
    const unsigned long long value = std::strtoull(chunk_text.c_str(), &end, 10);
    if (*end || value == 0 || value > 64 * mebibyte)
      throw std::invalid_argument("CYCLES_DEEP_INGEST_CHUNK must be 1..67108864 bytes");
    chunk = size_t(value);
  }
  ingest_ = std::make_unique<Ingest>(*this, unsigned(threads), chunk, queue);
}
int Capture::ingest_threads() const
{
  return ingest_ ? int(ingest_->threads()) : 0;
}
void Capture::drain_ingest() const
{
  if (ingest_)
    ingest_->drain();
}

void Capture::open_diagnostics(const std::string &path)
{
  if (path.empty() || volume_)
    return;
  diagnostics_.open(path);
  diagnostics_ << "file_x,file_y,sample,stage,type,primitive,segment,object,u,v,t,facing,depth,alpha,object_id\n"
               << std::setprecision(std::numeric_limits<float>::max_digits10);
  if (!diagnostics_)
    throw std::runtime_error("Cannot open deep hit diagnostics");
}

void Capture::diagnostic_hit(const int x, const int y, const uint32_t sample,
                             const KernelDeepHit &hit)
{
  if (!diagnostics())
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  diagnostics_ << x << ',' << height_ - 1 - y << ',' << sample << ',' << hit.stage
               << ',' << hit.type << ',' << hit.primitive << ',' << hit.segment << ',' << hit.object
               << ',' << hit.u << ',' << hit.v << ',' << hit.t << ',' << hit.facing
               << ',' << hit.depth << ',' << hit.alpha << ','
               << (ids_ && hit.object >= 0 && size_t(hit.object) < object_manifest.size() ?
                       object_manifest[hit.object].first : 0) << '\n';
  /* Terminal records survive a failed traversal; this is not an EXR completion marker. */
  if (hit.stage >= 2)
    diagnostics_.flush();
  if (!diagnostics_)
    set_error(IO_ERROR);
}
Capture::SpillStatistics Capture::spill_statistics() const
{
  SpillStatistics result;
  for (const auto &item : bands_) {
    const Band &band = *item;
    const std::lock_guard<std::mutex> lock(band.mutex);
    result.read_bytes += band.read_bytes;
    result.write_bytes += band.write_bytes;
    result.index_bytes += band.records * stride_;
    result.event_raw_bytes += band.event_bytes;
    result.event_file_bytes += compression_ ? band.events.file_bytes : band.event_bytes;
  }
  if (reducer_) {
    const auto nodes = reducer_->statistics();
    result.node_file_bytes = nodes.file_bytes;
    result.node_file_peak = nodes.file_peak;
    result.node_ram_peak = nodes.ram_peak;
    result.read_bytes += nodes.file_read_bytes;
    result.write_bytes += nodes.stored_written;
  }
  result.file_bytes = result.index_bytes + result.event_file_bytes + result.node_file_bytes;
  return result;
}
void Capture::end_pass(const std::function<bool()> &cancelled)
{
  drain_ingest();
  if (!reducer_ || failed())
    return;
  try {
    reducer_->compact(cancelled);
  }
  catch (const SpaceError &error) {
    set_error(SPILL_SPACE, error.what());
  }
  catch (const std::exception &error) {
    set_error(IO_ERROR, error.what());
  }
}
std::string Capture::reducer_summary() const
{
  if (!reducer_)
    return std::string();
  const auto s = reducer_->statistics();
  std::ostringstream out;
  if (surface_policy_)
    out << "kind=surface ";
  out << "levels=" << (surface_policy_ ? 0 : reduction_levels_) << " merges=" << s.merges
      << " merge_worker_seconds=" << s.merge_seconds << " ram_bytes=" << s.ram_bytes
      << " ram_peak=" << s.ram_peak << " ram_limit=" << s.ram_limit
      << " node_file_bytes=" << s.file_bytes << " node_live_file_bytes=" << s.live_file_bytes
      << " node_file_peak=" << s.file_peak << " node_raw_written=" << s.raw_written
      << " node_stored_written=" << s.stored_written << " node_read_bytes=" << s.file_read_bytes
      << " evicted=" << s.evicted_nodes << " reloaded=" << s.reloaded_nodes
      << " compactions=" << s.compactions << " raw_samples=" << (retain_samples_ ? 1 : 0)
      << " evict_sweeps=" << s.sweeps << " evict_worker_seconds=" << s.evict_seconds
      << " evict_wait_seconds=" << s.wait_seconds << " compact_seconds=" << s.compact_seconds
      << " table_bytes=" << s.table_bytes << " codec_bytes=" << s.codec_bytes;
  if (ingest_)
    out << ingest_->summary();
  if (surface_policy_) {
    const auto &a = surface_statistics_;
    out << " surface_capacity=" << surface_capacity_ << " surface_budget=" << surface_merge_budget()
        << " folded=" << a.folded.load() << " max_breakpoints=" << a.max_breakpoints.load()
        << " capacity_merges=" << a.capacity_merges.load()
        << " depth_merges=" << a.depth_merges.load()
        << " overflow_folds=" << a.overflow_folds.load()
        << " max_ledger=" << SurfaceAccumulatorStatistics::value(a.max_ledger);
  }
  return out.str();
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
    throw std::runtime_error(std::string("Deep spill seek failed: ") + error_text(errno) +
                             " (errno " + std::to_string(errno) + "), offset " +
                             std::to_string(offset));
}
}  // namespace

FILE *Capture::open_spill_file(const char *kind, const uint64_t sparse_bytes, bool &sparse,
                               std::string &path) const
{
  static std::atomic<uint64_t> sequence{0};
  sparse = false;
#ifdef _WIN32
  const std::filesystem::path directory = native_path(spill_directory_);
  HANDLE handle = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 64 && handle == INVALID_HANDLE_VALUE; ++attempt) {
    const std::filesystem::path name =
        directory / ("cycles-deep-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(sequence++) + "-" + kind + ".tmp");
    path = utf8_path(name);
    /* The system deletes the file when its last handle closes, including on
     * process termination. Exclusive sharing keeps other processes out. */
    handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
      break;
  }
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    throw std::runtime_error("Cannot create deep spill file " + path + ": " +
                             system_message(code) + " (Windows error " + std::to_string(code) +
                             ")");
  }
  if (sparse_bytes) {
    DWORD returned = 0;
    if (DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
      LARGE_INTEGER size, zero;
      size.QuadPart = LONGLONG(sparse_bytes);
      zero.QuadPart = 0;
      sparse = SetFilePointerEx(handle, size, nullptr, FILE_BEGIN) && SetEndOfFile(handle);
      if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN))
        sparse = false;
    }
  }
  const int descriptor = _open_osfhandle(intptr_t(handle), _O_RDWR | _O_BINARY);
  if (descriptor == -1) {
    CloseHandle(handle);
    throw std::runtime_error("Cannot open deep spill file descriptor " + path);
  }
  FILE *file = _fdopen(descriptor, "w+b");
  if (!file) {
    const int code = errno;
    _close(descriptor);
    io_failure("Deep spill stream open", path, 0, code);
  }
#else
  std::string name = (std::filesystem::path(spill_directory_) /
                      ("cycles-deep-" + std::to_string(getpid()) + "-" +
                       std::to_string(sequence++) + "-" + kind + "-XXXXXX"))
                         .string();
  const int descriptor = mkstemp(name.data());
  path = name;
  if (descriptor == -1)
    io_failure("Deep spill file creation", path, 0, errno);
  /* Unlinked while open: removed when closed or when the process exits. */
  unlink(name.c_str());
  if (sparse_bytes)
    sparse = ftruncate(descriptor, off_t(sparse_bytes)) == 0;
  FILE *file = fdopen(descriptor, "w+b");
  if (!file) {
    const int code = errno;
    close(descriptor);
    io_failure("Deep spill stream open", path, 0, code);
  }
#endif
  return file;
}

void Capture::spill_written(const uint64_t bytes) const
{
  constexpr uint64_t step = 64 * mebibyte;
  const uint64_t before = spill_written_.fetch_add(bytes);
  const uint64_t written = before + bytes;
  if (written / step == before / step)
    return;
  /* Stop before the drive (or quota) is exhausted; reports carry both numbers. */
  std::error_code ec;
  const auto info = std::filesystem::space(native_path(spill_directory_), ec);
  uint64_t available = ec ? UINT64_MAX : uint64_t(info.available);
  /* Compaction gives node stream bytes back: the quota charges live bytes. */
  const uint64_t released = std::min(spill_released_.load(), written);
  const uint64_t live = written - released;
  if (spill_quota_bytes_) {
    const uint64_t used = live + index_total_bytes_;
    available = std::min(available, spill_quota_bytes_ > used ? spill_quota_bytes_ - used : 0);
  }
  if (available >= spill_reserve_bytes)
    return;
  const size_t done = completed_.load();
  throw SpaceError(
      "free space in " + spill_directory_ +
      (spill_quota_bytes_ ? " (with CYCLES_DEEP_SPILL_MAX_MB quota " +
                                std::to_string(spill_quota_bytes_) + " bytes)" :
                            std::string()) +
      " fell to " + std::to_string(available) + " bytes after " + std::to_string(written) +
      " stored spill bytes (" + std::to_string(live) + " live, " +
      std::to_string(index_total_bytes_) + " index; " + std::to_string(done) + " of " +
      std::to_string(count_) + " camera samples" +
      (done && !reducer_ ?
           ", projected " + std::to_string(uint64_t((long double)written * count_ / done)) +
               " bytes" :
           std::string()) +
      "); reserve " + std::to_string(spill_reserve_bytes) + " bytes");
}

void Capture::append(Band &band, SpillStream &stream, const void *data, const size_t bytes) const
{
  if (!bytes)
    return;
  if (!compression_ || stream.raw) {
    if (stream.reading) {
      seek_record(stream.file, stream.file_bytes);
      stream.reading = false;
    }
    if (std::fwrite(data, 1, bytes, stream.file) != bytes)
      io_failure("Deep spill append", stream.path, bytes, errno);
    stream.raw_bytes += bytes;
    stream.file_bytes += bytes;
    band.write_bytes += bytes;
    spill_written(bytes);
    return;
  }
  const unsigned char *source = static_cast<const unsigned char *>(data);
  size_t remaining = bytes;
  while (remaining) {
    if (stream.pending.capacity() < event_page_bytes)
      stream.pending.reserve(event_page_bytes);
    const size_t n = std::min(remaining, event_page_bytes - stream.pending.size());
    stream.pending.insert(stream.pending.end(), source, source + n);
    source += n;
    remaining -= n;
    stream.raw_bytes += n;
    if (stream.pending.size() == event_page_bytes)
      flush_stream(band, stream);
  }
}

void Capture::flush_stream(Band &band, SpillStream &stream) const
{
  if (stream.pending.empty())
    return;
#ifdef WITH_CYCLES_DEEP_ZSTD
  if (!band.compressor && !(band.compressor = ZSTD_createCCtx()))
    throw std::runtime_error("Cannot create deep spill compressor");
  const size_t raw = stream.pending.size();
  band.block.resize(8 + ZSTD_compressBound(event_page_bytes));
  size_t stored = ZSTD_compressCCtx(static_cast<ZSTD_CCtx *>(band.compressor),
                                    band.block.data() + 8, band.block.size() - 8,
                                    stream.pending.data(), raw, compression_);
  /* Incompressible (or failed) blocks are stored raw; decoding is lossless either way. */
  if (ZSTD_isError(stored) || stored >= raw) {
    stored = raw;
    std::memcpy(band.block.data() + 8, stream.pending.data(), raw);
  }
  const uint32_t header[2] = {uint32_t(raw), uint32_t(stored)};
  std::memcpy(band.block.data(), header, sizeof(header));
  if (stream.reading) {
    seek_record(stream.file, stream.file_bytes);
    stream.reading = false;
  }
  const size_t bytes = sizeof(header) + stored;
  if (std::fwrite(band.block.data(), 1, bytes, stream.file) != bytes)
    io_failure("Deep spill block write", stream.path, bytes, errno);
  stream.file_bytes += bytes;
  band.write_bytes += bytes;
  stream.pending.clear();
  spill_written(bytes);
#else
  (void)band;
  throw std::logic_error("Deep spill compression is unavailable in this build");
#endif
}

uint64_t Capture::read_block(Band &band, SpillStream &stream, const uint64_t offset,
                             unsigned char *destination, size_t &raw) const
{
#ifdef WITH_CYCLES_DEEP_ZSTD
  uint32_t header[2];
  if (offset > stream.file_bytes || stream.file_bytes - offset < sizeof(header))
    throw std::runtime_error("Truncated deep spill block header in " + stream.path);
  seek_record(stream.file, size_t(offset));
  stream.reading = true;
  if (std::fread(header, 1, sizeof(header), stream.file) != sizeof(header))
    io_failure("Deep spill block header read", stream.path, sizeof(header), errno);
  raw = header[0];
  const size_t stored = header[1];
  if (raw == 0 || raw > event_page_bytes || stored > raw ||
      stored > stream.file_bytes - offset - sizeof(header))
    throw std::runtime_error("Invalid deep spill block in " + stream.path);
  if (stored == raw) {
    if (std::fread(destination, 1, raw, stream.file) != raw)
      io_failure("Deep spill block read", stream.path, raw, errno);
  }
  else {
    band.block.resize(std::max(band.block.size(), stored));
    if (std::fread(band.block.data(), 1, stored, stream.file) != stored)
      io_failure("Deep spill block read", stream.path, stored, errno);
    if (!band.decompressor && !(band.decompressor = ZSTD_createDCtx()))
      throw std::runtime_error("Cannot create deep spill decompressor");
    const size_t decoded = ZSTD_decompressDCtx(static_cast<ZSTD_DCtx *>(band.decompressor),
                                               destination, raw, band.block.data(), stored);
    if (ZSTD_isError(decoded) || decoded != raw)
      throw std::runtime_error("Corrupt deep spill block in " + stream.path);
  }
  band.read_bytes += sizeof(header) + stored;
  return offset + sizeof(header) + stored;
#else
  (void)band;
  (void)stream;
  (void)offset;
  (void)destination;
  (void)raw;
  throw std::logic_error("Deep spill compression is unavailable in this build");
#endif
}

/* Capture holds only the destination band's mutex. */
void Capture::flush_page(Band &band, SpillPage &page) const
{
  if (!page.dirty_end)
    return;
  const size_t bytes = page.dirty_end - page.dirty_begin;
  seek_record(band.index, page.first * stride_ + page.dirty_begin);
  if (std::fwrite(page.data.data() + page.dirty_begin, 1, bytes, band.index) != bytes)
    io_failure("Deep spill index page write", band.index_path, bytes, errno);
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
      io_failure("Deep spill index page read", band.index_path, bytes, errno);
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
  if (compression_) {
    /* Pre-export reads (validation only) scan block headers forward from the
     * cached block, or from the start; blocks are immutable once stored. */
    flush_stream(band, band.events);
    EventPage &page = band.event_page;
    while (bytes) {
      if (page.first == size_t(-1) || offset < page.first || offset >= page.first + page.bytes) {
        const bool forward = page.first != size_t(-1) && offset >= page.first + page.bytes;
        uint64_t file_offset = forward ? page.next_block : 0;
        size_t raw_offset = forward ? page.first + page.bytes : 0;
        page.first = size_t(-1);
        while (true) {
          uint32_t header[2];
          if (file_offset > band.events.file_bytes ||
              band.events.file_bytes - file_offset < sizeof(header))
            throw std::runtime_error("Deep event offset beyond stored blocks");
          seek_record(band.events.file, size_t(file_offset));
          band.events.reading = true;
          if (std::fread(header, 1, sizeof(header), band.events.file) != sizeof(header))
            io_failure("Deep spill block header read", band.events.path, sizeof(header), errno);
          band.read_bytes += sizeof(header);
          if (offset < raw_offset + header[0])
            break;
          raw_offset += header[0];
          file_offset += sizeof(header) + header[1];
        }
        size_t raw = 0;
        page.next_block = read_block(band, band.events, file_offset, page.data.data(), raw);
        page.first = raw_offset;
        page.bytes = raw;
      }
      const size_t n = std::min(bytes, page.bytes - (offset - page.first));
      std::memcpy(destination, page.data.data() + offset - page.first, n);
      destination += n;
      offset += n;
      bytes -= n;
    }
    return;
  }
  while (bytes) {
    const size_t first = offset / event_page_bytes * event_page_bytes;
    EventPage &page = band.event_page;
    if (page.first != first) {
      page.first = size_t(-1);
      page.bytes = std::min(event_page_bytes, band.event_bytes - first);
      seek_record(band.events.file, first);
      band.events.reading = true;
      if (std::fread(page.data.data(), 1, page.bytes, band.events.file) != page.bytes)
        io_failure("Deep event file read", band.events.path, page.bytes, errno);
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
        if (has_density(deep_event_type(events[i]))) {
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
      size_t pixel;
      uint32_t sample;
      decode_index(index, pixel, sample);
      set_error(DUPLICATE, "pixel " + std::to_string(pixel % width_) + "," +
                               std::to_string(pixel / width_) + ", sample " +
                               std::to_string(sample));
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
        if (has_density(deep_event_type(events[i])))
          bytes += sizeof(KernelDeepDensity);
      if (band.event_bytes > size_t(INT64_MAX) - bytes)
        throw std::runtime_error("Deep event file offset overflow");
      if (band.events.reading)
        band.event_page.first = size_t(-1);
      /* Self-describing stream records allow sequential re-bucketing without
       * an unbounded offset catalogue. Identity is canonical pixel/sample. */
      const SpillRecord header{uint64_t(pixel * samples_ + sample), result};
      /* A free-space failure after a stored write still fails the capture. */
      append(band, band.events, &header, sizeof(header));
      append(band, band.events, events, event_bytes);
      for (unsigned i = 0; i < result.count; ++i)
        if (has_density(deep_event_type(events[i])))
          append(band, band.events, density + i, sizeof(*density));
      const SpillRecord stored{uint64_t(band.event_bytes + sizeof(header)), result};
      band.event_bytes += bytes;
      row_event_bytes_[pixel / width_] += bytes;
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
          if (has_density(deep_event_type(events[i])))
            density_[index * capacity_ + i] = density[i];
    }
    ++completed_;
  }
  catch (const SpaceError &error) {
    set_error(SPILL_SPACE, error.what());
  }
  catch (const std::exception &error) {
    set_error(IO_ERROR, error.what());
  }
}
namespace {
const char *base_error_message(const int error, const KernelDeepError failure)
{
  switch (error) {
    case 5: /* IO_ERROR */
      return "deep spill I/O failed (disk full, seek, read or write error)";
    case 6: /* SPILL_SPACE */
      return "deep spill disk space is insufficient";
    case 7: /* REDUCTION */
      return "deep online reduction failed";
    case 1:
      return "camera sample outside capture bounds";
    case 2:
      return "camera sample recorded more than once";
    case 3:
      return "camera intersection depth or opacity is invalid";
    case 4:
      switch (failure) {
        case DEEP_ERROR_CAPACITY:
          return "deep traversal limit exceeded";
        case DEEP_ERROR_DIAGNOSTIC_CAPACITY:
          return "deep raw-hit diagnostic capacity exceeded (no diagnostic truncation permitted)";
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
      return "";
  }
}
}  // namespace
void Capture::set_error(const Error error, const std::string &detail) const
{
  if (error_.load() != NONE)
    return;
  const std::lock_guard<std::mutex> lock(error_mutex_);
  if (error_.load() != NONE)
    return;
  /* Compose before publishing; readers see error_ first, then this text. */
  if (!detail.empty())
    error_detail_ = std::string(base_error_message(error, failure_.load())) + ": " + detail;
  error_.store(error);
}
void Capture::fail(const KernelDeepError reason)
{
  KernelDeepError expected = DEEP_ERROR_NONE;
  failure_.compare_exchange_strong(expected,
                                   reason == DEEP_ERROR_NONE ? DEEP_ERROR_STATE : reason);
  set_error(UNSUPPORTED_STATE);
}
void Capture::record(const int x, const int y, const uint32_t sample, const float depth,
                     const int object)
{
  if (max_events_) {
    fail();
    return;
  }
  if (!std::isfinite(depth) || depth < 0) {
    set_error(INVALID_DEPTH);
    return;
  }
  const KernelDeepEvent event{deep_event_pack(DEEP_SURFACE, object), depth, depth, 1, 0};
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
                            const KernelDeepResult &input,
                            const KernelDeepEvent *events,
                            const KernelDeepDensity *density)
{
  KernelDeepResult result = input;
  if (diagnostics())
    diagnostic_hit(x, y, sample, {2, int(result.status), int(result.count), int(result.error),
                                  -1, 0, 0, 0, 0, 0, 0});
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
  if (result.count && !events) {
    fail(DEEP_ERROR_CAPACITY);
    return;
  }
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || sample >= uint32_t(samples_)) {
    set_error(OUT_OF_RANGE);
    return;
  }
  /* Kernels never exceed the capacity: they merge on overflow themselves
   * (kernel/deep/merge.h). A direct caller's excess is validated first, then
   * merged by the same rule, so invalid input is never merged into validity. */
  auto valid = [&](const KernelDeepEvent *events, const KernelDeepDensity *density) {
    for (unsigned i = 0; i < result.count; ++i) {
      const KernelDeepEvent &e = events[i];
      const bool surface = deep_event_type(e) == DEEP_SURFACE;
      const bool cubic = deep_event_type(e) == DEEP_VOLUME_CUBIC;
      if (has_density(deep_event_type(e))) {
        if (!volume_grid_ || !density || (cubic && e.optical_depth != 0))
          return false;
        if (!std::isfinite(density[i].front) || !std::isfinite(density[i].back) ||
            density[i].front <= 0 || density[i].back <= density[i].front ||
            e.front != float(density[i].front) || e.back != float(density[i].back))
          return false;
        for (float coefficient : density[i].optical_depth)
          if (!std::isfinite(coefficient) || coefficient < 0)
            return false;
      }
      if (!std::isfinite(e.front) || e.front <= 0 || !std::isfinite(e.back) ||
          !std::isfinite(e.surface_alpha) || !std::isfinite(e.optical_depth) ||
          (surface ? (e.back != e.front || e.surface_alpha < 0 || e.surface_alpha > 1 ||
                      (e.optical_depth != 0 && e.optical_depth != 1 && e.optical_depth != -1)) :
                     ((!cubic && deep_event_type(e) != DEEP_VOLUME) || !volume_ ||
                      (has_density(deep_event_type(e)) ? e.back < e.front : e.back <= e.front) ||
                      e.optical_depth < 0 || e.surface_alpha != 0)) ||
          (!volume_ && i && e.front < events[i - 1].front) ||
          (!max_events_ && (!surface || e.surface_alpha != 1)))
        return false;
    }
    return true;
  };
  if (!valid(events, density)) {
    set_error(INVALID_DEPTH);
    return;
  }
  std::vector<KernelDeepEvent> merged_events;
  std::vector<KernelDeepDensity> merged_density;
  if (result.count > capacity_) {
    merged_events.assign(events, events + result.count);
    if (density)
      merged_density.assign(density, density + result.count);
    int count = int(result.count);
    while (count > int(capacity_)) {
      int companions = 0;
      const int freed = ccl::deep_merge_events(
          merged_events.data(), density ? merged_density.data() : nullptr, 1, count, false,
          has_density(DEEP_VOLUME), count - int(capacity_), &companions);
      if (freed < 1) {
        fail(DEEP_ERROR_CAPACITY);
        return;
      }
      count -= freed;
    }
    result.count = unsigned(count);
    events = merged_events.data();
    density = density ? merged_density.data() : nullptr;
    if (!valid(events, density)) {
      set_error(INVALID_DEPTH);
      return;
    }
    record_fallbacks(DEEP_FALLBACK_MERGED);
  }
  if (reducer_) {
    const size_t pixel = size_t(y) * width_ + x;
    if (ingest_) {
      /* Raw records (when retained) keep their caller-thread order; only the
       * reduction moves to the ingest workers. */
      if (retain_samples_) {
        {
          const std::lock_guard<std::mutex> lock(band_for_pixel(pixel).mutex);
          store_record(record_index(pixel, sample), result, events, density);
        }
        if (error_.load() != NONE)
          return;
      }
      try {
        ingest_->push(pixel, sample, result, events, volume_grid_ ? density : nullptr);
      }
      catch (const std::exception &error) {
        set_error(REDUCTION, std::string("parallel ingest queue (") +
                                 std::to_string(ingest_->queue_bytes()) + " byte cap, " +
                                 std::to_string(ingest_->chunk_bytes()) + " byte chunks): " +
                                 error.what());
      }
      return;
    }
    record_reduced(pixel, sample, result, events, density);
    return;
  }
  const size_t index = record_index(size_t(y) * width_ + x, sample);
  std::lock_guard<std::mutex> lock(bands_.empty() ? mutex_ : band_for_pixel(size_t(y) * width_ + x).mutex);
  store_record(index, result, events, density);
}
void Capture::record_reduced(const size_t pixel,
                             const uint32_t sample,
                             const KernelDeepResult &result,
                             const KernelDeepEvent *events,
                             const KernelDeepDensity *density)
{
  if (retain_samples_) {
    {
      const std::lock_guard<std::mutex> lock(band_for_pixel(pixel).mutex);
      store_record(record_index(pixel, sample), result, events, density);
    }
    if (error_.load() != NONE)
      return;
  }
  reduce_record(pixel, sample, result, events, density);
}
void Capture::reduce_record(const size_t pixel,
                            const uint32_t sample,
                            const KernelDeepResult &result,
                            const KernelDeepEvent *events,
                            const KernelDeepDensity *density)
{
  const int x = int(pixel % width_), y = int(pixel / width_);
  if (surface_policy_) {
    try {
      /* build_volume_sample's surface events (FLOAT values kept): events
       * behind the nearest opaque surface are dropped. Zero-alpha events are
       * removed and the rest sorted in reconstruct() order by the encoder. */
      float opaque = std::numeric_limits<float>::infinity();
      for (unsigned i = 0; i < result.count; ++i)
        if (deep_event_type(events[i]) == DEEP_SURFACE && events[i].surface_alpha == 1)
          opaque = std::min(opaque, events[i].front);
      std::vector<SurfaceEventRecord> leaf;
      leaf.reserve(result.count);
      for (unsigned i = 0; i < result.count; ++i) {
        const KernelDeepEvent &event = events[i];
        if (event.front > opaque)
          continue;
        const int object = int(deep_event_object(event));
        if (ids() && size_t(object) >= object_manifest.size())
          throw std::runtime_error("Deep event object outside manifest");
        leaf.push_back({event.front, event.surface_alpha, int32_t(object),
                        int8_t(int(event.optical_depth))});
      }
      reducer_->add(pixel, sample, SurfaceAccumulator::encode_sample(std::move(leaf), ids()));
      if (!retain_samples_)
        ++completed_;
    }
    catch (const DuplicateSample &error) {
      set_error(DUPLICATE, std::string("surface accumulator, ") + error.what());
    }
    catch (const SpaceError &error) {
      set_error(SPILL_SPACE, error.what());
    }
    catch (const ReducerIoError &error) {
      set_error(IO_ERROR, error.what());
    }
    catch (const std::exception &error) {
      set_error(REDUCTION, std::string("surface accumulator at pixel ") + std::to_string(x) +
                               "," + std::to_string(y) + ", sample " + std::to_string(sample) +
                               ": " + error.what());
    }
    return;
  }
  try {
    /* The export-time leaf: density allowance halved for the one-ray fit,
     * which runs in strict mode (as reconstruct_volume_pixel does). */
    const auto budget = error_budget(error_setting_);
    VolumeCameraSample leaf = build_volume_sample(sample, result, events, density,
                                                  budget.density / 2, nullptr);
    if (error_setting_ == 0) {
      const auto compact = fit_volume({leaf}, budget.reconstruction, reconstruction_limit(),
                                      budget.density / 2, std::numeric_limits<size_t>::max(),
                                      x, y, nullptr);
      leaf = curve_sample(compact, leaf.camera.id, leaf.camera.weight);
    }
    if (collect_histogram)
      histogram_record(ray_histogram_, leaf.intervals.size() + leaf.camera.events.size());
    reducer_->add(pixel, sample, encode_volume(leaf));
    if (!retain_samples_)
      ++completed_;
  }
  catch (const DuplicateSample &error) {
    set_error(DUPLICATE, error.what());
  }
  catch (const SpaceError &error) {
    set_error(SPILL_SPACE, error.what());
  }
  catch (const ReducerIoError &error) {
    set_error(IO_ERROR, error.what());
  }
  catch (const std::exception &error) {
    set_error(REDUCTION, error.what());
  }
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
  if (!retains_samples())
    throw std::runtime_error(
        surface_policy_ ?
            "Per-sample deep surface records are not retained by the online accumulator; request "
            "diagnostic records before rendering or set CYCLES_DEEP_SURFACE_REDUCE=0" :
            "Per-sample deep volume records are not retained by the online reducer; request "
            "diagnostic records before rendering or set CYCLES_DEEP_VOLUME_REDUCE=0");
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
  return build_volume_sample(uint64_t(sample), result, events.data(),
                             volume_grid_ ? density.data() : nullptr, density_tolerance,
                             &export_statistics);
}
VolumeCameraSample Capture::build_volume_sample(const uint64_t sample,
                                                const KernelDeepResult &result,
                                                const KernelDeepEvent *record,
                                                const KernelDeepDensity *density,
                                                const double density_tolerance,
                                                ExportStatistics *statistics) const
{
  VolumeCameraSample output{{sample, 1, true, {}}, {}};
  double opaque_depth = std::numeric_limits<double>::infinity();
  for (unsigned i = 0; i < result.count; ++i)
    if (deep_event_type(record[i]) == DEEP_SURFACE && record[i].surface_alpha == 1)
      opaque_depth = std::min(opaque_depth, double(record[i].front));
  std::vector<CubicDensityInterval> cubic;
  std::map<int, std::vector<CubicDensityInterval>> object_cubic;
  for (unsigned i = 0; i < result.count; ++i) {
    const auto &event = record[i];
    /* A fully opaque surface makes all deeper extinction invisible to every
     * depth query. This is exact occlusion, with no opacity threshold. */
    const double front = has_density(deep_event_type(event)) ? density[i].front : event.front;
    if (front > opaque_depth ||
        (deep_event_type(event) != DEEP_SURFACE && front == opaque_depth))
      continue;
    const int object = int(deep_event_object(event));
    if (ids() && size_t(object) >= object_manifest.size())
      throw std::runtime_error("Deep event object outside manifest");
    if (deep_event_type(event) == DEEP_SURFACE)
      output.camera.events.push_back({event.front, event.surface_alpha, object, int(event.optical_depth)});
    else if (deep_event_type(event) == DEEP_VOLUME_CUBIC) {
      const auto &b = density[i].optical_depth;
      (ids() ? object_cubic[object] : cubic).push_back(
          {density[i].front, density[i].back, {b[0], b[1], b[2], b[3]}});
    }
    else if (has_density(deep_event_type(event)))
      output.intervals.push_back({density[i].front, density[i].back,
          double(density[i].optical_depth[0]) + double(density[i].optical_depth[1]), object});
    else
      output.intervals.push_back({event.front, event.back, event.optical_depth, object});
  }
  for (const auto &entry : object_cubic) {
    ExportTimer timer{statistics, ExportStatistics::DensityFit};
    const auto fitted = integrate_cubic_density(entry.second,
        density_tolerance / object_cubic.size(), reconstruction_limit());
    if (fitted.size() > reconstruction_limit() - output.intervals.size())
      throw std::runtime_error("Volume integration interval budget exceeded");
    for (const auto &v : fitted)
      output.intervals.push_back({v.front, v.back, v.optical_depth, entry.first});
  }
  if (!cubic.empty()) {
    ExportTimer timer{statistics, ExportStatistics::DensityFit};
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
  drain_ingest();
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
  if (reducer_) {
    /* Accepted counts plus exact node coverage of [0, population) prove the
     * samples are complete with none outside the population. */
    for (size_t pixel = 0; pixel < size_t(width_) * height_; ++pixel)
      if (!reducer_->complete(pixel, adaptive() ? populations_[pixel] : uint32_t(samples_)))
        return false;
    if (!retain_samples_)
      return true;
  }
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
      if (band.events.file) {
        flush_stream(band, band.events);
        /* Export needs no compressor until a re-bucket; return capture state. */
        std::vector<unsigned char>().swap(band.events.pending);
#ifdef WITH_CYCLES_DEEP_ZSTD
        ZSTD_freeCCtx(static_cast<ZSTD_CCtx *>(band.compressor));
        band.compressor = nullptr;
#endif
      }
      if (std::fflush(band.index))
        io_failure("Deep spill index flush", band.index_path, 0, errno);
      if (band.events.file && !band.events.reading && std::fflush(band.events.file))
        io_failure("Deep spill event flush", band.events.path, 0, errno);
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
  catch (const std::exception &error) {
    set_error(IO_ERROR, error.what());
    return false;
  }
  return true;
}

void Capture::load_stream(Band &band, SpillStream &stream,
                           const int first_y, const int rows) const
{
  const size_t bytes = size_t(stream.raw_bytes);
  const size_t records = size_t(width_) * rows * samples_;
  const auto budget_failure = [&](const char *what) {
    const uint64_t needed = uint64_t(bytes) + uint64_t(records) * sizeof(SpillRecord);
    const uint64_t scratch = stream.raw ? 0 : export_decode_bytes(bytes);
    return std::runtime_error(
        std::string(what) + ": " + std::to_string(rows) + " row(s) from y=" +
        std::to_string(first_y) + " need " + std::to_string(needed + scratch) + " bytes (events " +
        std::to_string(bytes) + " + records " + std::to_string(records * sizeof(SpillRecord)) +
        " + decode scratch " + std::to_string(scratch) + "), staging limit " +
        std::to_string(spill_memory_bytes_) +
        " bytes (deep_memory_mb/8 after adaptive populations); minimum deep_memory_mb " +
        std::to_string((8 * (needed + scratch) + population_bytes_ + reserved_bytes_ +
                        mebibyte - 1) /
                       mebibyte));
  };
  const uint64_t decode = stream.raw ? 0 : export_decode_bytes(bytes);
  if (bytes > spill_memory_bytes_ || decode > spill_memory_bytes_ - bytes ||
      records > (spill_memory_bytes_ - bytes - decode) / sizeof(SpillRecord))
    throw budget_failure("Deep export row exceeds --deep-memory-mb budget");
  staged_records_ = std::vector<SpillRecord>(records);
  staged_events_ = std::vector<unsigned char>(bytes);
  if (staged_events_.capacity() + staged_records_.capacity() * sizeof(SpillRecord) + decode >
      spill_memory_bytes_)
    throw budget_failure("Deep staged vector capacities exceed memory budget");
  if (compression_ && !stream.raw) {
#ifdef WITH_CYCLES_DEEP_ZSTD
    /* Read batches of stored blocks sequentially and decode each batch in
     * parallel straight into the staged stream. Fixed scratch: one batch of
     * stored blocks plus one decoder context per thread. */
    flush_stream(band, stream);
    seek_record(stream.file, 0);
    stream.reading = true;
    band.event_page.first = size_t(-1);
    constexpr size_t batch_blocks = decode_batch_blocks;
    struct Job {
      size_t stored_offset, stored, raw_offset, raw;
    };
    std::vector<Job> jobs;
    jobs.reserve(batch_blocks);
    /* Stored blocks of one batch never exceed the stored stream. */
    std::vector<unsigned char> batch(size_t(std::min<uint64_t>(batch_blocks * event_page_bytes,
                                                               stream.file_bytes)));
    BatchPool pool(std::max(1u, std::min(unsigned(decode_threads), std::thread::hardware_concurrency())));
    uint64_t file_offset = 0;
    size_t raw_offset = 0;
    while (raw_offset < bytes) {
      jobs.clear();
      size_t stored_offset = 0;
      while (jobs.size() < batch_blocks && raw_offset < bytes) {
        uint32_t header[2];
        if (stream.file_bytes - file_offset < sizeof(header) ||
            std::fread(header, 1, sizeof(header), stream.file) != sizeof(header))
          io_failure("Deep spill block header read", stream.path, sizeof(header), errno);
        const size_t raw = header[0], stored = header[1];
        if (raw == 0 || raw > event_page_bytes || stored > raw || raw > bytes - raw_offset ||
            stored > stream.file_bytes - file_offset - sizeof(header) ||
            stored > batch.size() - stored_offset)
          throw std::runtime_error("Invalid deep spill block in " + stream.path + " at offset " +
                                   std::to_string(file_offset) + " (raw " + std::to_string(raw) +
                                   ", stored " + std::to_string(stored) + " bytes)");
        if (std::fread(batch.data() + stored_offset, 1, stored, stream.file) != stored)
          io_failure("Deep spill block read", stream.path, stored, errno);
        jobs.push_back({stored_offset, stored, raw_offset, raw});
        stored_offset += stored;
        raw_offset += raw;
        file_offset += sizeof(header) + stored;
        band.read_bytes += sizeof(header) + stored;
      }
      pool.run(jobs.size(), [&](const size_t i) {
        const Job &job = jobs[i];
        unsigned char *destination = staged_events_.data() + job.raw_offset;
        if (job.stored == job.raw) {
          std::memcpy(destination, batch.data() + job.stored_offset, job.raw);
          return;
        }
        thread_local std::unique_ptr<ZSTD_DCtx, size_t (*)(ZSTD_DCtx *)> context(nullptr,
                                                                                 ZSTD_freeDCtx);
        if (!context)
          context.reset(ZSTD_createDCtx());
        const size_t decoded = context ? ZSTD_decompressDCtx(context.get(), destination, job.raw,
                                                             batch.data() + job.stored_offset,
                                                             job.stored) :
                                         0;
        if (!context || ZSTD_isError(decoded) || decoded != job.raw)
          throw std::runtime_error("Corrupt deep spill block in " + stream.path);
      });
    }
    if (file_offset != stream.file_bytes)
      throw std::runtime_error("Deep spill stream has trailing blocks: " + stream.path);
#endif
  }
  else {
    seek_record(stream.file, 0);
    stream.reading = true;
    if (bytes && std::fread(staged_events_.data(), 1, bytes, stream.file) != bytes)
      io_failure("Deep sequential band read", stream.path, bytes, errno);
    band.read_bytes += bytes;
  }
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
      if (has_density(deep_event_type(event)))
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
    throw std::runtime_error("Deep re-bucket scratch exceeds memory budget: " +
                             std::to_string(capacity_ * (sizeof(KernelDeepEvent) +
                                                         sizeof(KernelDeepDensity))) +
                             " bytes, staging limit " + std::to_string(spill_memory_bytes_));
  /* Row files are transient (one band at a time) and stay raw: recompressing
   * them would put a second encode on the serial export path. */
  for (int y = 0; y < band.rows; ++y) {
    band.row_files.push_back(std::make_unique<SpillStream>());
    bool unused = false;
    band.row_files.back()->raw = true;
    band.row_files.back()->file = open_spill_file("row", 0, unused, band.row_files.back()->path);
  }
  /* Sequential source reader: raw records, or decoded blocks in the event page. */
  SpillStream &source = band.events;
  band.event_page.first = size_t(-1);
  uint64_t block_offset = 0;
  size_t block_position = 0, block_size = 0;
  if (!compression_) {
    seek_record(source.file, 0);
    source.reading = true;
  }
  const auto read = [&](void *destination, const size_t bytes, const char *what) {
    if (!compression_) {
      if (bytes && std::fread(destination, 1, bytes, source.file) != bytes)
        io_failure(what, source.path, bytes, errno);
      return;
    }
    unsigned char *output = static_cast<unsigned char *>(destination);
    size_t remaining = bytes;
    while (remaining) {
      if (block_position == block_size) {
        block_offset = read_block(band, source, block_offset, band.event_page.data.data(),
                                  block_size);
        block_position = 0;
      }
      const size_t n = std::min(remaining, block_size - block_position);
      std::memcpy(output, band.event_page.data.data() + block_position, n);
      output += n;
      block_position += n;
      remaining -= n;
    }
  };
  size_t remaining = band.event_bytes;
  while (remaining) {
    SpillRecord header;
    if (remaining < sizeof(header))
      throw std::runtime_error("Deep re-bucket header read failed");
    read(&header, sizeof(header), "Deep re-bucket header read");
    remaining -= sizeof(header);
    const size_t row = size_t(header.event_offset / samples_ / width_);
    if (row < size_t(band.first_y) || row >= size_t(band.first_y + band.rows) ||
        header.result.count > capacity_ || header.result.status != DEEP_COMPLETE ||
        header.result.error != DEEP_ERROR_NONE)
      throw std::runtime_error("Invalid deep re-bucket record");
    const size_t event_bytes = header.result.count * sizeof(KernelDeepEvent);
    if (events.size() < header.result.count)
      events.resize(header.result.count);
    if (event_bytes > remaining)
      throw std::runtime_error("Deep re-bucket event read failed");
    read(events.data(), event_bytes, "Deep re-bucket event read");
    remaining -= event_bytes;
    size_t densities = 0;
    for (unsigned i = 0; i < header.result.count; ++i)
      densities += has_density(deep_event_type(events[i]));
    const size_t density_bytes = densities * sizeof(KernelDeepDensity);
    if (density.size() < densities)
      density.resize(densities);
    if (density_bytes > remaining)
      throw std::runtime_error("Deep re-bucket density read failed");
    read(density.data(), density_bytes, "Deep re-bucket density read");
    remaining -= density_bytes;
    SpillStream &row_file = *band.row_files[row - band.first_y];
    append(band, row_file, &header, sizeof(header));
    append(band, row_file, events.data(), event_bytes);
    append(band, row_file, density.data(), density_bytes);
    if (!compression_)
      band.read_bytes += sizeof(header) + event_bytes + density_bytes;
  }
  for (const auto &row : band.row_files) {
    flush_stream(band, *row);
    if (std::fflush(row->file))
      io_failure("Deep row file flush", row->path, 0, errno);
  }
  band.event_page.first = size_t(-1);
  std::fclose(band.index);
  band.index = nullptr;
  band.events.close();
  band.rebucketed = true;
}

void Capture::begin_export_row(const int y) const
{
  ExportTimer timer{&export_statistics, ExportStatistics::Staging};
  if (bands_.empty())
    return;
  if (reducer_ && !retain_samples_) {
    if (!exporting_) {
      if (!finalize())
        throw std::runtime_error("Cannot export incomplete deep capture");
      exporting_ = true;
      next_export_y_ = height_ - 1;
    }
    /* A same-capture validation companion exports the frame again; the
     * reducer keeps its nodes for it (end_export_row skips release). */
    if (retain_for_validation && next_export_y_ == -1 && !export_row_open_)
      next_export_y_ = height_ - 1;
    if (export_row_open_ || y != next_export_y_)
      throw std::runtime_error("Deep export rows must be sequential Y-down");
  }
  if (reducer_) {
    /* Export reads only each pixel's surviving nodes: about width x L curves. */
    const size_t first = size_t(y) * width_;
    const uint64_t bytes = reducer_->staging_bytes(first, width_);
    if (bytes > reducer_->ram_limit()) {
      const uint64_t share = retain_samples_ ? 2 : 1;
      throw std::runtime_error(
          std::string(surface_policy_ ? "Deep surface" : "Deep volume") +
          " export row nodes exceed --deep-memory-mb budget: row y=" +
          std::to_string(y) + " needs " + std::to_string(bytes) + " bytes, reducer share " +
          std::to_string(reducer_->ram_limit()) + " bytes; minimum deep_memory_mb " +
          std::to_string((8 * share * bytes + population_bytes_ + reserved_bytes_ + mebibyte - 1) /
                         mebibyte));
    }
    reducer_->reserve(bytes);
    reducer_->stage(first, width_);
    if (!retain_samples_) {
      export_row_open_ = true;
      return;
    }
  }
  if (!exporting_) {
    if (!finalize())
      throw std::runtime_error("Cannot export incomplete deep capture");
    /* A band is staged whole or, when it exceeds staging, row by row, and a
     * row never needs more than its band, so the largest row decides whether
     * the export fits. Fail before exporting any row, with the minimum budget. */
    const uint64_t row_records = uint64_t(width_) * samples_ * sizeof(SpillRecord);
    const auto largest = std::max_element(row_event_bytes_.begin(), row_event_bytes_.end());
    if (largest != row_event_bytes_.end() && *largest + row_records > spill_memory_bytes_) {
      const uint64_t needed = *largest + row_records;
      throw std::runtime_error(
          "Deep export row exceeds --deep-memory-mb budget: largest row y=" +
          std::to_string(largest - row_event_bytes_.begin()) + " needs " + std::to_string(needed) +
          " bytes (events " + std::to_string(*largest) + " + records " +
          std::to_string(row_records) + "), staging limit " + std::to_string(spill_memory_bytes_) +
          " bytes (deep_memory_mb/8 after adaptive populations and host reservations); minimum "
          "deep_memory_mb " +
          std::to_string((8 * needed + population_bytes_ + reserved_bytes_ + mebibyte - 1) / mebibyte));
    }
    exporting_ = true;
    next_export_y_ = height_ - 1;
  }
  if (retain_for_validation && next_export_y_ == -1 && !export_row_open_)
    next_export_y_ = height_ - 1;
  if (export_row_open_ || y != next_export_y_)
    throw std::runtime_error("Deep export rows must be sequential Y-down");
  Band &band = band_for_pixel(size_t(y) * width_);
  if (staged_first_y_ < 0) {
    const size_t index_bytes = band.records * sizeof(SpillRecord);
    const uint64_t decode = export_decode_bytes(band.event_bytes);
    if (!band.rebucketed && (band.event_bytes > spill_memory_bytes_ ||
        decode > spill_memory_bytes_ - band.event_bytes ||
        index_bytes > spill_memory_bytes_ - band.event_bytes - decode))
      rebucket(band);
    if (band.rebucketed)
      load_stream(band, *band.row_files[y - band.first_y], y, 1);
    else
      load_stream(band, band.events, band.first_y, band.rows);
  }
  export_row_open_ = true;
}
void Capture::end_export_row(const int y) const
{
  if (bands_.empty())
    return;
  if (!export_row_open_ || y != next_export_y_)
    throw std::runtime_error("Deep export row completion mismatch");
  if (reducer_ && !retain_for_validation)
    reducer_->release(size_t(y) * width_, width_);
  if (reducer_ && !retain_samples_) {
    export_row_open_ = false;
    --next_export_y_;
    return;
  }
  Band &band = band_for_pixel(size_t(y) * width_);
  if (band.rebucketed || y == band.first_y) {
    std::vector<SpillRecord>().swap(staged_records_);
    std::vector<unsigned char>().swap(staged_events_);
    staged_first_y_ = -1;
    staged_rows_ = 0;
    /* Validation companions reuse disk records, never retain frame RAM.
     * FILE owners still close/remove their temporary files at destruction. */
    if (!retain_for_validation) {
      if (band.rebucketed)
        band.row_files[y - band.first_y].reset();
      else {
        std::fclose(band.index);
        band.index = nullptr;
        band.events.close();
      }
#ifdef WITH_CYCLES_DEEP_ZSTD
      if (!band.rebucketed || y == band.first_y) {
        ZSTD_freeDCtx(static_cast<ZSTD_DCtx *>(band.decompressor));
        band.decompressor = nullptr;
        std::vector<unsigned char>().swap(band.block);
      }
#endif
    }
  }
  export_row_open_ = false;
  --next_export_y_;
}
const char *Capture::error_message() const
{
  const int error = error_.load();
  if (error != NONE) {
    if (!error_detail_.empty())
      return error_detail_.c_str();
    return base_error_message(error, failure_.load());
  }
  if (finalize())
    return "";
  if (!reducer_ || retain_samples_)
    return "camera capture is incomplete";
  /* Online reducers keep no per-sample index: name the first incomplete pixel. */
  size_t expected = count_;
  if (adaptive()) {
    expected = 0;
    for (const uint32_t n : populations_)
      expected += n;
  }
  std::string detail = "camera capture is incomplete: accepted " +
                       std::to_string(completed_.load()) + " of " + std::to_string(expected) +
                       " camera samples";
  for (size_t pixel = 0; pixel < size_t(width_) * height_; ++pixel) {
    const uint32_t population = adaptive() ? populations_[pixel] : uint32_t(samples_);
    if (!reducer_->complete(pixel, population)) {
      detail += "; first incomplete pixel " + std::to_string(pixel % width_) + "," +
                std::to_string(pixel / width_) + " accepted " +
                std::to_string(reducer_->accepted(pixel)) + " of population " +
                std::to_string(population);
      break;
    }
  }
  const std::lock_guard<std::mutex> lock(error_mutex_);
  incomplete_detail_ = detail;
  return incomplete_detail_.c_str();
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
  if (surface_policy_)
    return reconstruct_surface_pixel(x, y);
  PixelLedger ledger{x, y, {}};
  for (int sample = 0; sample < population(x, y); ++sample)
    ledger.samples.push_back({uint64_t(sample), 1, true, events(x, y, sample)});
  return reconstruct(ledger, ids());
}
std::vector<IntervalSample> Capture::fit_volume(std::vector<VolumeCameraSample> samples,
                                               const double tolerance,
                                               const size_t limit,
                                               const double reduction,
                                               const size_t bytes,
                                               const int x,
                                               const int y,
                                               ExportStatistics *statistics) const
{
  ExportTimer timer{statistics, ExportStatistics::MixtureFit};
  bool merged = false;
  auto record_merge = [&] {
    if (!merged)
      return;
    /* Rare; one pixel may be fitted several times (tree levels, reducer). */
    std::lock_guard<std::mutex> lock(budget_merged_mutex_);
    budget_merged_pixels_.insert(uint64_t(y) * uint64_t(width_) + uint64_t(x));
  };
  if (!ids()) {
    auto curve = reconstruct_volume(std::move(samples), tolerance, limit, reduction, bytes,
                                    &merged);
    record_merge();
    return curve;
  }
  try {
    auto curve = reconstruct_volume_ids(std::move(samples), tolerance + reduction, limit, bytes,
                                        &merged);
    record_merge();
    return curve;
  }
  catch (const std::runtime_error &error) {
    throw std::runtime_error(std::string(error.what()) + ", pixel=" +
                             std::to_string(x) + "," + std::to_string(y));
  }
}
void Capture::histogram_record(Histogram &histogram, const size_t n)
{
  int bucket = 0;
  for (size_t v = n; v && bucket < histogram_buckets - 1; v >>= 1)
    ++bucket;
  histogram.count[bucket].fetch_add(1, std::memory_order_relaxed);
  histogram.total.fetch_add(n, std::memory_order_relaxed);
  histogram.items.fetch_add(1, std::memory_order_relaxed);
  uint64_t maximum = histogram.maximum.load();
  while (maximum < n && !histogram.maximum.compare_exchange_weak(maximum, uint64_t(n)))
    ;
}
/* One balanced-tree combine, as in the export-time tree: the left operand
 * covers the lower samples and keeps its id; allowance R/(2 levels). */
VolumeCameraSample Capture::combine_volume(VolumeCameraSample left,
                                           VolumeCameraSample right,
                                           const size_t levels,
                                           const int x,
                                           const int y,
                                           ExportStatistics *statistics) const
{
  const auto budget = error_budget(error_setting_);
  const uint64_t id = left.camera.id;
  const double weight = left.camera.weight + right.camera.weight;
  std::vector<VolumeCameraSample> pair;
  pair.push_back(std::move(left));
  pair.push_back(std::move(right));
  const auto curve = fit_volume(std::move(pair), budget.reconstruction / (2 * levels),
                                reconstruction_limit(), budget.reconstruction / (2 * levels),
                                std::numeric_limits<size_t>::max(), x, y, statistics);
  if (collect_histogram) {
    int level = 0;
    while (level < histogram_levels - 1 && double(uint64_t(1) << (level + 1)) < weight)
      ++level;
    histogram_record(level_histogram_[level], curve.size());
  }
  return curve_sample(curve, id, weight);
}
uint64_t Capture::export_decode_bytes(const uint64_t raw_bytes) const
{
#ifdef WITH_CYCLES_DEEP_ZSTD
  if (!compression_ || !raw_bytes)
    return 0;
  return std::min<uint64_t>(raw_bytes, decode_batch_blocks * event_page_bytes) +
         uint64_t(decode_threads) * ZSTD_estimateDCtxSize();
#else
  (void)raw_bytes;
  return 0;
#endif
}
double Capture::surface_merge_budget() const
{
  /* The exact writer spends at most F = 1e-6 (FLOAT export and its z
   * tolerance rounding) against the exported curve, so merges may use E - F.
   * Compact and --deep-reduce writers spend up to E themselves. */
  if (compact || export_reduce || error_setting_ == 0)
    return 0;
  return std::max(0.0, error_budget(error_setting_).effective - 1e-6);
}
SurfaceAccumulatorSettings Capture::surface_settings() const
{
  SurfaceAccumulatorSettings settings;
  settings.ids = ids_;
  settings.capacity = surface_capacity_;
  settings.budget = surface_merge_budget();
  settings.z_tolerance = error_setting_ ? double(z_tolerance) : 0.0;
  return settings;
}
std::vector<SurfaceSample> Capture::reconstruct_surface_pixel(const int x, const int y) const
{
  ExportTimer pixel_timer{&export_statistics, ExportStatistics::Pixel};
  const size_t pixel = size_t(y) * width_ + x;
  const uint32_t cameras = uint32_t(population(x, y));
  if (!reducer_->complete(pixel, cameras))
    throw std::runtime_error("Deep surface accumulator is incomplete at pixel " +
                             std::to_string(x) + "," + std::to_string(y) + ": accepted " +
                             std::to_string(reducer_->accepted(pixel)) + " of population " +
                             std::to_string(cameras) +
                             " camera samples (nodes must cover samples 0.." +
                             std::to_string(cameras ? cameras - 1 : 0) + " exactly)");
  thread_local std::vector<PixelReducer::ExportNode> nodes;
  thread_local std::vector<std::vector<unsigned char>> scratch;
  {
    ExportTimer timer{&export_statistics, ExportStatistics::Read};
    reducer_->export_nodes(pixel, nodes, scratch);
  }
  std::vector<std::pair<const unsigned char *, size_t>> payloads;
  payloads.reserve(nodes.size());
  for (const auto &node : nodes)
    payloads.emplace_back(node.payload, node.bytes);
  try {
    return SurfaceAccumulator::finish(payloads, cameras, surface_settings(), &surface_statistics_)
        .samples;
  }
  catch (const std::invalid_argument &) {
    throw; /* reconstruct()'s own input errors, unchanged. */
  }
  catch (const std::exception &error) {
    throw std::runtime_error(std::string(error.what()) + ", pixel " + std::to_string(x) + "," +
                             std::to_string(y));
  }
}
std::vector<IntervalSample> Capture::reconstruct_reduced_pixel(const int x, const int y) const
{
  ExportTimer pixel_timer{&export_statistics, ExportStatistics::Pixel};
  const size_t pixel = size_t(y) * width_ + x;
  const uint32_t cameras = uint32_t(population(x, y));
  if (!reducer_->complete(pixel, cameras))
    throw std::runtime_error("Cannot read incomplete deep capture");
  const auto budget = error_budget(error_setting_);
  thread_local std::vector<PixelReducer::ExportNode> nodes;
  thread_local std::vector<std::vector<unsigned char>> scratch;
  {
    ExportTimer timer{&export_statistics, ExportStatistics::Read};
    reducer_->export_nodes(pixel, nodes, scratch);
  }
  /* Same retained-curve memory rule as the export-time tree. */
  size_t retained = 0;
  for (const auto &node : nodes) {
    uint32_t counts[2];
    std::memcpy(counts, node.payload, 8);
    retained += (size_t(counts[0]) + counts[1]) * 128;
  }
  if (volume_pixel_bytes_ && retained > volume_pixel_bytes_)
    throw std::runtime_error(
        "Deep volume pixel reconstruction exceeds --deep-memory-mb budget: retained " +
        std::to_string(retained) + ", budget " + std::to_string(volume_pixel_bytes_) +
        ", pixel " + std::to_string(x) + "," + std::to_string(y));
  /* Fold the remaining (decreasing) aligned groups from the right, then the
   * final fit, exactly as the export-time tree does after its last camera. */
  const auto decode = [&](const PixelReducer::ExportNode &node) {
    return decode_volume(node.payload, node.bytes, node.key.first(),
                         double(uint64_t(1) << node.key.level));
  };
  const double step = reducer_->step_error();
  VolumeCameraSample sample = decode(nodes.back());
  double ledger = nodes.back().error;
  for (size_t i = nodes.size() - 1; i-- > 0;) {
    sample = combine_volume(decode(nodes[i]), std::move(sample), reduction_levels_, x, y,
                            &export_statistics);
    ledger = std::max(ledger, nodes[i].error) + step;
  }
  /* Ledger: deepest leaf-to-root path of spent per-level allowances. */
  const double allowance = budget.reconstruction / 2;
  if (ledger > allowance * (1 + 1e-9)) {
    std::ostringstream message;
    message << std::setprecision(6) << "Deep volume reduction ledger exceeds its allowance: "
            << ledger << " > " << allowance << " (" << reduction_levels_ << " levels of " << step
            << "), pixel " << x << "," << y;
    throw std::runtime_error(message.str());
  }
  std::vector<VolumeCameraSample> ledger_samples;
  ledger_samples.push_back(std::move(sample));
  return fit_volume(std::move(ledger_samples), budget.reconstruction / 2, reconstruction_limit(),
                    0, volume_pixel_bytes_ ? volume_pixel_bytes_ : std::numeric_limits<size_t>::max(),
                    x, y, &export_statistics);
}
std::vector<IntervalSample> Capture::reconstruct_volume_pixel(const int x, const int y) const
{
  if (reducer_ && volume_ && error_.load() == NONE)
    return reconstruct_reduced_pixel(x, y);
  ExportTimer pixel_timer{&export_statistics, ExportStatistics::Pixel};
  auto fit = [&](std::vector<VolumeCameraSample> samples, double tolerance, size_t limit,
                 double reduction, size_t bytes = std::numeric_limits<size_t>::max()) {
    return fit_volume(std::move(samples), tolerance, limit, reduction, bytes, x, y,
                      &export_statistics);
  };
  if (!volume_ || error_.load() != NONE)
    throw std::runtime_error("Invalid volume capture reconstruction");
  const auto budget = error_budget(error_setting_);
  std::vector<VolumeCameraSample> ledger;
  const int cameras = population(x, y);
  const bool compact_ray = volume_grid_ && samples_ > 8;
  /* Per-level allowance from the sample cap (equal to the population without
   * adaptive sampling), as the online reducer must choose it before the
   * population is known; raw and reduced captures then agree exactly. */
  size_t levels = 1;
  for (size_t n = compact_ray ? size_t(samples_) : size_t(cameras); n > 2; n = (n + 1) / 2)
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
        sample.camera.events.push_back({span.front, span.alpha, span.object, span.facing});
      else
        sample.intervals.push_back({span.front, span.back, -std::log1p(-span.alpha), span.object});
    return sample;
  };
  auto bytes = [](const VolumeCameraSample &sample) {
    return (sample.intervals.capacity() + sample.camera.events.capacity()) * 128;
  };
  /* Diagnostic interval counts (log only): per ray and per tree node level. */
  struct LocalHistogram {
    uint64_t count[histogram_buckets] = {}, total = 0, items = 0, maximum = 0;
    void add(const size_t n)
    {
      int bucket = 0;
      for (size_t v = n; v && bucket < histogram_buckets - 1; v >>= 1)
        ++bucket;
      ++count[bucket];
      total += n;
      ++items;
      maximum = std::max(maximum, uint64_t(n));
    }
  };
  std::unique_ptr<std::array<LocalHistogram, histogram_levels + 1>> histogram;
  if (collect_histogram)
    histogram = std::make_unique<std::array<LocalHistogram, histogram_levels + 1>>();
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
    if (histogram) {
      /* Level = ceil(log2(population)); balanced nodes have exact powers of two. */
      int level = 0;
      while (level < histogram_levels - 1 && double(uint64_t(1) << (level + 1)) < weight)
        ++level;
      (*histogram)[1 + level].add(curve.size());
    }
    return from_curve(curve, id, weight);
  };
  size_t retained_bytes = 0;
  for (int i = 0; i < cameras; ++i) {
    auto sample = volume_sample(
        x, y, i, (compact_ray || (volume_grid_ && error_setting_ > 0)) ? budget.density / 2 : budget.density);
    if (compact_ray && error_setting_ == 0) {
      /* Split the existing density allowance between integration and one-ray
       * reduction. Convex averaging preserves this per-ray absolute T bound. */
      const auto compact = fit(
          {sample}, budget.reconstruction, reconstruction_limit(), budget.density / 2);
      sample = from_curve(compact, sample.camera.id, sample.camera.weight);
    }
    if (histogram)
      (*histogram)[0].add(sample.intervals.size() + sample.camera.events.size());
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
  if (histogram) {
    for (int i = 0; i <= histogram_levels; ++i) {
      const LocalHistogram &local = (*histogram)[i];
      if (!local.items)
        continue;
      Histogram &global = i ? level_histogram_[i - 1] : ray_histogram_;
      for (int b = 0; b < histogram_buckets; ++b)
        if (local.count[b])
          global.count[b].fetch_add(local.count[b], std::memory_order_relaxed);
      global.total.fetch_add(local.total, std::memory_order_relaxed);
      global.items.fetch_add(local.items, std::memory_order_relaxed);
      uint64_t maximum = global.maximum.load();
      while (maximum < local.maximum && !global.maximum.compare_exchange_weak(maximum, local.maximum))
        ;
    }
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

std::string Capture::check_spill_space()
{
  if (bands_.empty() || failed())
    return std::string();
  if (reducer_ && !retain_samples_) {
    /* Node storage: live curves grow with tree level, not with samples.
     * Project live node bytes by (final levels + 1) / (current mean level + 1),
     * double it for dead records awaiting compaction, add 5% and the reserve. */
    const auto nodes = reducer_->statistics();
    std::error_code ec;
    const auto info = std::filesystem::space(native_path(spill_directory_), ec);
    if (ec)
      return "Deep spill: cannot query free space in " + spill_directory_ + ": " + ec.message();
    const uint64_t free_bytes = uint64_t(info.available);
    const uint64_t stored = nodes.file_bytes;
    const uint64_t quota_free = spill_quota_bytes_ ?
                                    (spill_quota_bytes_ > stored ? spill_quota_bytes_ - stored : 0) :
                                    UINT64_MAX;
    const uint64_t available = std::min(free_bytes, quota_free);
    const std::string advice =
        ". Choose a larger spill directory (Output > Deep > Spill Directory, --deep-spill-dir or "
        "CYCLES_DEEP_SPILL_DIR), or reduce samples, resolution or events";
    if (available < spill_reserve_bytes) {
      set_error(SPILL_SPACE, "spill directory " + spill_directory_ + " has " +
                                 std::to_string(available) + " usable bytes, below the " +
                                 std::to_string(spill_reserve_bytes) + " byte reserve, after " +
                                 std::to_string(stored) + " stored node bytes" + advice);
      return std::string();
    }
    const size_t done = completed_.load();
    if (!done)
      return std::string();
    const uint64_t live = reducer_->live_bytes();
    const double mean = double(done) / (double(width_) * height_);
    /* Volume nodes grow with tree level; surface curves with distinct depths
     * can grow linearly in samples (up to their breakpoint capacity). */
    const double growth = surface_policy_ ?
                              std::max(1.0, double(samples_) / std::max(1.0, mean)) :
                              std::max(1.0, (std::log2(double(samples_)) + 1) /
                                                (std::log2(std::max(1.0, mean)) + 1));
    /* Nodes reach disk only beyond the reducer's RAM share. */
    const uint64_t projected =
        uint64_t(2 * std::max(0.0, double(live) * growth - double(reducer_->ram_limit())));
    const uint64_t remaining = projected > stored ? projected - stored : 0;
    const uint64_t needed = remaining + remaining / 20 + spill_reserve_bytes;
    if (needed <= available)
      return std::string();
    const std::string message =
        "spill directory " + spill_directory_ + " (" + std::to_string(free_bytes) +
        " bytes free" +
        (spill_quota_bytes_ ? ", quota " + std::to_string(spill_quota_bytes_) + " bytes" :
                              std::string()) +
        ") cannot hold the projected " + (surface_policy_ ? "surface" : "volume") +
        " node storage: " + std::to_string(live) +
        " live node bytes after " + std::to_string(done) + " of " + std::to_string(count_) +
        " camera samples project to " + std::to_string(projected) +
        " stored bytes (x" + std::to_string(growth) +
        " level growth, less the RAM share, x2 compaction headroom); still needed " +
        std::to_string(needed) + " bytes (5% margin + " + std::to_string(spill_reserve_bytes) +
        " reserve) versus " + std::to_string(available) + " usable" + advice;
    if (adaptive()) {
      if (projection_warned_)
        return std::string();
      projection_warned_ = true;
      return "Deep spill projection uses the adaptive sample cap as an upper bound: " + message;
    }
    set_error(SPILL_SPACE, message);
    return std::string();
  }
  /* Workers are joined between passes, so band counters are stable. */
  uint64_t stored = 0, largest_raw = 0, largest_staged = 0;
  for (const auto &item : bands_) {
    const Band &band = *item;
    stored += compression_ ? band.events.file_bytes + band.events.pending.size() : band.event_bytes;
    if (band.event_bytes > largest_raw) {
      largest_raw = band.event_bytes;
      largest_staged = band.event_bytes + band.records * stride_;
    }
  }
  std::error_code ec;
  const auto info = std::filesystem::space(native_path(spill_directory_), ec);
  if (ec)
    return "Deep spill: cannot query free space in " + spill_directory_ + ": " + ec.message();
  const uint64_t free_bytes = uint64_t(info.available);
  /* The quota charges the index at its full logical size from the start; free
   * disk is charged only for index pages still to be allocated. */
  const uint64_t quota_free = spill_quota_bytes_ ?
                                  (spill_quota_bytes_ > stored + index_total_bytes_ ?
                                       spill_quota_bytes_ - stored - index_total_bytes_ :
                                       0) :
                                  UINT64_MAX;
  const size_t done = completed_.load();
  const std::string where = spill_directory_ + " (" + std::to_string(free_bytes) + " bytes free" +
                            (spill_quota_bytes_ ? ", CYCLES_DEEP_SPILL_MAX_MB quota " +
                                                      std::to_string(spill_quota_bytes_) + " bytes" :
                                                  std::string()) +
                            ")";
  const std::string advice =
      ". Choose a larger spill directory (Output > Deep > Spill Directory, --deep-spill-dir or "
      "CYCLES_DEEP_SPILL_DIR), or reduce samples, resolution or events";
  const uint64_t available = std::min(free_bytes, quota_free);
  if (available < spill_reserve_bytes) {
    set_error(SPILL_SPACE, "spill directory " + where + " has " + std::to_string(available) +
                               " usable bytes, below the " + std::to_string(spill_reserve_bytes) +
                               " byte reserve, after " + std::to_string(stored) +
                               " stored event bytes and a " + std::to_string(index_total_bytes_) +
                               " byte index" + advice);
    return std::string();
  }
  if (!done)
    return std::string();
  const long double scale = (long double)count_ / done;
  const uint64_t projected = uint64_t(stored * scale);
  const uint64_t events = projected > stored ? projected - stored : 0;
  /* Export re-buckets a band whose stream and index exceed the staging budget,
   * one band at a time, writing a raw second copy of that band. */
  const uint64_t rebucket = uint64_t(largest_staged * scale) > spill_memory_bytes_ ?
                                uint64_t(largest_raw * scale) :
                                0;
  const uint64_t index = index_sparse_ ? index_total_bytes_ : 0;
  const auto required = [](const uint64_t remaining) {
    return remaining + remaining / 20 + spill_reserve_bytes;
  };
  const bool disk_short = required(events + index + rebucket) > free_bytes;
  const bool quota_short = required(events + rebucket) > quota_free;
  if (!disk_short && !quota_short)
    return std::string();
  const uint64_t needed = disk_short ? required(events + index + rebucket) : required(events + rebucket);
  const std::string message =
      "spill directory " + where + " cannot hold the projected spill: " +
      std::to_string(stored) + " stored event bytes after " + std::to_string(done) + " of " +
      std::to_string(count_) + " camera samples project to " + std::to_string(projected) +
      " bytes; still needed " + std::to_string(needed) + " bytes (events " +
      std::to_string(events) + (disk_short ? " + unallocated index " + std::to_string(index) : "") +
      " + export re-bucket copy " + std::to_string(rebucket) + " + 5% margin + " +
      std::to_string(spill_reserve_bytes) + " reserve) versus " +
      std::to_string(disk_short ? free_bytes : quota_free) +
      (disk_short ? " bytes free" : " bytes left in the quota") + advice;
  if (adaptive()) {
    if (projection_warned_)
      return std::string();
    projection_warned_ = true;
    return "Deep spill projection uses the adaptive sample cap as an upper bound: " + message;
  }
  set_error(SPILL_SPACE, message);
  return std::string();
}

std::string Capture::volume_histogram() const
{
  std::ostringstream out;
  const auto describe = [&](const char *label, const Histogram &h) {
    const uint64_t items = h.items.load();
    if (!items)
      return;
    out << label << " n=" << items << " mean=" << std::setprecision(4)
        << double(h.total.load()) / items << " max=" << h.maximum.load();
    /* Percentiles are bucket upper bounds: bucket b holds [2^(b-1), 2^b). */
    const auto percentile = [&](const double q) {
      uint64_t seen = 0;
      for (int b = 0; b < histogram_buckets; ++b) {
        seen += h.count[b].load();
        if (seen >= q * items)
          return b ? (uint64_t(1) << b) - 1 : uint64_t(0);
      }
      return h.maximum.load();
    };
    out << " p50<=" << percentile(.5) << " p90<=" << percentile(.9) << " p99<=" << percentile(.99)
        << " buckets[";
    bool first = true;
    for (int b = 0; b < histogram_buckets; ++b)
      if (const uint64_t n = h.count[b].load()) {
        out << (first ? "" : " ") << (b ? (uint64_t(1) << (b - 1)) : 0) << ":" << n;
        first = false;
      }
    out << "]";
  };
  describe("rays(after per-ray processing)", ray_histogram_);
  for (int level = 0; level < histogram_levels; ++level) {
    if (!level_histogram_[level].items.load())
      continue;
    out << "; ";
    describe(("level" + std::to_string(level + 1)).c_str(), level_histogram_[level]);
  }
  return out.str();
}
}  // namespace ccl::deep
