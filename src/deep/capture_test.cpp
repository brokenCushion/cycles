/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

using ccl::deep::Capture;
static_assert(std::is_trivially_copyable_v<KernelDeepRecord>);
static_assert(std::is_standard_layout_v<KernelDeepRecord>);
static void check(bool condition)
{
  if (!condition)
    throw std::runtime_error("capture test failed");
}
template<typename F> static void rejects(F f)
{
  bool rejected = false;
  try {
    f();
  }
  catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
}
int main()
{
  try {
    {
      using namespace ccl::deep;
      VolumeCameraSample ray{{0, 1, true, {}}, {}};
      for (int i = 0; i < 12000; ++i)
        ray.intervals.push_back({double(i + 1), double(i + 2), .0001});
      const auto curve = reconstruct_volume({ray}, 2e-7, 16384);
      check(curve.size() == 12000);
      for (const double depth : {1.0, 10.5, 6000.25, 12001.0})
        check(std::abs(interval_transmittance(curve, depth) -
                        std::exp(-.0001 * (depth - 1))) < 1e-12);
      ray.intervals = {{1, 2, 80}};
      const auto dense = reconstruct_volume({ray});
      check(dense.size() == 5);
      check(std::abs(interval_transmittance(dense, 1.25) - std::exp(-20.0)) < 1e-15);
    }
    {
      Capture memory(33, 1, 17, 128 * 1024 * 1024, 65, false, false, true, true);
      Capture disk(33, 1, 17, 128 * 1024 * 1024, 65, true, false, true, true);
      std::vector<KernelDeepEvent> events(65);
      std::vector<KernelDeepDensity> density(65);
      events[0] = {DEEP_SURFACE, 1, 1, .25f, 0};
      for (unsigned i = 1; i < 65; ++i) {
        events[i] = {DEEP_VOLUME_CUBIC, float(i), float(i + 1), 0, 0};
        density[i] = {{.001f, .001f, .001f, .001f}, double(i), double(i + 1)};
      }
      for (int sample = 0; sample < 17; ++sample)
        for (int x = 32; x >= 0; --x)
          for (Capture *capture : {&memory, &disk})
            capture->record_sample(x, 0, sample, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE},
                                   events.data(), density.data());
      check(memory.finalize() && disk.finalize());
      for (int x = 0; x < 33; ++x)
        for (int sample = 0; sample < 17; ++sample) {
          const auto a = memory.volume_sample(x, 0, sample);
          const auto b = disk.volume_sample(x, 0, sample);
          check(a.camera.events.size() == 1 && b.camera.events.size() == 1);
          check(a.intervals.size() == 64 && b.intervals.size() == 64);
          for (size_t i = 0; i < a.intervals.size(); ++i) {
            check(a.intervals[i].front == b.intervals[i].front);
            check(a.intervals[i].back == b.intervals[i].back);
            check(a.intervals[i].optical_depth == b.intervals[i].optical_depth);
            check(b.intervals[i].optical_depth == double(.001f));
          }
        }
      check(disk.value(0, 0, 0) == 65);
      disk.record_sample(0, 0, 0, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE},
                         events.data(), density.data());
      check(!disk.finalize());
      for (bool spill : {false, true}) {
        Capture thin(1, 1, 1, 8 * 1024 * 1024, 1, spill, false, true, true);
        const KernelDeepEvent thin_event{DEEP_VOLUME_CUBIC, 1000, 1000, 0, 0};
        const KernelDeepDensity thin_density{{1e-8f, 1e-8f, 1e-8f, 1e-8f}, 1000, 1000 + 1e-8};
        thin.record_sample(0, 0, 0, {DEEP_COMPLETE, 1, DEEP_ERROR_NONE}, &thin_event, &thin_density);
        check(thin.finalize());
        const auto thin_sample = thin.volume_sample(0, 0, 0);
        check(thin_sample.intervals.size() == 1);
        check(thin_sample.intervals[0].front == thin_density.front &&
              thin_sample.intervals[0].back == thin_density.back);
        Capture varying(1, 1, 2, 8 * 1024 * 1024, 2, spill, false, true, true);
        const KernelDeepEvent cell{DEEP_VOLUME_CUBIC, 1, 2, 0, 0};
        const KernelDeepDensity curve{{0, .0001f, .0002f, 0}, 1, 2};
        varying.record_sample(0, 0, 0, {DEEP_COMPLETE, 1, DEEP_ERROR_NONE}, &cell, &curve);
        check(!varying.volume_sample(0, 0, 0).intervals.empty());
        /* Resume appending after reading a partial final event cache page. */
        varying.record_sample(0, 0, 1, {DEEP_COMPLETE, 1, DEEP_ERROR_NONE}, &cell, &curve);
        check(varying.finalize());
        const auto reconstructed = varying.reconstruct_volume_pixel(0, 0);
        for (int probe = 0; probe <= 100; ++probe) {
          const double u = probe / 100.0;
          const double b = curve.optical_depth[1], c = curve.optical_depth[2];
          const double tau = 1.5 * b * u * u + (-2 * b + c) * u * u * u +
                             (3 * b - 3 * c) * u * u * u * u / 4;
          check(std::abs(interval_transmittance(reconstructed, 1 + u) - std::exp(-tau)) < 3e-7);
        }
        Capture missing(1, 1, 1, 8 * 1024 * 1024, 65, spill, false, true, true);
        missing.record_sample(0, 0, 0, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE}, events.data());
        check(!missing.finalize());
        Capture invalid(1, 1, 1, 8 * 1024 * 1024, 65, spill, false, true, true);
        density[1].optical_depth[2] = -1;
        invalid.record_sample(0, 0, 0, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE},
                               events.data(), density.data());
        check(!invalid.finalize());
      }
    }
    rejects([] { Capture c(-1, 2, 1, 1024); });
    rejects([] { Capture c(1, 2, 0, 1024); });
    rejects([] { Capture c(1, 1, 4097, 1024); });
    rejects([] { Capture c(1000, 1000, 1000, 1024); });
    rejects([] { Capture c(INT32_MAX, INT32_MAX, 4096, SIZE_MAX); });
    Capture capture(2, 1, 4, 256);
    check(!capture.finalize());
    rejects([&] { capture.reconstruct_pixel(0, 0); });
    std::thread a([&] {
      for (int i = 0; i < 4; ++i)
        capture.record(0, 0, i, i < 2 ? 2.0f : 8.0f);
    });
    std::thread b([&] {
      for (int i = 0; i < 4; ++i)
        capture.record(1, 0, i, i == 0 ? 4.0f : 0.0f);
    });
    a.join();
    b.join();
    check(capture.finalize());
    auto p = capture.reconstruct_pixel(0, 0);
    check(p.size() == 2 && p[0].depth == 2 && p[0].alpha == 0.5 && p[1].depth == 8 &&
          p[1].alpha == 1);
    p = capture.reconstruct_pixel(1, 0);
    check(p.size() == 1 && p[0].alpha == 0.25);
    rejects([&] { capture.value(2, 0, 0); });
    capture.record(0, 0, 0, 2);
    check(!capture.finalize());
    rejects([&] { capture.reconstruct_pixel(0, 0); });
    for (float invalid :
         {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
      Capture c(1, 1, 1, 32);
      c.record(0, 0, 0, invalid);
      check(!c.finalize());
    }
    Capture bounds(1, 1, 1, 32);
    bounds.record(1, 0, 0, 0);
    check(!bounds.finalize());
    Capture sample(1, 1, 1, 32);
    sample.record(0, 0, 1, 0);
    check(!sample.finalize());
    Capture state(1, 1, 1, 32);
    state.fail();
    state.record(0, 0, 0, 0);
    check(!state.finalize());
    Capture miss(1, 1, 1, 32);
    miss.record(0, 0, 0, 0);
    check(miss.finalize());
    check(miss.reconstruct_pixel(0, 0).empty());
    Capture chains(1, 1, 2, 104, 2);
    const KernelDeepEvent stack[] = {{DEEP_SURFACE, 2, 2, .25f, 0}, {DEEP_SURFACE, 8, 8, .5f, 0}};
    chains.record_events(0, 0, 0, stack, 2);
    check(!chains.finalize());
    chains.record_events(0, 0, 1, nullptr, 0);
    check(chains.finalize());
    p = chains.reconstruct_pixel(0, 0);
    check(p.size() == 2 && p[0].alpha == .125);
    check(std::abs((1 - p[0].alpha) * (1 - p[1].alpha) - .6875) < 1e-12);
    chains.record_events(0, 0, 0, stack, 2);
    check(!chains.finalize());
    rejects([] { Capture c(1, 1, 2, 103, 2); });
    rejects([] { Capture c(1, 1, 1, 1000, 65); });
    for (const float alpha : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
      Capture c(1, 1, 1, 32, 1);
      const KernelDeepEvent event[] = {{DEEP_SURFACE, 2, 2, alpha, 0}};
      c.record_events(0, 0, 0, event, 1);
      check(!c.finalize());
    }
    Capture overflow(1, 1, 1, 32, 1);
    overflow.record_events(0, 0, 0, stack, 2);
    check(!overflow.finalize());
    for (const bool spill : {false, true}) {
      for (const KernelDeepResult invalid : {KernelDeepResult{DEEP_EMPTY, 0, DEEP_ERROR_NONE},
                                             KernelDeepResult{DEEP_ACTIVE, 0, DEEP_ERROR_NONE},
                                             KernelDeepResult{DEEP_FAILED, 0, DEEP_ERROR_CAPACITY},
                                             KernelDeepResult{DEEP_COMPLETE, 0, DEEP_ERROR_DEPTH},
                                             KernelDeepResult{DEEP_SKIPPED, 1, DEEP_ERROR_NONE},
                                             KernelDeepResult{DEEP_SKIPPED, 0, DEEP_ERROR_STATE}})
      {
        Capture c(1, 1, 1, 4 * 1024 * 1024, 1, spill);
        c.record_sample(0, 0, 0, invalid, nullptr);
        c.record_sample(0, 0, 0, {DEEP_COMPLETE, 0, DEEP_ERROR_NONE}, nullptr);
        check(!c.finalize());
        rejects([&] { c.reconstruct_pixel(0, 0); });
      }
      Capture skipped(1, 1, 1, 4 * 1024 * 1024, 1, spill);
      skipped.record_sample(0, 0, 0, {DEEP_SKIPPED, 0, DEEP_ERROR_NONE}, nullptr);
      check(!skipped.finalize());
      skipped.record_sample(0, 0, 0, {DEEP_COMPLETE, 0, DEEP_ERROR_NONE}, nullptr);
      check(skipped.finalize() && skipped.reconstruct_pixel(0, 0).empty());
      for (const KernelDeepEvent invalid : {KernelDeepEvent{DEEP_SURFACE, 2, 3, .5f, 0},
                                            KernelDeepEvent{DEEP_SURFACE, 2, 2, .5f, 1},
                                            KernelDeepEvent{DEEP_VOLUME, 2, 2, 0, 1},
                                            KernelDeepEvent{DEEP_VOLUME, 2, 3, .5f, 1},
                                            KernelDeepEvent{KernelDeepEventKind(99), 2, 3, 0, 1}})
      {
        Capture c(1, 1, 1, 4 * 1024 * 1024, 1, spill, false, true);
        c.record_events(0, 0, 0, &invalid, 1);
        check(!c.finalize());
      }
      Capture surface_only(1, 1, 1, 4 * 1024 * 1024, 1, spill);
      const KernelDeepEvent medium{DEEP_VOLUME, 2, 3, 0, 1};
      surface_only.record_events(0, 0, 0, &medium, 1);
      check(!surface_only.finalize());
      Capture volume(1, 1, 2, 4 * 1024 * 1024, 3, spill, false, true);
      const KernelDeepEvent intervals[] = {
          {DEEP_VOLUME, 2, 8, 0, 1.8f}, {DEEP_SURFACE, 5, 5, .5f, 0}, {DEEP_VOLUME, 4, 9, 0, 1}};
      volume.record_events(0, 0, 0, intervals, 3);
      check(!volume.finalize());
      volume.record_events(0, 0, 1, nullptr, 0);
      check(volume.finalize());
      const auto v = volume.volume_sample(0, 0, 0);
      check(v.intervals.size() == 2 && v.camera.events.size() == 1);
      const auto curve = volume.reconstruct_volume_pixel(0, 0);
      check(std::abs(interval_transmittance(curve, 6) -
                     (.5 + .25 * std::exp(-double(1.8f) * 4 / 6 - .4))) < 2e-7);
      rejects([&] { volume.events(0, 0, 0); });
      volume.record_events(0, 0, 0, intervals, 3);
      check(!volume.finalize());
      Capture adaptive(2, 1, 8, 4 * 1024 * 1024, 2, spill, true);
      adaptive.record_events(0, 0, 0, stack, 2);
      adaptive.record_events(0, 0, 1, nullptr, 0);
      adaptive.set_population(0, 0, 2);
      adaptive.record_events(1, 0, 0, nullptr, 0);
      check(!adaptive.finalize());  // Missing independent film population.
      adaptive.set_population(1, 0, 1);
      check(adaptive.finalize());
      p = adaptive.reconstruct_pixel(0, 0);
      check(p.size() == 2 && p[0].alpha == .125);  // Divide by 2, not maximum 8.
      check(adaptive.reconstruct_pixel(1, 0).empty());
      adaptive.set_population(0, 0, 3);  // Film accepted an uncaptured sample.
      check(!adaptive.finalize());
      adaptive.record_events(0, 0, 2, stack, 2);
      check(adaptive.finalize());  // A converged pixel may become active again.
      adaptive.record_events(0, 0, 3, nullptr, 0);
      check(!adaptive.finalize());  // Extra capture outside film population.
      Capture hole(1, 1, 8, 4 * 1024 * 1024, 0, spill, true);
      hole.record(0, 0, 1, 2);
      hole.set_population(0, 0, 1);
      check(!hole.finalize());  // Equal counts do not hide a missing identity.
      Capture decreasing(1, 1, 8, 4 * 1024 * 1024, 0, spill, true);
      decreasing.record(0, 0, 0, 2);
      decreasing.set_population(0, 0, 2);
      decreasing.set_population(0, 0, 1);
      check(!decreasing.finalize());
    }
    {
      /* More pages than the bounded cache, revisited over sample batches.
       * This exercises dirty eviction, partial last pages and later duplicate
       * detection against records no longer resident in the cache. */
      Capture memory(257, 1, 128, 32 * 1024 * 1024, 2, false, true);
      Capture disk(257, 1, 128, 32 * 1024 * 1024, 2, true, true);
      for (int first = 0; first < 128; first += 8) {
        for (int x = 256; x >= 0; --x) {
          for (int sample = first; sample < first + 8; ++sample) {
            const float z = 1 + float(x * 128 + sample) / 1024;
            const KernelDeepEvent events[] = {{DEEP_SURFACE, z, z, .25f, 0},
                                              {DEEP_SURFACE, z + 1, z + 1, .5f, 0}};
            for (Capture *capture : {&memory, &disk})
              capture->record_events(x, 0, sample, events, 2);
          }
          memory.set_population(x, 0, first + 8);
          disk.set_population(x, 0, first + 8);
        }
      }
      check(memory.finalize() && disk.finalize());
      for (int x = 0; x < 257; ++x) {
        for (int sample = 0; sample < 128; ++sample) {
          const auto a = memory.events(x, 0, sample), b = disk.events(x, 0, sample);
          check(a.size() == 2 && b.size() == 2);
          for (size_t i = 0; i < a.size(); ++i)
            check(a[i].depth == b[i].depth && a[i].alpha == b[i].alpha);
        }
      }
      check(disk.finalize());
      disk.record_events(0, 0, 0, nullptr, 0);
      check(!disk.finalize());
    }
    std::cout
        << "Capture coverage, lifecycle, concurrent writes, bounds and budget checks passed\n";
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
