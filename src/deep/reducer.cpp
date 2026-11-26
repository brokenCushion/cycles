/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/reducer.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>
#ifdef WITH_CYCLES_DEEP_ZSTD
#  include <zstd.h>
#endif

namespace ccl::deep {
namespace {
void seek(FILE *file, const uint64_t offset, const std::string &path)
{
#ifdef _WIN32
  const int result = _fseeki64(file, int64_t(offset), SEEK_SET);
#else
  const int result = fseeko(file, off_t(offset), SEEK_SET);
#endif
  if (result != 0)
    throw ReducerIoError("Deep node stream seek failed (errno " + std::to_string(errno) +
                             "), offset " + std::to_string(offset) + ", file " + path);
}
[[noreturn]] void io_failure(const char *operation, const std::string &path, const size_t bytes)
{
  const int code = errno;
  throw ReducerIoError(std::string(operation) + " failed (errno " + std::to_string(code) +
                           "), " + std::to_string(bytes) + " bytes, file " + path);
}
}  // namespace

PixelReducer::Band::~Band()
{
  if (file)
    std::fclose(file);
#ifdef WITH_CYCLES_DEEP_ZSTD
  ZSTD_freeDCtx(static_cast<ZSTD_DCtx *>(decompressor));
#endif
}

PixelReducer::PixelReducer(const int width,
                           const int height,
                           const int rows_per_band,
                           const uint32_t samples,
                           const size_t ram_limit,
                           const double step_error,
                           const int compression,
                           ReducerIo io,
                           ReducerPolicy &policy)
    : width_(width), height_(height), rows_per_band_(rows_per_band), samples_(samples),
      ram_limit_(ram_limit), step_error_(step_error), compression_(compression),
      io_(std::move(io)), policy_(policy)
{
  if (width <= 0 || height <= 0 || rows_per_band <= 0 || samples == 0 ||
      samples > (uint32_t(1) << 31))
    throw std::invalid_argument("Invalid deep reducer dimensions");
#ifndef WITH_CYCLES_DEEP_ZSTD
  compression_ = 0;
#endif
  const size_t pixels = size_t(width) * size_t(height);
  const uint64_t fixed = uint64_t(pixels) * sizeof(Pixel) + stripe_count * sizeof(std::mutex);
  /* Pixel state is resident; payloads must keep at least as much room. */
  if (fixed > ram_limit / 2)
    throw std::invalid_argument(
        "Deep volume reducer pixel state exceeds its memory share: " + std::to_string(fixed) +
        " bytes for " + std::to_string(pixels) + " pixels, share " + std::to_string(ram_limit) +
        " bytes (deep_memory_mb/8); increase --deep-memory-mb");
  evict_threshold_.store(int64_t(ram_limit));
  pixels_.resize(pixels);
  stripes_ = std::make_unique<std::mutex[]>(stripe_count);
  for (int y = 0; y < height; y += rows_per_band)
    bands_.push_back(std::make_unique<Band>());
  charge(int64_t(fixed));
}

PixelReducer::~PixelReducer()
{
#ifdef WITH_CYCLES_DEEP_ZSTD
  ZSTD_freeCCtx(static_cast<ZSTD_CCtx *>(compressor_));
#endif
}

void PixelReducer::charge(const int64_t bytes)
{
  const int64_t now = ram_.fetch_add(bytes) + bytes;
  uint64_t peak = ram_peak_.load();
  while (now > 0 && uint64_t(now) > peak && !ram_peak_.compare_exchange_weak(peak, uint64_t(now)))
    ;
}

void PixelReducer::insert(Pixel &pixel, Node node)
{
  if (pixel.count == pixel.capacity) {
    if (pixel.capacity == std::numeric_limits<uint16_t>::max())
      throw std::runtime_error("Deep reducer pixel node table is full (65535 nodes)");
    const uint16_t capacity = uint16_t(
        std::min<uint32_t>(std::numeric_limits<uint16_t>::max(),
                           pixel.capacity ? 2u * pixel.capacity : 4u));
    auto nodes = std::make_unique<Node[]>(capacity);
    for (uint16_t i = 0; i < pixel.count; ++i)
      nodes[i] = std::move(pixel.nodes[i]);
    charge(int64_t(capacity - pixel.capacity) * int64_t(sizeof(Node)));
    pixel.nodes = std::move(nodes);
    pixel.capacity = capacity;
  }
  pixel.nodes[pixel.count++] = std::move(node);
}

PixelReducer::Node PixelReducer::take(Pixel &pixel, const uint16_t index)
{
  Node node = std::move(pixel.nodes[index]);
  pixel.nodes[index] = std::move(pixel.nodes[pixel.count - 1]);
  --pixel.count;
  return node;
}

/* Reads an evicted node; the caller holds the pixel's stripe or runs alone. */
void PixelReducer::load(const size_t pixel, Node &node, std::vector<unsigned char> &destination) const
{
  Band &b = band(pixel);
  destination.resize(node.raw);
  const std::lock_guard<std::mutex> lock(b.mutex);
  seek(b.file, node.offset, b.path);
  uint32_t header[2];
  if (std::fread(header, 1, sizeof(header), b.file) != sizeof(header))
    io_failure("Deep node header read", b.path, sizeof(header));
  if (header[0] != node.raw || header[1] != node.stored)
    throw std::runtime_error("Corrupt deep node header in " + b.path + " at offset " +
                             std::to_string(node.offset));
  if (node.stored == node.raw) {
    if (node.raw && std::fread(destination.data(), 1, node.raw, b.file) != node.raw)
      io_failure("Deep node read", b.path, node.raw);
  }
  else {
#ifdef WITH_CYCLES_DEEP_ZSTD
    b.scratch.resize(std::max<size_t>(b.scratch.size(), node.stored));
    if (std::fread(b.scratch.data(), 1, node.stored, b.file) != node.stored)
      io_failure("Deep node read", b.path, node.stored);
    if (!b.decompressor && !(const_cast<Band &>(b).decompressor = ZSTD_createDCtx()))
      throw std::runtime_error("Cannot create deep node decompressor");
    const size_t decoded = ZSTD_decompressDCtx(static_cast<ZSTD_DCtx *>(b.decompressor),
                                               destination.data(), node.raw, b.scratch.data(),
                                               node.stored);
    if (ZSTD_isError(decoded) || decoded != node.raw)
      throw std::runtime_error("Corrupt deep node frame in " + b.path);
#else
    throw std::runtime_error("Compressed deep node stream without zstd support");
#endif
  }
  read_bytes_ += sizeof(header) + node.stored;
}

/* Caller holds the pixel's stripe and the evict mutex (or runs alone). */
void PixelReducer::evict_node(const size_t pixel, Node &node)
{
  Band &b = band(pixel);
  const unsigned char *data = node.ram.get();
  size_t stored = node.raw;
#ifdef WITH_CYCLES_DEEP_ZSTD
  if (compression_ && node.raw >= 64) {
    if (!compressor_ && !(compressor_ = ZSTD_createCCtx()))
      throw std::runtime_error("Cannot create deep node compressor");
    compress_scratch_.resize(std::max(compress_scratch_.size(), ZSTD_compressBound(node.raw)));
    const size_t n = ZSTD_compressCCtx(static_cast<ZSTD_CCtx *>(compressor_),
                                       compress_scratch_.data(), compress_scratch_.size(),
                                       node.ram.get(), node.raw, compression_);
    if (!ZSTD_isError(n) && n < node.raw) {
      stored = n;
      data = compress_scratch_.data();
    }
  }
#endif
  const uint32_t header[2] = {node.raw, uint32_t(stored)};
  {
    const std::lock_guard<std::mutex> lock(b.mutex);
    if (!b.file)
      b.file = io_.open(b.path);
    seek(b.file, b.file_bytes, b.path);
    if (std::fwrite(header, 1, sizeof(header), b.file) != sizeof(header) ||
        (stored && std::fwrite(data, 1, stored, b.file) != stored))
      io_failure("Deep node write", b.path, sizeof(header) + stored);
    node.offset = b.file_bytes;
    node.stored = uint32_t(stored);
    b.file_bytes += sizeof(header) + stored;
    b.live_bytes += sizeof(header) + stored;
  }
  raw_written_ += node.raw;
  stored_written_ += sizeof(header) + stored;
  uint64_t total = 0;
  for (const auto &item : bands_)
    total += item->file_bytes;
  uint64_t peak = file_peak_.load();
  while (total > peak && !file_peak_.compare_exchange_weak(peak, total))
    ;
  node.ram.reset();
  charge(-int64_t(node.raw + node_overhead));
  ++evicted_;
  /* May throw a free-space error after the record is safely written. */
  if (io_.written)
    io_.written(sizeof(header) + stored);
}

void PixelReducer::add(const size_t pixel,
                       const uint32_t sample,
                       std::vector<unsigned char> leaf,
                       const double leaf_error)
{
  if (pixel >= pixels_.size() || sample >= samples_)
    throw std::out_of_range("Deep reducer sample outside capture bounds");
  if (leaf.size() > std::numeric_limits<uint32_t>::max())
    throw std::runtime_error("Deep reducer node payload too large");
  {
    const std::lock_guard<std::mutex> lock(stripe(pixel));
    Pixel &p = pixels_[pixel];
    for (uint16_t i = 0; i < p.count; ++i)
      if (p.nodes[i].key.covers(sample))
        throw DuplicateSample("pixel " + std::to_string(pixel % size_t(width_)) + "," +
                              std::to_string(pixel / size_t(width_)) + ", sample " +
                              std::to_string(sample));
    Node current;
    current.key = {sample, 0};
    current.error = leaf_error;
    current.raw = uint32_t(leaf.size());
    current.ram = std::make_unique<unsigned char[]>(std::max<size_t>(1, leaf.size()));
    if (!leaf.empty())
      std::memcpy(current.ram.get(), leaf.data(), leaf.size());
    charge(int64_t(current.raw + node_overhead));
    std::vector<unsigned char>().swap(leaf);
    std::vector<unsigned char> loaded;
    while (true) {
      const NodeKey sibling{current.key.k ^ 1u, current.key.level};
      uint16_t index = p.count;
      for (uint16_t i = 0; i < p.count; ++i)
        if (p.nodes[i].key.k == sibling.k && p.nodes[i].key.level == sibling.level) {
          index = i;
          break;
        }
      if (index == p.count)
        break;
      Node other = take(p, index);
      const unsigned char *other_data = other.ram.get();
      if (!other_data) {
        load(pixel, other, loaded);
        other_data = loaded.data();
        {
          Band &b = band(pixel);
          const std::lock_guard<std::mutex> lock(b.mutex);
          b.live_bytes -= 8 + other.stored;
        }
        ++reloaded_;
      }
      const bool other_left = other.key.k < current.key.k;
      const NodeKey parent{current.key.k >> 1, current.key.level + 1};
      const auto start = std::chrono::steady_clock::now();
      std::vector<unsigned char> merged =
          other_left ? policy_.combine(pixel, parent, other_data, other.raw, current.ram.get(),
                                       current.raw) :
                       policy_.combine(pixel, parent, current.ram.get(), current.raw,
                                       other_data, other.raw);
      merge_ns_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count());
      ++merges_;
      if (merged.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Deep reducer node payload too large");
      if (other.ram)
        charge(-int64_t(other.raw + node_overhead));
      charge(-int64_t(current.raw + node_overhead));
      Node parent_node;
      parent_node.key = parent;
      /* Ledger: the deeper child's spent allowance plus this level's step. */
      parent_node.error = std::max(current.error, other.error) + step_error_;
      parent_node.raw = uint32_t(merged.size());
      parent_node.ram = std::make_unique<unsigned char[]>(std::max<size_t>(1, merged.size()));
      if (!merged.empty())
        std::memcpy(parent_node.ram.get(), merged.data(), merged.size());
      charge(int64_t(parent_node.raw + node_overhead));
      current = std::move(parent_node);
    }
    insert(p, std::move(current));
    ++p.accepted;
  }
  finish_pixel(pixel);
}

void PixelReducer::finish_pixel(size_t)
{
  if (ram_.load() <= int64_t(ram_limit_))
    return;
  evict();
}

void PixelReducer::evict()
{
  /* Workers skip while another thread evicts, up to a hard cap of 5/4 of the
   * share; beyond it they wait for the running sweep (back-pressure). */
  std::unique_lock<std::mutex> lock(evict_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    if (ram_.load() <= int64_t(ram_limit_ + ram_limit_ / 4))
      return;
    lock.lock();
  }
  if (ram_.load() <= evict_threshold_.load())
    return;
  evict_to(int64_t(ram_limit_ / 2));
}

void PixelReducer::reserve(const uint64_t bytes)
{
  const std::lock_guard<std::mutex> lock(evict_mutex_);
  evict_to(int64_t(ram_limit_) - int64_t(std::min<uint64_t>(bytes, ram_limit_)));
}

/* Caller holds the evict mutex. */
void PixelReducer::evict_to(const int64_t target)
{
  for (size_t visited = 0; visited < pixels_.size() && ram_.load() > target; ++visited) {
    const size_t p = evict_cursor_;
    evict_cursor_ = (evict_cursor_ + 1) % pixels_.size();
    std::unique_lock<std::mutex> pixel_lock(stripe(p), std::try_to_lock);
    if (!pixel_lock.owns_lock())
      continue;
    Pixel &px = pixels_[p];
    for (uint16_t i = 0; i < px.count; ++i)
      if (px.nodes[i].ram)
        evict_node(p, px.nodes[i]);
  }
  /* Busy or resident-only memory cannot go lower: avoid repeated full sweeps
   * until another quarter of the limit has been added (never above the cap). */
  evict_threshold_.store(std::min(int64_t(ram_limit_ + ram_limit_ / 4),
                                  std::max(int64_t(ram_limit_), ram_.load() + int64_t(ram_limit_ / 4))));
}

void PixelReducer::compact_band(const size_t index, const std::function<bool()> &cancelled)
{
  Band &b = *bands_[index];
  const uint64_t dead = b.file_bytes - b.live_bytes;
  if (!b.file || dead == 0 || dead <= std::max<uint64_t>(b.live_bytes, compaction_threshold_))
    return;
  /* Copy live records in pixel order (no recompression); apply new offsets
   * only after the new stream is complete, so cancellation keeps the old one. */
  std::string path;
  FILE *file = io_.open(path);
  std::vector<std::pair<Node *, uint64_t>> moved;
  std::vector<unsigned char> buffer;
  uint64_t offset = 0;
  const size_t first = index * size_t(rows_per_band_) * size_t(width_);
  const size_t last = std::min(pixels_.size(), first + size_t(rows_per_band_) * size_t(width_));
  try {
    for (size_t pixel = first; pixel < last; ++pixel) {
      if (cancelled && (pixel - first) % size_t(width_) == 0 && cancelled()) {
        std::fclose(file);
        return;
      }
      Pixel &px = pixels_[pixel];
      for (uint16_t i = 0; i < px.count; ++i) {
        Node &node = px.nodes[i];
        if (node.ram)
          continue;
        const size_t bytes = 8 + node.stored;
        buffer.resize(bytes);
        seek(b.file, node.offset, b.path);
        if (std::fread(buffer.data(), 1, bytes, b.file) != bytes)
          io_failure("Deep node compaction read", b.path, bytes);
        if (std::fwrite(buffer.data(), 1, bytes, file) != bytes)
          io_failure("Deep node compaction write", path, bytes);
        read_bytes_ += bytes;
        moved.emplace_back(&node, offset);
        offset += bytes;
        if (io_.written)
          io_.written(bytes);
      }
    }
    if (std::fflush(file))
      io_failure("Deep node compaction flush", path, 0);
  }
  catch (...) {
    std::fclose(file);
    throw;
  }
  for (auto &[node, position] : moved)
    node->offset = position;
  std::fclose(b.file);
  b.file = file;
  b.path = path;
  b.file_bytes = offset;
  b.live_bytes = offset;
  ++compactions_;
}

void PixelReducer::compact(const std::function<bool()> &cancelled)
{
  for (size_t i = 0; i < bands_.size(); ++i) {
    if (cancelled && cancelled())
      return;
    compact_band(i, cancelled);
  }
  if (ram_.load() > int64_t(ram_limit_))
    evict();
}

uint32_t PixelReducer::accepted(const size_t pixel) const
{
  const std::lock_guard<std::mutex> lock(stripe(pixel));
  return pixels_.at(pixel).accepted;
}

bool PixelReducer::complete(const size_t pixel, const uint32_t population) const
{
  const std::lock_guard<std::mutex> lock(stripe(pixel));
  const Pixel &p = pixels_.at(pixel);
  if (population == 0 || p.accepted != population)
    return false;
  unsigned expected = 0;
  for (uint32_t v = population; v; v &= v - 1)
    ++expected;
  if (p.count != expected)
    return false;
  /* Binary decomposition: blocks of decreasing size from sample 0. */
  uint64_t start = 0;
  for (int level = 31; level >= 0; --level) {
    if (!(population >> level & 1u))
      continue;
    const NodeKey key{uint32_t(start >> level), uint32_t(level)};
    bool found = false;
    for (uint16_t i = 0; i < p.count && !found; ++i)
      found = p.nodes[i].key.k == key.k && p.nodes[i].key.level == key.level;
    if (!found)
      return false;
    start += uint64_t(1) << level;
  }
  return true;
}

uint64_t PixelReducer::staging_bytes(const size_t first, const size_t count) const
{
  uint64_t bytes = 0;
  for (size_t pixel = first; pixel < first + count; ++pixel) {
    const std::lock_guard<std::mutex> lock(stripe(pixel));
    const Pixel &p = pixels_.at(pixel);
    for (uint16_t i = 0; i < p.count; ++i)
      if (!p.nodes[i].ram)
        bytes += p.nodes[i].raw + node_overhead;
  }
  return bytes;
}

void PixelReducer::stage(const size_t first, const size_t count)
{
  /* Sequential reads in file order within each band. */
  std::vector<std::pair<uint64_t, std::pair<size_t, Node *>>> pending;
  for (size_t pixel = first; pixel < first + count; ++pixel) {
    Pixel &p = pixels_.at(pixel);
    for (uint16_t i = 0; i < p.count; ++i)
      if (!p.nodes[i].ram)
        pending.push_back({p.nodes[i].offset, {pixel, &p.nodes[i]}});
  }
  std::sort(pending.begin(), pending.end(), [&](const auto &a, const auto &b) {
    const size_t ba = a.second.first / size_t(width_) / size_t(rows_per_band_);
    const size_t bb = b.second.first / size_t(width_) / size_t(rows_per_band_);
    return ba != bb ? ba < bb : a.first < b.first;
  });
  std::vector<unsigned char> buffer;
  for (auto &item : pending) {
    Node &node = *item.second.second;
    load(item.second.first, node, buffer);
    node.ram = std::make_unique<unsigned char[]>(std::max<size_t>(1, node.raw));
    if (node.raw)
      std::memcpy(node.ram.get(), buffer.data(), node.raw);
    charge(int64_t(node.raw + node_overhead));
    Band &b = band(item.second.first);
    const std::lock_guard<std::mutex> lock(b.mutex);
    b.live_bytes -= 8 + node.stored;
  }
}

void PixelReducer::release(const size_t first, const size_t count)
{
  for (size_t pixel = first; pixel < first + count; ++pixel) {
    const std::lock_guard<std::mutex> lock(stripe(pixel));
    Pixel &p = pixels_.at(pixel);
    for (uint16_t i = 0; i < p.count; ++i) {
      Node &node = p.nodes[i];
      if (node.ram)
        charge(-int64_t(node.raw + node_overhead));
      else {
        Band &b = band(pixel);
        const std::lock_guard<std::mutex> band_lock(b.mutex);
        b.live_bytes -= 8 + node.stored;
      }
    }
    charge(-int64_t(p.capacity) * int64_t(sizeof(Node)));
    p.nodes.reset();
    p.count = p.capacity = 0;
  }
}

void PixelReducer::export_nodes(const size_t pixel,
                                std::vector<ExportNode> &out,
                                std::vector<std::vector<unsigned char>> &scratch) const
{
  const std::lock_guard<std::mutex> lock(stripe(pixel));
  const Pixel &p = pixels_.at(pixel);
  out.clear();
  if (scratch.size() < p.count)
    scratch.resize(p.count);
  for (uint16_t i = 0; i < p.count; ++i) {
    Node &node = p.nodes[i];
    std::vector<unsigned char> &copy = scratch[i];
    if (node.ram)
      copy.assign(node.ram.get(), node.ram.get() + node.raw);
    else
      load(pixel, node, copy);
    out.push_back({node.key, node.error, nullptr, node.raw});
  }
  for (uint16_t i = 0; i < p.count; ++i)
    out[i].payload = scratch[i].data();
  /* Ascending first sample; disjoint aligned ranges sort uniquely. */
  std::vector<size_t> order(out.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return out[a].key.first() < out[b].key.first(); });
  std::vector<ExportNode> sorted;
  sorted.reserve(out.size());
  for (const size_t i : order)
    sorted.push_back(out[i]);
  out.swap(sorted);
}

