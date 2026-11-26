/* SPDX-License-Identifier: Apache-2.0 */
/* Online volume reduction against the export-time balanced tree: byte identity
 * on random fixtures (non-power-of-two counts, out-of-order, split and threaded
 * arrival, eviction, compaction and cancellation), duplicates, ledger and node
 * limit failures, and adaptive populations against the exact oracle. */
#include "deep/capture.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
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

namespace ccl::deep {
struct ReducerTestAccess {
  static void evict_all(Capture &c)
  {
    c.reducer_->reserve(c.reducer_->ram_limit());
  }
  static void compaction_threshold(Capture &c, const uint64_t bytes)
  {
    c.reducer_->set_compaction_threshold(bytes);
  }
  static PixelReducer::Statistics statistics(const Capture &c)
  {
    return c.reducer_->statistics();
  }
  static void node_limit(Capture &c, const size_t limit)
  {
    c.node_limit_ = limit;
  }
  /* Replace the reducer before any sample (test ledger step or RAM share). */
  static void rebuild(Capture &c, const size_t ram, const double step)
  {
    c.make_reducer(ram, step);
  }
  static double step(const Capture &c)
  {
    return c.reducer_->step_error();
  }
};
}  // namespace ccl::deep

using namespace ccl::deep;

namespace {
void check_impl(const bool condition, const int line)
{
  if (!condition)
    throw std::runtime_error("reducer test failed at line " + std::to_string(line));
}
}  // namespace
#define check(condition) check_impl((condition), __LINE__)

static void set_env(const char *name, const char *value)
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

