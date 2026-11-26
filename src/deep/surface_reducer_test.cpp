/* SPDX-License-Identifier: Apache-2.0 */
/* Online surface accumulation against the reconstruct() oracle: byte identity
 * on random, coincident-depth, ID and adaptive fixtures in forward, reverse,
 * shuffled, threaded and evicting arrival orders; capacity overflow (ledger
 * and depth-tolerance merges) with measured error <= ledger <= budget and
 * order-independent results; Capture-level identity with the raw spill path,
 * the off switch, strict fallback, duplicate and completion errors. */
#include "deep/capture.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

using namespace ccl::deep;

namespace {
void check_impl(const bool condition, const int line)
{
  if (!condition)
    throw std::runtime_error("surface reducer test failed at line " + std::to_string(line));
}
#define check(condition) check_impl((condition), __LINE__)

void set_env(const char *name, const char *value)
{
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

std::filesystem::path spill_root()
{
  static const std::filesystem::path root = [] {
    auto path = std::filesystem::temp_directory_path() /
                ("cycles-deep-surface-test-" + std::to_string(
#ifdef _WIN32
                                                   _getpid()
#else
                                                   getpid()
#endif
                                                       ));
    std::filesystem::create_directories(path);
    return path;
  }();
  return root;
}

using Events = std::vector<SurfaceEventRecord>;
struct Pixel {
  std::vector<Events> samples;
};

enum class Kind { Random, Coincident, Transparent, Clustered };

Pixel make_pixel(std::mt19937 &rng, const int n, const Kind kind, const bool ids)
{
  std::uniform_real_distribution<float> unit(0, 1);
  const float coincident[] = {2.f, 3.5f, 5.f, 5.f, 7.25f};
  Pixel p;
  p.samples.resize(n);
  for (auto &events : p.samples) {
    if (unit(rng) < .15f)
      continue; /* miss */
    const int count = kind == Kind::Transparent ? 2 + int(unit(rng) * 7) :
                                                  int(unit(rng) * 6);
    for (int i = 0; i < count; ++i) {
      SurfaceEventRecord e;
      switch (kind) {
        case Kind::Coincident:
          e.depth = coincident[int(unit(rng) * 5)];
          break;
        case Kind::Clustered:
          e.depth = (1 + int(unit(rng) * 3)) * (1 + .004f * unit(rng));
          break;
        default:
          e.depth = unit(rng) < .3f ? coincident[int(unit(rng) * 5)] : 1 + unit(rng) * 9;
      }
      const float r = unit(rng);
      if (kind == Kind::Transparent || kind == Kind::Clustered)
        e.alpha = .002f + .2f * unit(rng);
      else
        e.alpha = r < .15f ? 1.f : r < .25f ? 0.f : r < .35f ? 1e-6f * unit(rng) : unit(rng);
      e.object = kind == Kind::Transparent || kind == Kind::Clustered ? int(unit(rng) * 2) :
                 ids ? int(unit(rng) * 3) : int(unit(rng) * 4) - 1;
      e.facing = kind == Kind::Transparent || kind == Kind::Clustered ?
                     1 :
                     int8_t(int(unit(rng) * 3) - 1);
      events.push_back(e);
    }
  }
  return p;
}

std::vector<SurfaceSample> oracle(const Pixel &p, const int population, const bool ids)
{
  PixelLedger ledger{0, 0, {}};
  for (int s = 0; s < population; ++s) {
    CameraSample sample{uint64_t(s), 1, true, {}};
    for (const auto &e : p.samples[s])
      sample.events.push_back({double(e.depth), double(e.alpha), e.object, e.facing});
    ledger.samples.push_back(std::move(sample));
  }
  return reconstruct(ledger, ids);
}

bool identical(const std::vector<SurfaceSample> &a, const std::vector<SurfaceSample> &b)
{
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::memcmp(&a[i].depth, &b[i].depth, 8) || std::memcmp(&a[i].alpha, &b[i].alpha, 8) ||
        a[i].object != b[i].object || a[i].facing != b[i].facing)
      return false;
  return true;
}

/* Maximum |T_a - T_b| over both step curves (right-continuous T). Depths in
 * a skip window [d, d * (1 + tolerance)) after any of `windows` are skipped. */
double curve_error(const std::vector<SurfaceSample> &a, const std::vector<SurfaceSample> &b,
                   const std::vector<SurfaceSample> &windows = {}, const double tolerance = 0)
{
  std::vector<double> z;
  for (const auto &s : a)
    z.push_back(s.depth);
  for (const auto &s : b)
    z.push_back(s.depth);
  z.push_back(0);
  const auto t = [](const std::vector<SurfaceSample> &c, const double depth) {
    double v = 1;
    for (const auto &s : c)
      if (s.depth <= depth)
        v *= 1 - s.alpha;
    return v;
  };
  double error = 0;
  for (const double depth : z) {
    bool skip = false;
    for (const auto &w : windows)
      skip |= tolerance > 0 && depth >= w.depth && depth < w.depth * (1 + tolerance) * (1 + 1e-7);
    if (!skip)
      error = std::max(error, std::abs(t(a, depth) - t(b, depth)));
  }
  return error;
}

struct TestPolicy final : ReducerPolicy {
  SurfaceAccumulatorSettings settings;
  SurfaceAccumulatorStatistics statistics;
  std::vector<unsigned char> combine(size_t,
                                     const NodeKey &parent,
                                     const unsigned char *left,
                                     const size_t left_bytes,
                                     const unsigned char *right,
                                     const size_t right_bytes) override
  {
    return SurfaceAccumulator::combine(parent, left, left_bytes, right, right_bytes, settings,
                                       &statistics);
  }
};

ReducerIo test_io()
{
  ReducerIo io;
  io.open = [](std::string &path) {
    static std::atomic<int> serial{0};
    path = (spill_root() / ("nodes-" + std::to_string(serial++))).string();
    FILE *file = std::fopen(path.c_str(), "w+b");
    if (!file)
      throw ReducerIoError("cannot open " + path);
    return file;
  };
  return io;
}

enum class Order { Forward, Reverse, Shuffled, Threads, Evicting };

struct Run {
  std::vector<SurfaceAccumulator::Result> results;
  uint64_t evicted = 0, capacity_merges = 0, depth_merges = 0, max_breakpoints = 0;
};

/* Feeds pixels (one row) through a real PixelReducer in the given order and
 * exports each pixel with SurfaceAccumulator::finish. */
Run accumulate(const std::vector<Pixel> &pixels, const std::vector<int> &populations,
               const int cap, const SurfaceAccumulatorSettings &settings, const Order order,
               const unsigned seed)
{
  TestPolicy policy;
  policy.settings = settings;
  const int width = int(pixels.size());
  PixelReducer reducer(width, 1, 1, uint32_t(cap), 64u * 1024 * 1024, 0, 1, test_io(), policy);
  std::vector<std::array<int, 2>> items;
  for (int x = 0; x < width; ++x)
    for (int s = 0; s < populations[x]; ++s)
      items.push_back({x, s});
  std::mt19937 rng(seed);
  if (order == Order::Reverse)
    std::reverse(items.begin(), items.end());
  if (order == Order::Shuffled || order == Order::Threads || order == Order::Evicting)
    std::shuffle(items.begin(), items.end(), rng);
  const auto add = [&](const std::array<int, 2> &item) {
    reducer.add(size_t(item[0]), uint32_t(item[1]),
                SurfaceAccumulator::encode_sample(pixels[item[0]].samples[item[1]], settings.ids));
  };
  if (order == Order::Threads) {
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
      threads.emplace_back([&, t] {
        for (size_t i = t; i < items.size(); i += 4)
          add(items[i]);
      });
    for (auto &thread : threads)
      thread.join();
  }
  else {
    size_t n = 0;
    for (const auto &item : items) {
      add(item);
      if (order == Order::Evicting && ++n % 5 == 0)
        reducer.reserve(reducer.ram_limit());
    }
  }
  if (order == Order::Evicting)
    reducer.compact();
  Run run;
  for (int x = 0; x < width; ++x) {
    check(reducer.complete(size_t(x), uint32_t(populations[x])));
    std::vector<PixelReducer::ExportNode> nodes;
    std::vector<std::vector<unsigned char>> scratch;
    reducer.export_nodes(size_t(x), nodes, scratch);
    std::vector<std::pair<const unsigned char *, size_t>> payloads;
    for (const auto &node : nodes)
      payloads.emplace_back(node.payload, node.bytes);
    run.results.push_back(SurfaceAccumulator::finish(payloads, uint32_t(populations[x]),
                                                     settings, &policy.statistics));
  }
  run.evicted = reducer.statistics().evicted_nodes;
  run.capacity_merges = policy.statistics.capacity_merges.load();
  run.depth_merges = policy.statistics.depth_merges.load();
  run.max_breakpoints = policy.statistics.max_breakpoints.load();
  return run;
}

/* Capture fixture: per pixel/sample sorted KernelDeepEvents. */
struct Frame {
  int width, height, samples;
  bool ids;
  std::vector<std::vector<KernelDeepEvent>> data;
  const std::vector<KernelDeepEvent> &at(int x, int y, int s) const
  {
    return data[(size_t(y) * width + x) * samples + s];
  }
};

Frame make_frame(const int width, const int height, const int samples, const bool ids,
                 const unsigned seed)
{
  Frame f{width, height, samples, ids, {}};
  std::mt19937 rng(seed);
  f.data.resize(size_t(width) * height * samples);
  for (auto &events : f.data) {
    const Pixel p = make_pixel(rng, 1, Kind::Random, ids);
    for (const auto &e : p.samples[0])
      events.push_back({deep_event_pack(DEEP_SURFACE, uint32_t(std::max(0, int(e.object)))),
                        e.depth, e.depth, e.alpha, float(e.facing)});
    std::sort(events.begin(), events.end(),
              [](const auto &a, const auto &b) { return a.front < b.front; });
  }
  return f;
}

/* raw: CYCLES_DEEP_SURFACE_REDUCE=0 (the Stage 3 path, the reference). */
std::unique_ptr<Capture> make_capture(const Frame &f, const float error, const bool retain,
                                      const bool adaptive = false, const bool raw = false)
{
  if (raw)
    set_env("CYCLES_DEEP_SURFACE_REDUCE", "0");
  auto c = std::make_unique<Capture>(f.width, f.height, f.samples, 256u * 1024 * 1024, 16, true,
                                     adaptive, false, false, 2, error, 0, f.ids,
                                     spill_root().string(), 0, retain);
  if (raw)
    set_env("CYCLES_DEEP_SURFACE_REDUCE", nullptr);
  if (f.ids)
    c->set_object_manifest({{11u, "a"}, {22u, "b"}, {33u, "c"}});
  return c;
}

void record(Capture &c, const Frame &f, const int x, const int y, const int s)
{
  const auto &events = f.at(x, y, s);
  c.record_sample(x, y, uint32_t(s), {DEEP_COMPLETE, unsigned(events.size()), DEEP_ERROR_NONE},
                  events.empty() ? nullptr : events.data());
}

using Image = std::vector<std::vector<SurfaceSample>>;
Image export_image(const Capture &c)
{
  Image image(size_t(c.width()) * c.height());
  for (int y = c.height() - 1; y >= 0; --y) {
    c.begin_export_row(y);
    for (int x = 0; x < c.width(); ++x)
      image[size_t(y) * c.width() + x] = c.reconstruct_pixel(x, y);
    c.end_export_row(y);
  }
  return image;
}
}  // namespace