uint64_t PixelReducer::live_bytes() const
{
  uint64_t bytes = 0;
  for (size_t pixel = 0; pixel < pixels_.size(); ++pixel) {
    const std::lock_guard<std::mutex> lock(stripe(pixel));
    const Pixel &p = pixels_[pixel];
    for (uint16_t i = 0; i < p.count; ++i)
      bytes += p.nodes[i].raw;
  }
  return bytes;
}

PixelReducer::Statistics PixelReducer::statistics() const
{
  Statistics s;
  s.ram_bytes = uint64_t(std::max<int64_t>(0, ram_.load()));
  s.ram_peak = ram_peak_.load();
  s.ram_limit = ram_limit_;
  for (const auto &item : bands_) {
    const std::lock_guard<std::mutex> lock(item->mutex);
    s.file_bytes += item->file_bytes;
    s.live_file_bytes += item->live_bytes;
  }
  s.raw_written = raw_written_.load();
  s.stored_written = stored_written_.load();
  s.file_read_bytes = read_bytes_.load();
  s.file_peak = std::max<uint64_t>(file_peak_.load(), s.file_bytes);
  s.evicted_nodes = evicted_.load();
  s.reloaded_nodes = reloaded_.load();
  s.compactions = compactions_.load();
  s.merges = merges_.load();
  s.merge_seconds = merge_ns_.load() * 1e-9;
  return s;
}

}  // namespace ccl::deep