namespace {
struct Sample {
  std::vector<KernelDeepEvent> events;
  std::vector<KernelDeepDensity> density;
};
struct Fixture {
  int width, height, samples;
  float error;
  bool ids;
  std::vector<Sample> data; /* pixel-major, then sample */
  const Sample &at(int x, int y, int s) const
  {
    return data[(size_t(y) * width + x) * samples + s];
  }
};

static Fixture make_fixture(const int width, const int height, const int samples,
                            const float error, const bool ids, const unsigned seed)
{
  Fixture f{width, height, samples, error, ids, {}};
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> unit(0, 1);
  f.data.resize(size_t(width) * height * samples);
  for (auto &sample : f.data) {
    if (unit(rng) < .2f)
      continue; /* miss */
    const int volumes = int(unit(rng) * 5);
    for (int i = 0; i < volumes; ++i) {
      const float front = 1 + unit(rng) * 6;
      const float back = front + .05f + unit(rng) * 3;
      const int object = ids ? int(unit(rng) * 2) : 0;
      const bool cubic = unit(rng) < .3f;
      KernelDeepDensity d{};
      d.front = double(front);
      d.back = double(back);
      if (cubic) {
        for (float &c : d.optical_depth)
          c = unit(rng) * .8f;
        sample.events.push_back({deep_event_pack(DEEP_VOLUME_CUBIC, object), front, back, 0, 0});
      }
      else {
        const float tau = unit(rng) * .7f;
        d.optical_depth[0] = tau;
        d.optical_depth[1] = unit(rng) * 1e-3f;
        sample.events.push_back({deep_event_pack(DEEP_VOLUME, object), front, back, 0, tau});
      }
      sample.density.push_back(d);
    }
    if (unit(rng) < .4f) {
      const float depth = 1 + unit(rng) * 9;
      const float alpha = unit(rng) < .2f ? 1.f : unit(rng);
      const int object = ids ? int(unit(rng) * 2) : 0;
      sample.events.push_back({deep_event_pack(DEEP_SURFACE, object), depth, depth, alpha, 1});
      sample.density.push_back({});
    }
  }
  return f;
}

static void record(Capture &c, const Fixture &f, const int x, const int y, const int s)
{
  const Sample &sample = f.at(x, y, s);
  c.record_sample(x, y, uint32_t(s), {DEEP_COMPLETE, unsigned(sample.events.size()), DEEP_ERROR_NONE},
                  sample.events.empty() ? nullptr : sample.events.data(),
                  sample.density.empty() ? nullptr : sample.density.data());
}

static std::filesystem::path spill_root()
{
  static const std::filesystem::path root = [] {
    auto path = std::filesystem::temp_directory_path() /
                ("cycles-deep-reducer-test-" + std::to_string(
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

static std::unique_ptr<Capture> make_capture(const Fixture &f, const bool reduce,
                                             const bool retain, const bool adaptive = false,
                                             const size_t budget = 256u * 1024 * 1024)
{
  set_env("CYCLES_DEEP_VOLUME_REDUCE", reduce ? "1" : "0");
  auto c = std::make_unique<Capture>(f.width, f.height, f.samples, budget, 16, true, adaptive,
                                     true, true, 2, f.error, 0, f.ids,
                                     spill_root().string(), 0, retain);
  set_env("CYCLES_DEEP_VOLUME_REDUCE", nullptr);
  if (f.ids)
    c->set_object_manifest({{11u, "a"}, {22u, "b"}});
  check(c->reducing() == reduce);
  return c;
}

using Image = std::vector<std::vector<IntervalSample>>;
static Image export_image(const Capture &c)
{
  Image image(size_t(c.width()) * c.height());
  for (int y = c.height() - 1; y >= 0; --y) {
    c.begin_export_row(y);
    for (int x = 0; x < c.width(); ++x)
      image[size_t(y) * c.width() + x] = c.reconstruct_volume_pixel(x, y);
    c.end_export_row(y);
  }
  return image;
}
static bool identical(const Image &a, const Image &b)
{
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].size() != b[i].size())
      return false;
    for (size_t j = 0; j < a[i].size(); ++j) {
      const auto &p = a[i][j], &q = b[i][j];
      if (std::memcmp(&p.front, &q.front, 8) || std::memcmp(&p.back, &q.back, 8) ||
          std::memcmp(&p.alpha, &q.alpha, 8) || p.object != q.object || p.facing != q.facing)
        return false;
    }
  }
  return true;
}

enum class Order { Forward, Reverse, Shuffled, MissesFirst, Passes, Threads };

/* Feeds every (pixel, sample) of the fixture in the given arrival order. */
static void feed(Capture &c, const Fixture &f, const Order order, const unsigned seed,
                 const bool evict = false, const std::function<void()> &pass_end = {})
{
  std::vector<std::array<int, 3>> items;
  for (int y = 0; y < f.height; ++y)
    for (int x = 0; x < f.width; ++x)
      for (int s = 0; s < f.samples; ++s)
        items.push_back({x, y, s});
  std::mt19937 rng(seed);
  switch (order) {
    case Order::Forward:
      break;
    case Order::Reverse:
      std::reverse(items.begin(), items.end());
      break;
    case Order::Shuffled:
    case Order::Threads:
      std::shuffle(items.begin(), items.end(), rng);
      break;
    case Order::MissesFirst:
      std::stable_partition(items.begin(), items.end(), [&](const auto &i) {
        return f.at(i[0], i[1], i[2]).events.empty();
      });
      break;
    case Order::Passes: {
      /* CPU-like: passes of growing sample ranges over all pixels. */
      std::vector<std::array<int, 3>> ordered;
      int start = 0, size = 1;
      while (start < f.samples) {
        const int end = std::min(f.samples, start + size);
        for (int y = 0; y < f.height; ++y)
          for (int x = 0; x < f.width; ++x)
            for (int s = start; s < end; ++s)
              ordered.push_back({x, y, s});
        start = end;
        size = std::min(size * 2, 16);
        ordered.push_back({-1, -1, -1}); /* pass boundary */
      }
      items.swap(ordered);
      break;
    }
  }
  if (order == Order::Threads) {
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
      threads.emplace_back([&, t] {
        for (size_t i = t; i < items.size(); i += 4)
          record(c, f, items[i][0], items[i][1], items[i][2]);
      });
    for (auto &thread : threads)
      thread.join();
    return;
  }
  size_t n = 0;
  for (const auto &item : items) {
    if (item[0] < 0) {
      if (evict)
        ReducerTestAccess::evict_all(c);
      if (pass_end)
        pass_end();
      continue;
    }
    record(c, f, item[0], item[1], item[2]);
    if (evict && ++n % 7 == 0)
      ReducerTestAccess::evict_all(c);
  }
}

static std::vector<double> probes()
{
  std::vector<double> z;
  for (int i = 0; i <= 400; ++i)
    z.push_back(.5 + 13.0 * (i + .37) / 401);
  return z;
}

/* Exact mixture transmittance of the accepted samples (linear-in-depth optical
 * depth for constant intervals; cubic Bernstein integral for cubic cells). */
static double oracle(const Fixture &f, const int x, const int y, const int population,
                     const double z)
{
  double sum = 0;
  for (int s = 0; s < population; ++s) {
    const Sample &sample = f.at(x, y, s);
    double tau = 0, surface = 1, opaque = INFINITY;
    for (const auto &e : sample.events)
      if (deep_event_type(e) == DEEP_SURFACE && e.surface_alpha == 1)
        opaque = std::min(opaque, double(e.front));
    for (size_t i = 0; i < sample.events.size(); ++i) {
      const auto &e = sample.events[i];
      if (deep_event_type(e) == DEEP_SURFACE) {
        if (double(e.front) <= z && double(e.front) <= opaque)
          surface *= 1 - double(e.surface_alpha);
        continue;
      }
      const auto &d = sample.density[i];
      const double front = d.front, back = d.back;
      const double end = std::min({z, back, opaque});
      if (end <= front)
        continue;
      const double t = (end - front) / (back - front);
      if (deep_event_type(e) == DEEP_VOLUME) {
        tau += t * (f.error > 0 ? double(d.optical_depth[0]) + double(d.optical_depth[1]) :
                                  double(e.optical_depth));
      }
      else {
        /* Integral of the cubic Bernstein density (controls are cell optical
         * depths): integral_0^t B(u) du, with mean = total cell depth. */
        const double b[4] = {d.optical_depth[0], d.optical_depth[1], d.optical_depth[2],
                             d.optical_depth[3]};
        const double u = t, v = 1 - t;
        /* Antiderivatives of the degree-3 Bernstein basis (each integrates to 1/4). */
        const double i0 = (1 - std::pow(v, 4)) / 4, i3 = std::pow(u, 4) / 4;
        const double i1 = (3 * u * u / 2 - 2 * u * u * u + 3 * std::pow(u, 4) / 4);
        const double i2 = (u * u * u - 3 * std::pow(u, 4) / 4);
        tau += b[0] * i0 + b[1] * i1 + b[2] * i2 + b[3] * i3;
      }
    }
    sum += std::exp(-tau) * surface;
  }
  return sum / population;
}

}  // namespace

/* Run from the capture CTest (keeps the configured CTest set). */
int run_reducer_tests()
{
  try {
    size_t identity_cases = 0, oracle_checks = 0;
    double adaptive_worst = 0, adaptive_bound = 0;
    /* 1. Byte identity, every arrival order, strict and numeric, IDs. */
    const int counts[] = {9, 12, 16, 17, 31, 64, 100, 128};
    unsigned seed = 1;
    for (const int samples : counts)
      for (const float error : {0.f, 1e-4f})
        for (const bool ids : {false, true}) {
          /* Strict leaves run the one-ray fit (slow): fewer strict/ID cases. */
          if ((ids && samples > 31) || (error == 0 && samples > 17 && samples != 100) ||
              (ids && error == 0 && samples > 12))
            continue;
          const Fixture f = make_fixture(3, 2, samples, error, ids, seed++);
          auto reference = make_capture(f, false, true);
          feed(*reference, f, Order::Forward, 0);
          check(reference->finalize());
          const Image expected = export_image(*reference);
          for (const Order order : {Order::Forward, Order::Reverse, Order::Shuffled,
                                    Order::MissesFirst, Order::Passes, Order::Threads})
            for (const bool retain : {false, true}) {
              auto online = make_capture(f, true, retain);
              feed(*online, f, order, seed * 7 + int(order));
              check(online->finalize());
              /* Pre-export reads (validation) and the staged export agree. */
              const auto early = online->reconstruct_volume_pixel(1, 1);
              const Image actual = export_image(*online);
              check(identical(actual, expected));
              check(identical({early}, {actual[size_t(1) * f.width + 1]}));
              ++identity_cases;
            }
          std::cout << "identity samples=" << samples << " error=" << error << " ids=" << ids
                    << std::endl;
        }
    /* 2. Eviction to the node stream, reload, compaction at pass ends and a
     * cancelled compaction keep bytes identical. */
    for (const int samples : {17, 40}) {
      const Fixture f = make_fixture(2, 66, samples, samples == 17 ? 0.f : 1e-4f, false,
                                     900 + samples);
      auto reference = make_capture(f, false, true);
      feed(*reference, f, Order::Forward, 0);
      const Image expected = export_image(*reference);
      for (const bool cancel : {false, true}) {
        auto online = make_capture(f, true, false);
        ReducerTestAccess::compaction_threshold(*online, 0);
        int calls = 0;
        feed(*online, f, Order::Passes, 5, true, [&] {
          online->end_pass([&] { return cancel && ++calls % 3 == 0; });
        });
        const auto stats = ReducerTestAccess::statistics(*online);
        check(stats.evicted_nodes > 0 && stats.reloaded_nodes > 0);
        if (!cancel)
          check(stats.compactions > 0);
        check(online->finalize());
        check(identical(export_image(*online), expected));
        ++identity_cases;
        std::cout << "evict samples=" << samples << " cancel=" << cancel
                  << " evicted=" << stats.evicted_nodes << " reloaded=" << stats.reloaded_nodes
                  << " compactions=" << stats.compactions << " file_peak=" << stats.file_peak
                  << "\n";
      }
    }
    /* 3. Duplicates are explicit errors (online and with retained samples). */
    for (const bool retain : {false, true}) {
      const Fixture f = make_fixture(2, 1, 16, 0.f, false, 77);
      auto c = make_capture(f, true, retain);
      record(*c, f, 0, 0, 3);
      record(*c, f, 0, 0, 2);
      record(*c, f, 0, 0, 3);
      check(c->failed());
      check(std::string(c->error_message()).find("more than once") != std::string::npos);
      check(!c->finalize());
      /* A sample covered by a merged node is also a duplicate. */
      auto merged = make_capture(f, true, false);
      for (int s = 0; s < 4; ++s)
        record(*merged, f, 1, 0, s);
      record(*merged, f, 1, 0, 1);
      check(merged->failed());
      check(std::string(merged->error_message()).find("sample 1") != std::string::npos);
    }
    /* 4. Completion: a missing sample or out-of-population leaf fails finalize. */
    {
      const Fixture f = make_fixture(1, 1, 16, 0.f, false, 78);
      auto c = make_capture(f, true, false);
      for (int s = 0; s < 15; ++s)
        record(*c, f, 0, 0, s);
      check(!c->finalize());
      record(*c, f, 0, 0, 15);
      check(c->finalize());
      auto adaptive = make_capture(f, true, false, true);
      for (int s = 0; s < 5; ++s)
        record(*adaptive, f, 0, 0, s == 4 ? 7 : s);
      adaptive->set_population(0, 0, 5);
      check(!adaptive->finalize()); /* five accepted, but sample 4 missing */
    }
    /* 5. Ledger failure: an inflated per-level step exceeds the allowance. */
    {
      const Fixture f = make_fixture(1, 1, 16, 0.f, false, 79);
      auto c = make_capture(f, true, false);
      const double step = ReducerTestAccess::step(*c);
      ReducerTestAccess::rebuild(*c, 64u * 1024 * 1024, 4 * step);
      feed(*c, f, Order::Forward, 0);
      check(c->finalize());
      bool failed = false;
      try {
        c->begin_export_row(0);
        c->reconstruct_volume_pixel(0, 0);
      }
      catch (const std::exception &e) {
        failed = std::string(e.what()).find("ledger exceeds") != std::string::npos;
        std::cout << "ledger failure: " << e.what() << "\n";
      }
      check(failed);
    }
    /* 6. Node limit failure is explicit, with numbers. */
    {
      Fixture f = make_fixture(1, 1, 16, 0.f, false, 80);
      for (auto &s : f.data)
        if (s.events.empty()) {
          s.events.push_back({deep_event_pack(DEEP_VOLUME, 0), 2, 3, 0, .5f});
          s.density.push_back({});
        }
      auto c = make_capture(f, true, false);
      ReducerTestAccess::node_limit(*c, 1);
      feed(*c, f, Order::Forward, 0);
      check(c->failed());
      const std::string message = c->error_message();
      std::cout << "limit failure: " << message << "\n";
      check(message.find("reconstruction limit") != std::string::npos &&
            message.find("limit 1") != std::string::npos);
    }
    /* 7. Destruction mid-capture with evicted nodes leaves no spill files. */
    {
      const Fixture f = make_fixture(4, 4, 32, 0.f, false, 81);
      {
        auto c = make_capture(f, true, false);
        for (int s = 0; s < 9; ++s)
          for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
              record(*c, f, x, y, s);
        ReducerTestAccess::evict_all(*c);
        check(ReducerTestAccess::statistics(*c).file_bytes > 0);
      }
      check(std::filesystem::is_empty(spill_root()));
    }
    /* 8. Adaptive populations: levels from the sample cap in both trees; within
     * the stated bound of the exact oracle (Stage 1 used population levels). */
    for (const float error : {0.f, 1e-4f}) {
      const Fixture f = make_fixture(4, 2, 64, error, false, 82 + int(error * 1e4f));
      auto c = make_capture(f, true, false, true);
      auto raw = make_capture(f, false, true, true);
      const int populations[] = {1, 2, 9, 16, 33, 50, 63, 64};
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) {
          const int population = populations[y * 4 + x];
          for (int s = population - 1; s >= 0; --s) {
            record(*c, f, x, y, s);
            record(*raw, f, x, y, s);
          }
          c->set_population(x, y, population);
          raw->set_population(x, y, population);
        }
      check(c->finalize() && raw->finalize());
      const Image online = export_image(*c), reference = export_image(*raw);
      /* Both trees take their levels from the sample cap: byte-identical. */
      check(identical(online, reference));
      const double bound = error_budget(error).effective;
      adaptive_bound = std::max(adaptive_bound, bound);
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
          for (const double z : probes()) {
            const double exact = oracle(f, x, y, populations[y * 4 + x], z);
            const double a = interval_transmittance(online[size_t(y) * 4 + x], z);
            const double b = interval_transmittance(reference[size_t(y) * 4 + x], z);
            adaptive_worst = std::max(adaptive_worst, std::abs(a - exact));
            check(std::abs(a - exact) <= bound);
            check(std::abs(b - exact) <= bound);
            ++oracle_checks;
          }
    }
    std::cout << "Online volume reduction: " << identity_cases
              << " byte-identity cases; adaptive worst |T - oracle| " << adaptive_worst
              << " <= bound " << adaptive_bound << " over " << oracle_checks
              << " probes; duplicate, completion, ledger, limit and cleanup checks passed\n";
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  std::error_code ec;
  std::filesystem::remove_all(spill_root(), ec);
  return 0;
}