int run_surface_reducer_tests()
{
  try {
    size_t identity = 0, overflow_cases = 0;
    double worst_ratio = 0, worst_ledger = 0;
    uint64_t total_capacity_merges = 0, total_depth_merges = 0;
    /* 1. Byte identity with reconstruct(): random, coincident and ID fixtures,
     * non-power-of-two populations, every arrival order, with eviction. */
    for (const bool ids : {false, true})
      for (const Kind kind : {Kind::Random, Kind::Coincident, Kind::Transparent}) {
        std::mt19937 rng(17 + int(kind) * 5 + ids);
        const int cap = 70;
        const std::vector<int> populations = {1, 2, 3, 7, 8, 13, 32, 45, 64, 70};
        std::vector<Pixel> pixels;
        for (size_t i = 0; i < populations.size(); ++i)
          pixels.push_back(make_pixel(rng, cap, kind, ids));
        SurfaceAccumulatorSettings settings;
        settings.ids = ids;
        settings.budget = 1e-3; /* No merge below capacity: still exact. */
        for (const Order order : {Order::Forward, Order::Reverse, Order::Shuffled, Order::Threads,
                                  Order::Evicting})
        {
          const Run run = accumulate(pixels, populations, cap, settings, order, 3 + int(order));
          if (order == Order::Evicting)
            check(run.evicted > 0);
          check(run.capacity_merges == 0 && run.depth_merges == 0);
          for (size_t x = 0; x < pixels.size(); ++x) {
            check(identical(run.results[x].samples, oracle(pixels[x], populations[x], ids)));
            check(run.results[x].ledger == 0);
            ++identity;
          }
        }
      }
    /* Coincident depths collapse to one breakpoint per key. */
    {
      std::mt19937 rng(5);
      const std::vector<Pixel> pixels = {make_pixel(rng, 64, Kind::Coincident, false)};
      SurfaceAccumulatorSettings settings;
      const Run run = accumulate(pixels, {64}, 64, settings, Order::Shuffled, 1);
      check(run.results[0].breakpoints <= 4);
      check(identical(run.results[0].samples, oracle(pixels[0], 64, false)));
    }
    /* 2. Capacity overflow: ledger merges (z tolerance 0) and depth merges
     * (budget 0). Measured error <= ledger <= budget; identical results in
     * every arrival order (folds run in sample order). */
    for (const bool ids : {false, true})
      for (const int capacity : {4, 8, 16}) {
        std::mt19937 rng(100 + capacity + ids);
        const int cap = 64;
        const std::vector<int> populations = {5, 17, 33, 64};
        std::vector<Pixel> pixels;
        for (size_t i = 0; i < populations.size(); ++i)
          pixels.push_back(make_pixel(rng, cap, Kind::Transparent, ids));
        SurfaceAccumulatorSettings settings;
        settings.ids = ids;
        settings.capacity = size_t(capacity);
        settings.budget = 2e-2;
        const Run reference = accumulate(pixels, populations, cap, settings, Order::Forward, 0);
        check(reference.capacity_merges > 0);
        total_capacity_merges += reference.capacity_merges;
        for (const Order order : {Order::Reverse, Order::Threads, Order::Evicting}) {
          const Run run = accumulate(pixels, populations, cap, settings, order, 9);
          for (size_t x = 0; x < pixels.size(); ++x)
            check(identical(run.results[x].samples, reference.results[x].samples) &&
                  run.results[x].ledger == reference.results[x].ledger);
        }
        for (size_t x = 0; x < pixels.size(); ++x) {
          const auto &r = reference.results[x];
          const double measured = curve_error(r.samples, oracle(pixels[x], populations[x], ids));
          check(r.ledger <= settings.budget);
          check(measured <= r.ledger + 1e-12);
          if (r.ledger > 0)
            worst_ratio = std::max(worst_ratio, measured / r.ledger);
          worst_ledger = std::max(worst_ledger, r.ledger);
          ++overflow_cases;
        }
      }
    for (const int capacity : {3, 6}) {
      std::mt19937 rng(300 + capacity);
      const std::vector<int> populations = {9, 31, 64};
      std::vector<Pixel> pixels;
      for (size_t i = 0; i < populations.size(); ++i)
        pixels.push_back(make_pixel(rng, 64, Kind::Clustered, false));
      SurfaceAccumulatorSettings settings;
      settings.capacity = size_t(capacity);
      settings.z_tolerance = .01;
      /* Budget 0: no merge at all (the pixel stays exact past capacity). */
      const Run exact = accumulate(pixels, populations, 64, settings, Order::Forward, 4);
      check(exact.depth_merges == 0 && exact.capacity_merges == 0);
      for (size_t x = 0; x < pixels.size(); ++x)
        check(identical(exact.results[x].samples, oracle(pixels[x], populations[x], false)));
      settings.budget = 2e-2;
      const Run run = accumulate(pixels, populations, 64, settings, Order::Shuffled, 4);
      const Run forward = accumulate(pixels, populations, 64, settings, Order::Forward, 4);
      check(run.depth_merges > 0);
      total_depth_merges += run.depth_merges;
      total_capacity_merges += run.capacity_merges;
      for (size_t x = 0; x < pixels.size(); ++x) {
        const auto &r = run.results[x];
        check(identical(r.samples, forward.results[x].samples));
        /* Depth merges are charged like any other move. */
        const double measured = curve_error(r.samples, oracle(pixels[x], populations[x], false));
        check(r.ledger <= settings.budget && measured <= r.ledger + 1e-12);
        if (r.ledger > 0)
          worst_ratio = std::max(worst_ratio, measured / r.ledger);
        ++overflow_cases;
      }
    }
    /* No legal merge (budget 0, no tolerance): the pixel stays exact. */
    {
      std::mt19937 rng(7);
      const std::vector<Pixel> pixels = {make_pixel(rng, 40, Kind::Transparent, false)};
      SurfaceAccumulatorSettings settings;
      settings.capacity = 4;
      const Run run = accumulate(pixels, {40}, 40, settings, Order::Forward, 0);
      check(run.capacity_merges == 0 && run.max_breakpoints > 4);
      check(identical(run.results[0].samples, oracle(pixels[0], 40, false)));
    }
    /* 3. Capture: online accumulation vs the raw spill (same reconstruct()). */
    size_t capture_cases = 0;
    for (const bool ids : {false, true})
      for (const bool adaptive : {false, true}) {
        const Frame f = make_frame(5, 3, 37, ids, 41 + ids * 2 + adaptive);
        auto online = make_capture(f, 1e-4f, false, adaptive);
        auto both = make_capture(f, 1e-4f, true, adaptive); /* online + raw records */
        auto raw = make_capture(f, 1e-4f, true, adaptive, true);
        check(online->surface_reducing() && online->reducing() && !online->retains_samples());
        check(both->surface_reducing() && both->retains_samples() && !raw->reducing());
        std::vector<std::array<int, 3>> items;
        for (int y = 0; y < f.height; ++y)
          for (int x = 0; x < f.width; ++x) {
            const int population = adaptive ? 1 + (x * 7 + y * 11) % f.samples : f.samples;
            for (int s = 0; s < population; ++s)
              items.push_back({x, y, s});
          }
        std::mt19937 rng(9);
        std::shuffle(items.begin(), items.end(), rng);
        for (const auto &i : items) {
          record(*online, f, i[0], i[1], i[2]);
          record(*both, f, i[0], i[1], i[2]);
          record(*raw, f, i[0], i[1], i[2]);
        }
        if (adaptive)
          for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x) {
              const int population = 1 + (x * 7 + y * 11) % f.samples;
              online->set_population(x, y, uint32_t(population));
              both->set_population(x, y, uint32_t(population));
              raw->set_population(x, y, uint32_t(population));
            }
        online->end_pass();
        check(online->finalize() && both->finalize() && raw->finalize());
        /* Per-sample records stay readable with the raw spill retained. */
        both->begin_export_row(f.height - 1);
        check(both->events(0, f.height - 1, 0).size() == raw->events(0, f.height - 1, 0).size());
        both->end_export_row(f.height - 1);
        const Image a = export_image(*online), b = export_image(*raw);
        both = make_capture(f, 1e-4f, true, adaptive);
        for (const auto &i : items)
          record(*both, f, i[0], i[1], i[2]);
        if (adaptive)
          for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x)
              both->set_population(x, y, uint32_t(1 + (x * 7 + y * 11) % f.samples));
        check(both->finalize());
        const Image c = export_image(*both);
        for (size_t p = 0; p < a.size(); ++p)
          check(identical(a[p], b[p]) && identical(c[p], b[p]));
        check(online->spill_statistics().index_bytes == 0);
        check(online->reducer_summary().find("kind=surface") != std::string::npos);
        ++capture_cases;
      }
    /* Parallel host ingest (GPU path): 2 and 8 workers, one-record and
     * default chunks, a 1.5 MiB share (eviction), pass ends: identical to the
     * raw path. */
    for (const bool ids : {false, true})
      for (const unsigned threads : {2u, 8u})
        for (const bool tiny : {false, true}) {
          const Frame f = make_frame(16, 66, 37, ids, 300 + ids);
          auto raw = make_capture(f, 1e-4f, true, false, true);
          for (int s = 0; s < f.samples; ++s)
            for (int y = 0; y < f.height; ++y)
              for (int x = 0; x < f.width; ++x)
                record(*raw, f, x, y, s);
          check(raw->finalize());
          const Image b = export_image(*raw);
          auto c = std::make_unique<Capture>(f.width, f.height, f.samples, 12u * 1024 * 1024, 16,
                                             true, false, false, false, 2, 1e-4f, 0, f.ids,
                                             spill_root().string(), 0, false);
          if (f.ids)
            c->set_object_manifest({{11u, "a"}, {22u, "b"}, {33u, "c"}});
          check(c->surface_reducing());
          set_env("CYCLES_DEEP_INGEST_CHUNK", tiny ? "1" : nullptr);
          c->set_ingest_threads(int(threads));
          set_env("CYCLES_DEEP_INGEST_CHUNK", nullptr);
          check(c->ingest_threads() == int(threads));
          /* Passes of growing sample ranges, as the GPU scheduler issues them. */
          for (int start = 0, size = 1; start < f.samples; start += size, size = std::min(size * 2, 16)) {
            for (int y = 0; y < f.height; ++y)
              for (int x = 0; x < f.width; ++x)
                for (int s = start; s < std::min(f.samples, start + size); ++s)
                  record(*c, f, x, y, s);
            c->end_pass();
          }
          check(c->finalize());
          const Image a = export_image(*c);
          for (size_t p = 0; p < a.size(); ++p)
            check(identical(a[p], b[p]));
          const std::string summary = c->reducer_summary();
          check(summary.find("ingest_threads=" + std::to_string(threads)) != std::string::npos);
          std::cout << "surface ingest ids=" << ids << " threads=" << threads << " tiny=" << tiny
                    << ": " << summary.substr(summary.find("evicted=")) << "\n";
          ++capture_cases;
        }
    /* Opaque-only capture (record API, no event capacity). */
    {
      const int w = 3, h = 2, n = 24;
      auto make = [&](const bool raw) {
        if (raw)
          set_env("CYCLES_DEEP_SURFACE_REDUCE", "0");
        auto c = std::make_unique<Capture>(w, h, n, 64u * 1024 * 1024, 0, true, false, false, false,
                                           1, 1e-3f, 0, false, spill_root().string(), 0, true);
        set_env("CYCLES_DEEP_SURFACE_REDUCE", nullptr);
        return c;
      };
      auto online = make(false), raw = make(true);
      check(!raw->reducing());
      check(online->surface_reducing());
      std::mt19937 rng(3);
      std::uniform_real_distribution<float> unit(0, 1);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int s = n - 1; s >= 0; --s) {
            const float depth = unit(rng) < .2f ? 0.f : 1 + float(int(unit(rng) * 4));
            online->record(x, y, uint32_t(s), depth);
            raw->record(x, y, uint32_t(s), depth);
          }
      check(online->finalize() && raw->finalize());
      const Image a = export_image(*online), b = export_image(*raw);
      for (size_t p = 0; p < a.size(); ++p)
        check(identical(a[p], b[p]));
      ++capture_cases;
    }
    /* Off switch, strict and records keep the raw path. */
    {
      check(!make_capture(make_frame(2, 2, 8, false, 1), 1e-4f, false)->reducing());
      const Frame f = make_frame(2, 2, 9, false, 1);
      set_env("CYCLES_DEEP_SURFACE_REDUCE", "0");
      check(!make_capture(f, 1e-4f, false)->reducing());
      set_env("CYCLES_DEEP_SURFACE_REDUCE", nullptr);
      check(!make_capture(f, 0, false)->reducing());
      check(make_capture(f, 1e-4f, true)->surface_reducing());
      set_env("CYCLES_DEEP_SURFACE_CAPACITY", "1");
      bool rejected = false;
      try {
        make_capture(f, 1e-4f, false);
      }
      catch (const std::invalid_argument &) {
        rejected = true;
      }
      check(rejected);
      set_env("CYCLES_DEEP_SURFACE_CAPACITY", "8");
      check(make_capture(f, 1e-4f, false)->reducer_summary().find("surface_capacity=8") !=
            std::string::npos);
      set_env("CYCLES_DEEP_SURFACE_CAPACITY", nullptr);
    }
    /* Duplicate and completion errors carry their numbers. */
    {
      const Frame f = make_frame(3, 2, 12, false, 2);
      auto c = make_capture(f, 1e-4f, false);
      check(c->surface_reducing());
      record(*c, f, 1, 1, 4);
      record(*c, f, 1, 1, 4);
      check(c->failed());
      const std::string message = c->error_message();
      check(message.find("more than once") != std::string::npos &&
            message.find("pixel 1,1, sample 4") != std::string::npos);
      auto incomplete = make_capture(f, 1e-4f, false);
      for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x)
          for (int s = 0; s < f.samples; ++s)
            if (!(x == 2 && y == 0 && s == 3))
              record(*incomplete, f, x, y, s);
      check(!incomplete->finalize() && !incomplete->failed());
      const std::string missing = incomplete->error_message();
      check(missing.find("accepted 71 of 72") != std::string::npos &&
            missing.find("pixel 2,0 accepted 11 of population 12") != std::string::npos);
      record(*incomplete, f, 2, 0, 3);
      check(incomplete->finalize());
    }
    /* Capacity merges in a Capture respect the exact-writer budget E - F and
     * get none in compact output. */
    {
      const Frame f = make_frame(2, 1, 16, false, 4);
      auto c = make_capture(f, 1e-3f, false);
      check(c->surface_reducing());
      check(c->surface_merge_budget() == double(1e-3f) - 1e-6);
      c->compact = true;
      check(c->surface_merge_budget() == 0);
      c->compact = false;
      c->export_reduce = true;
      check(c->surface_merge_budget() == 0);
    }
    std::cout << "Online surface accumulation: " << identity << " reconstruct() byte-identity "
              << "pixels, " << capture_cases << " capture identity cases; " << overflow_cases
              << " overflow pixels (" << total_capacity_merges << " ledger merges, "
              << total_depth_merges << " depth merges), worst measured/ledger " << worst_ratio
              << ", worst ledger " << worst_ledger << "; off switch, strict, duplicate and "
              << "completion checks passed\n";
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  std::error_code ec;
  std::filesystem::remove_all(spill_root(), ec);
  return 0;
}
