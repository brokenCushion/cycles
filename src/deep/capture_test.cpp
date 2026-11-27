/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include "deep/osl_features.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <numeric>
#include <random>
#include <thread>
#include <type_traits>

namespace ccl::deep {
struct CaptureTestAccess {
  static std::vector<KernelDeepEvent> events(const Capture &capture, int x, int y, int sample)
  {
    const size_t index = capture.record_index(size_t(y) * capture.width_ + x, sample);
    KernelDeepResult result{};
    capture.read_record(index, result, nullptr);
    std::vector<KernelDeepEvent> events(result.count);
    capture.read_record(index, result, events.data());
    return events;
  }
  /* Exact I/O accounting fixtures use raw (uncompressed) spill records. */
  static void raw_spill(Capture &capture)
  {
    capture.compression_ = 0;
  }
  static int compression(const Capture &capture)
  {
    return capture.compression_;
  }
  /* Codec coverage independent of the budget rule that enables compression. */
  static void compress_spill(Capture &capture, const int level)
  {
    capture.compression_ = level;
  }
};
}
using ccl::deep::Capture;
using ccl::deep::CaptureTestAccess;
int run_reducer_tests(); /* reducer_test.cpp: online volume reduction. */
int run_surface_reducer_tests(); /* surface_reducer_test.cpp: online surface accumulation. */
static_assert(std::is_trivially_copyable_v<KernelDeepEvent>);
static_assert(std::is_standard_layout_v<KernelDeepEvent>);
static_assert(offsetof(KernelDeepEvent, front) == 4);
static_assert(std::is_trivially_copyable_v<KernelDeepRecord>);
static_assert(std::is_standard_layout_v<KernelDeepRecord>);
static void check_impl(bool condition, int line)
{
  if (!condition)
    throw std::runtime_error("capture test failed at line " + std::to_string(line));
}
#define check(condition) check_impl((condition), __LINE__)
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
constexpr size_t single_record_bytes = sizeof(KernelDeepResult) + sizeof(KernelDeepEvent);
constexpr size_t chain_bytes = 2 * (sizeof(KernelDeepResult) + 2 * sizeof(KernelDeepEvent));
int main()
{
  const auto osl = ccl::deep::osl_features(
      "temp closure color c\nconst string label \"trace\"\ncode main\n"
      "# trace in a comment\n# c = transparent()\nclosure c label\nmul c c weight\nadd c c c\nend\n");
  check(osl.unsupported.empty() && osl.components == 1 && osl.muls == 1 && osl.adds == 1);
  check(ccl::deep::osl_features("code main\ntrace hit P I\n").unsupported == "trace");
  check(ccl::deep::osl_features("code main\nwhile flag\n").loop);
  check(!ccl::deep::osl_features("# while\ncode main\nassign x y\n").loop);
  check(!ccl::deep::osl_features("code main\nfunctioncall clamp 3\nmin t a b\nmax c t d\n").loop);
  check(!ccl::deep::osl_features("temp closure color c\ncode main\nmix c a b\n").unsupported.empty());
  static_assert(sizeof(ccl::deep::SurfaceEvent) == 24);
  static_assert(sizeof(ccl::deep::IntervalSample) == 32);
  {
    Capture replay(1,3,1,4*1024*1024,1,true);
    replay.retain_for_validation = true;
    const KernelDeepEvent event{deep_event_pack(DEEP_SURFACE,7),2,2,.5f,1};
    for (int y=0;y<3;++y) replay.record_events(0,y,0,&event,1);
    check(replay.finalize());
    for (int pass=0;pass<2;++pass)
      for (int y=2;y>=0;--y) {
        replay.begin_export_row(y);
        const auto curve=replay.reconstruct_pixel(0,y);
        check(curve.size()==1 && curve[0].alpha==.5 && curve[0].facing==1);
        replay.end_export_row(y);
      }
  }
  for (const bool spill : {false, true})
    for (const int facing : {-1, 0, 1}) {
      Capture capture(1, 1, 1, 4*1024*1024, 1, spill);
      const KernelDeepEvent event{deep_event_pack(DEEP_SURFACE, 7), 2, 2, .5f, float(facing)};
      capture.record_events(0,0,0,&event,1);
      check(capture.finalize());
      const auto raw=capture.events(0,0,0);
      check(raw.size() == 1 && raw[0].object == 7 && raw[0].facing == facing);
      const auto curve=capture.reconstruct_pixel(0,0);
      check(curve.size() == 1 && curve[0].object == 7 && curve[0].facing == facing);
    }
  try {
    check(deep_object_count_valid(1ull << 30));
    check(!deep_object_count_valid((1ull << 30) + 1));
    for (const bool spill : {false, true}) {
      /* Reused scratch sees growing, shrinking and empty records. Unused
       * density slots must never leak from a previous cubic sample. */
      Capture capture(1, 1, 4, 32 * 1024 * 1024, 3, spill, false, true, true);
      const KernelDeepEvent events[] = {{deep_event_pack(DEEP_VOLUME_CUBIC, 7), 1, 2, 0, 0},
                                       {deep_event_pack(DEEP_SURFACE, 31), 3, 3, .25f, 0},
                                       {deep_event_pack(DEEP_VOLUME, 42), 4, 5, 0, .2f}};
      const KernelDeepDensity density[] = {{{.1f,.1f,.1f,.1f},1,2}, {}, {}};
      capture.record_sample(0,0,0,{DEEP_COMPLETE,3,DEEP_ERROR_NONE},events,density);
      capture.record_events(0,0,1,nullptr,0);
      capture.record_events(0,0,2,events+2,1);
      capture.record_sample(0,0,3,{DEEP_COMPLETE,1,DEEP_ERROR_NONE},events,density);
      check(capture.finalize());
      auto check_objects = [&] {
        const int counts[] = {3, 0, 1, 1};
        for (int sample = 0; sample < 4; ++sample) {
          const auto stored = CaptureTestAccess::events(capture, 0, 0, sample);
          check(stored.size() == counts[sample]);
          for (size_t i = 0; i < stored.size(); ++i)
            check(deep_event_object(stored[i]) == deep_event_object(events[sample == 2 ? 2 : i]));
        }
      };
      check_objects(); // Memory or direct spill reads.
      if (spill) capture.begin_export_row(0);
      check_objects(); // Staged reads use the same complete event payload.
      const double expected[] = {.75*std::exp(-double(.1f)-double(.2f)),1,
                                 std::exp(-double(.2f)),std::exp(-double(.1f))};
      for (int sample : {3,1,0,2,1,3,0}) {
        const auto ray = capture.volume_sample(0,0,sample);
        double transmittance = 1;
        for (const auto &interval : ray.intervals)
          transmittance *= std::exp(-interval.optical_depth);
        for (const auto &surface : ray.camera.events)
          transmittance *= 1-surface.alpha;
        check(std::abs(transmittance-expected[sample]) < 1e-12);
      }
      if (spill) capture.end_export_row(0);
    }
    {
      Capture memory(7, 130, 17, 16 * 1024 * 1024, 2, false, true);
      Capture disk(7, 130, 17, 32 * 1024 * 1024, 2, true, true);
      std::vector<int> identities(7 * 130 * 17);
      std::iota(identities.begin(), identities.end(), 0);
      std::mt19937 random(42);
      std::shuffle(identities.begin(), identities.end(), random);
      for (int identity : identities) {
        const int pixel = identity / 17, sample = identity % 17;
        const int x = pixel % 7, y = pixel / 7, population = 1 + pixel % 17;
        if (sample >= population) continue;
        const float z = 1 + float(identity) / 1024;
        const KernelDeepEvent events[] = {{DEEP_SURFACE, z, z, .25f, 0}};
        for (Capture *capture : {&memory, &disk})
          capture->record_events(x, y, sample, events, identity % 5 ? 1 : 0);
      }
      for (int y = 0; y < 130; ++y)
        for (int x = 0; x < 7; ++x)
          for (Capture *capture : {&memory, &disk})
            capture->set_population(x, y, 1 + (y * 7 + x) % 17);
      check(memory.finalize() && disk.finalize());
      rejects([&] { disk.begin_export_row(0); });
      for (int y = 129; y >= 0; --y) {
        disk.begin_export_row(y);
        for (int x = 0; x < 7; ++x) {
          const auto a = memory.reconstruct_pixel(x, y), b = disk.reconstruct_pixel(x, y);
          check(a.size() == b.size());
          for (size_t i = 0; i < a.size(); ++i)
            check(a[i].depth == b[i].depth && a[i].alpha == b[i].alpha);
        }
        disk.end_export_row(y);
      }
      const auto io = disk.spill_statistics();
      std::cout << "Multi-band read bytes " << io.read_bytes << ", stored " << io.file_bytes << '\n';
      check(io.read_bytes <= io.file_bytes * 1.25);
      check(disk.finalize());
      rejects([&] { disk.begin_export_row(0); });
      Capture duplicate(1, 130, 1, 32 * 1024 * 1024, 0, true);
      duplicate.record(0, 129, 0, 1);
      duplicate.record(0, 0, 0, 1);
      duplicate.record(0, 129, 0, 1);
      check(!duplicate.finalize());
    }
    {
      /* A three-row band exceeds staging; each row fits. Re-bucket once. */
      Capture disk(4, 130, 8, 64 * 1024 * 1024, 8192, true, false, true, true);
      std::vector<KernelDeepEvent> events(8192);
      for (size_t i = 0; i < events.size(); ++i)
        events[i] = {deep_event_pack(DEEP_VOLUME, int(i % 64)), float(i + 1), float(i + 2), 0, .001f};
      for (int y = 0; y < 130; ++y)
        for (int x = 0; x < 4; ++x)
          for (int sample = 0; sample < 8; ++sample)
            disk.record_events(x, y, sample, events.data(), y < 3 ? 8192 : 0);
      check(disk.finalize());
      for (int y = 129; y >= 0; --y) {
        disk.begin_export_row(y);
        for (int x = 0; x < 4; ++x)
          for (int sample = 0; sample < 8; ++sample) {
            const auto raw = CaptureTestAccess::events(disk, x, y, sample);
            check(raw.size() == (y < 3 ? 8192 : 0));
            for (size_t i = 0; i < raw.size(); ++i)
              check(deep_event_object(raw[i]) == int(i % 64));
            const auto ray = disk.volume_sample(x, y, sample);
            check(ray.intervals.size() == (y < 3 ? 8192 : 0));
            if (y < 3) {
              check(ray.intervals.front().front == 1 && ray.intervals.back().back == 8193);
              check(ray.intervals.back().optical_depth == double(.001f));
            }
          }
        disk.end_export_row(y);
      }
      const auto io = disk.spill_statistics();
      std::cout << "Re-bucket read bytes " << io.read_bytes << ", stored " << io.file_bytes << '\n';
    }
    {
      /* Wider rows share a bounded total capacity instead of reserving every
       * pixel's worst case. Leave 32 MiB for the production GPU staging pool. */
      Capture production(664, 625, 4, size_t(1024) * 1024 * 1024,
                         4096, true, false, true, true);
      Capture wide(1920, 1, 4, size_t(1024 - 32) * 1024 * 1024,
                   4096, true, false, true, true);
      check(wide.volume_row_sample_limit() >= wide.reconstruction_limit());
      check(wide.volume_row_sample_limit() < 1920 * wide.reconstruction_limit());
      Capture limited(664, 1, 4, size_t(512) * 1024 * 1024,
                      4096, true, false, true, true);
      check(limited.volume_row_sample_limit() < production.volume_row_sample_limit());
      Capture parallel(1920, 1, 1024, size_t(1024 - 32) * 1024 * 1024,
                       8192, true, true, true, true, 8);
      check(parallel.volume_export_workers() == 4);
      Capture serial_rows(1920, 1, 1024, size_t(1024 - 32) * 1024 * 1024,
                          8192, true, true, true, true);
      check(parallel.volume_row_sample_limit() == serial_rows.volume_row_sample_limit());
      Capture landscape(1175, 1, 1024, size_t(1024 - 32) * 1024 * 1024,
                        8192, true, true, true, true, 24);
      check(landscape.volume_export_workers() == 4);
      Capture hardware_budget(1175, 500, 1024, size_t(8192 - 32) * 1024 * 1024,
                              8192, true, true, true, true, 24);
      check(hardware_budget.volume_export_workers() == 24);
      Capture fallback(12, 1, 1024, size_t(64) * 1024 * 1024,
                       4, true, true, true, true, 24);
      check(fallback.volume_export_workers() == 1);
      rejects([] { Capture c(1, 1, 1, 1024, 0, false, false, false, false, 0); });
      rejects([] { Capture too_small(664, 625, 4, size_t(8) * 1024 * 1024,
                                     4096, true, false, true, true); });
      Capture expanded(1, 1, 1, size_t(64) * 1024 * 1024,
                       8192, true, false, true, true);
      std::vector<KernelDeepEvent> events(8192);
      for (size_t i = 0; i < events.size(); ++i)
        events[i] = {DEEP_VOLUME, float(i + 1), float(i + 2), 0, .0001f};
      expanded.record_events(0, 0, 0, events.data(), int(events.size()));
      check(expanded.finalize());
      const auto curve = expanded.reconstruct_volume_pixel(0, 0);
      check(std::abs(interval_transmittance(curve, 8193) -
                     std::exp(-8192 * double(.0001f))) < 1e-12);
      /* Two rays with offset boundaries and varying density need more than
       * the 16384 budget: the pixel is merged to fit and counted, not failed. */
      Capture over_budget(1, 1, 2, size_t(64) * 1024 * 1024, 8192, true, false, true, true);
      std::vector<KernelDeepEvent> first(events.size()), offset(events.size());
      double tau_first = 0, tau_offset = 0;
      for (size_t i = 0; i < offset.size(); ++i) {
        first[i] = {DEEP_VOLUME, float(i + 1), float(i + 2), 0, .0001f * float(1 + i % 7)};
        offset[i] = {DEEP_VOLUME, float(i) + 1.5f, float(i) + 2.5f, 0,
                     .0001f * float(1 + (i + 3) % 5)};
        tau_first += double(first[i].optical_depth);
        tau_offset += double(offset[i].optical_depth);
      }
      over_budget.record_events(0, 0, 0, first.data(), int(first.size()));
      over_budget.record_events(0, 0, 1, offset.data(), int(offset.size()));
      check(over_budget.finalize());
      check(over_budget.budget_merged_pixels() == 0);
      const auto fitted = over_budget.reconstruct_volume_pixel(0, 0);
      check(fitted.size() <= over_budget.reconstruction_limit());
      check(over_budget.budget_merged_pixels() == 1);
      check(std::abs(interval_transmittance(fitted, 8194) -
                     .5 * (std::exp(-tau_first) + std::exp(-tau_offset))) < 1e-7);
      over_budget.reconstruct_volume_pixel(0, 0);
      check(over_budget.budget_merged_pixels() == 1);
      check(expanded.budget_merged_pixels() == 0);
      /* Streaming balanced averages fit dense accepted populations without
       * retaining every ray at once, even with a high adaptive maximum. */
      Capture sparse(2, 1, 1024, 64 * 1024 * 1024, 8192, true, true, true, true);
      for (int i = 0; i < 16; ++i)
        sparse.record_events(0, 0, i, events.data(), 1);
      sparse.set_population(0, 0, 16);
      check(!sparse.reconstruct_volume_pixel(0, 0).empty());
      for (auto &event : events)
        event.back = event.front + .5f;  // Empty gaps must survive ray reduction.
      for (int i = 0; i < 16; ++i)
        sparse.record_events(1, 0, i, events.data(), int(events.size()));
      sparse.set_population(1, 0, 16);
      check(sparse.finalize());
      const auto streamed = sparse.reconstruct_volume_pixel(1, 0);
      check(std::abs(interval_transmittance(streamed, 8193) -
                     std::exp(-8192 * double(.0001f))) < 1e-7);
      Capture mixed(3, 1, 1024, 64 * 1024 * 1024, 4, true, true, true, true);
      const int counts[] = {9, 65, 1024};
      for (int x = 0; x < 3; ++x) {
        for (int i = 0; i < counts[x]; ++i) {
          const KernelDeepEvent ray[] = {
              {DEEP_VOLUME, 2, 8, 0, .05f + (i % 11) * .01f},
              {DEEP_SURFACE, 4 + (i % 4) * .01f, 4 + (i % 4) * .01f, .3f, 0}};
          mixed.record_events(x, 0, i, ray, i % 5 ? (i % 2 ? 2 : 1) : 0);
        }
        mixed.set_population(x, 0, counts[x]);
        const auto result = mixed.reconstruct_volume_pixel(x, 0);
        for (int probe = 0; probe <= 2000; ++probe) {
          const double z = 1 + 9.0 * probe / 2000;
          double expected = 0;
          for (int i = 0; i < counts[x]; ++i) {
            double t = 1;
            if (i % 5) {
              const double fraction = std::max(0.0, std::min(1.0, (z - 2) / 6));
              t = std::exp(-double(.05f + (i % 11) * .01f) * fraction);
              if (i % 2 && z >= double(4 + (i % 4) * .01f))
                t *= 1 - double(.3f);
            }
            expected += t;
          }
          check(std::abs(interval_transmittance(result, z) - expected / counts[x]) < 1.51e-7);
        }
      }
      check(mixed.finalize());
      Capture batched(1920, 1, 1024, 512 * 1024 * 1024, 0, true, true);
      for (int x = 0; x < 1920; ++x) {
        batched.record(x, 0, 0, float(x + 1));
        batched.set_population(x, 0, 1);
      }
      check(batched.finalize());
      const auto io = batched.spill_statistics();
      check(io.read_bytes < 2 * 1024 * 1024);
      for (int x = 0; x < 1920; ++x)
        check(batched.value(x, 0, 0) == float(x + 1));
      Capture occluded(1, 1, 1, 32 * 1024 * 1024, 4, true, false, true, true);
      const KernelDeepEvent hidden[] = {
          {DEEP_SURFACE, 3, 3, 1, 0}, {DEEP_VOLUME, 1, 5, 0, 2},
          {DEEP_VOLUME, 6, 7, 0, 1}, {DEEP_SURFACE, 8, 8, .5f, 0}};
      occluded.record_events(0, 0, 0, hidden, 4);
      const auto visible = occluded.volume_sample(0, 0, 0);
      check(visible.camera.events.size() == 1 && visible.intervals.size() == 1);
      check(visible.intervals[0].back == 3 && visible.intervals[0].optical_depth == 1);
      const ccl::deep::VolumeCameraSample reference{{0, 1, true, {{3, 1}, {8, .5}}},
                                                   {{1, 5, 2}, {6, 7, 1}}};
      const auto full = ccl::deep::reconstruct_volume({reference});
      const auto clipped = occluded.reconstruct_volume_pixel(0, 0);
      check(interval_curve_error(full, clipped) < 1e-12);
    }
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
      std::vector<IntervalSample> rounded;
      for (const auto &interval : dense)
        rounded.push_back({double(float(interval.front)), double(float(interval.back)),
                           double(float(interval.alpha))});
      check(interval_curve_error(dense, rounded) < 8.1e-7);
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
        Capture thin(1, 1, 1, 16 * 1024 * 1024, 1, spill, false, true, true);
        CaptureTestAccess::raw_spill(thin);
        const auto initial_io = thin.spill_statistics();
        /* The sparse index is not zero-filled up front. */
        const uint64_t zero_fill = spill && !thin.spill_index_sparse() ? initial_io.file_bytes : 0;
        check(initial_io.write_bytes == zero_fill);
        const KernelDeepEvent thin_event{DEEP_VOLUME_CUBIC, 1000, 1000, 0, 0};
        const KernelDeepDensity thin_density{{1e-8f, 1e-8f, 1e-8f, 1e-8f}, 1000, 1000 + 1e-8};
        thin.record_sample(0, 0, 0, {DEEP_COMPLETE, 1, DEEP_ERROR_NONE}, &thin_event, &thin_density);
        check(thin.finalize());
        const auto captured_io = thin.spill_statistics();
        const auto thin_sample = thin.volume_sample(0, 0, 0);
        const auto read_io = thin.spill_statistics();
        thin.volume_sample(0, 0, 0);
        check(thin.spill_statistics().read_bytes == read_io.read_bytes);  // Cached reread.
        /* One completion header is appended beside each event payload. For
         * this one-record index its size equals the initial file size. */
        const uint64_t payload = sizeof(thin_event) + sizeof(thin_density) + initial_io.file_bytes;
        check(captured_io.file_bytes == initial_io.file_bytes + (spill ? payload : 0));
        check(captured_io.write_bytes == (spill ? zero_fill + initial_io.file_bytes + payload : 0));
        check(read_io.read_bytes == captured_io.read_bytes + (spill ? payload : 0));
        check(thin_sample.intervals.size() == 1);
        check(thin_sample.intervals[0].front == thin_density.front &&
              thin_sample.intervals[0].back == thin_density.back);
        Capture varying(1, 1, 2, 16 * 1024 * 1024, 2, spill, false, true, true);
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
        Capture missing(1, 1, 1, 16 * 1024 * 1024, 65, spill, false, true, true);
        missing.record_sample(0, 0, 0, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE}, events.data());
        check(!missing.finalize());
        Capture invalid(1, 1, 1, 16 * 1024 * 1024, 65, spill, false, true, true);
        density[1].optical_depth[2] = -1;
        invalid.record_sample(0, 0, 0, {DEEP_COMPLETE, 65, DEEP_ERROR_NONE},
                               events.data(), density.data());
        check(!invalid.finalize());
      }
    }
    {
      /* Stage 1 spill safety: explicit directory, delete-on-close files, lossless
       * compression round trip, first-error detail and budget numbers. */
      namespace fs = std::filesystem;
      const fs::path root = fs::temp_directory_path() / "cycles_deep_capture_test_spill";
      fs::remove_all(root);
      fs::create_directories(root);
      const auto spill_files = [&] {
        size_t n = 0;
        for (const auto &entry : fs::directory_iterator(root))
          n += entry.path().filename().string().rfind("cycles-deep-", 0) == 0;
        return n;
      };
      {
        Capture directed(3, 5, 2, 32 * 1024 * 1024, 2, true, false, false, false, 1, 0, 0, false,
                         root.string());
        check(directed.spill_directory() == root.string());
        check(spill_files() == 2 * 5);  // One index and one stream file per one-row band.
        /* Delete-on-close files are not visible to other openers. */
        check(spill_files() > 0);
      }
      check(spill_files() == 0);
      rejects([&] {
        Capture missing(1, 1, 1, 4 * 1024 * 1024, 0, true, false, false, false, 1, 0, 0, false,
                        (root / "does-not-exist").string());
      });
      /* Compressed and raw spill must reconstruct byte-identically, including
       * pre-export random reads, re-bucketed rows and adaptive populations. */
      for (const bool rebucket : {false, true}) {
        if (!Capture::spill_compression_supported())
          break;
        /* 256 MiB enables compression by the budget rule; 48 MiB forces a
         * re-bucket of the heavy first band (codec forced on for coverage). */
        const size_t budget = rebucket ? size_t(48) * 1024 * 1024 : size_t(256) * 1024 * 1024;
        const int width = rebucket ? 80 : 9;
        Capture raw(width, 70, 8, budget, 200, true, true, true, true, 1, 0, 0, false,
                    root.string());
        Capture packed(width, 70, 8, budget, 200, true, true, true, true, 1, 0, 0, false,
                       root.string());
        CaptureTestAccess::raw_spill(raw);
        check(rebucket ? CaptureTestAccess::compression(packed) == 0 :
                         CaptureTestAccess::compression(packed) > 0);
        CaptureTestAccess::compress_spill(packed, 1);
        std::mt19937 random(rebucket ? 7 : 3);
        std::vector<KernelDeepEvent> events(200);
        std::vector<KernelDeepDensity> density(200);
        for (int y = 0; y < 70; ++y)
          for (int x = 0; x < width; ++x) {
            const int population = y < 2 && rebucket ? 8 : 1 + (x + 3 * y) % 8;
            for (int sample = 0; sample < population; ++sample) {
              const int count = y < 2 ? (rebucket ? 150 + int(random() % 50) : int(random() % 200)) :
                                        int(random() % 40);
              float z = 1;
              for (int i = 0; i < count; ++i) {
                const float length = .01f + float(random() % 1000) / 4000;
                if (i % 10 == 0) {
                  events[i] = {deep_event_pack(DEEP_VOLUME_CUBIC, i % 5), z, z + length, 0, 0};
                  density[i] = {{.01f * (i % 7), .02f, .003f * (i % 3), .01f}, double(z),
                                double(z + length)};
                }
                else
                  events[i] = {deep_event_pack(DEEP_VOLUME, i % 5), z, z + length, 0,
                               .001f * float(random() % 100)};
                z += length + .001f;
              }
              for (Capture *capture : {&raw, &packed})
                capture->record_sample(x, y, sample, {DEEP_COMPLETE, unsigned(count), DEEP_ERROR_NONE},
                                       events.data(), density.data());
            }
            for (Capture *capture : {&raw, &packed})
              capture->set_population(x, y, population);
          }
        /* Pre-export random reads go through the compressed block cache. */
        for (const int y : {69, 0, 35, 1})
          for (const int x : {width - 1, 0, 4})
            for (const int sample : {0, 2 + x % 3}) {
              if (sample >= raw.population(x, y))
                continue;
              const auto a = CaptureTestAccess::events(raw, x, y, sample);
              const auto b = CaptureTestAccess::events(packed, x, y, sample);
              check(a.size() == b.size() &&
                    std::equal(a.begin(), a.end(), b.begin(), [](const auto &l, const auto &r) {
                      return std::memcmp(&l, &r, sizeof(l)) == 0;
                    }));
            }
        check(raw.finalize() && packed.finalize());
        for (int y = 69; y >= 0; --y) {
          raw.begin_export_row(y);
          packed.begin_export_row(y);
          for (int x = 0; x < width; ++x) {
            /* Every staged payload is byte-identical; a subset also reconstructs. */
            for (int sample = 0; sample < raw.population(x, y); ++sample) {
              const auto a = CaptureTestAccess::events(raw, x, y, sample);
              const auto b = CaptureTestAccess::events(packed, x, y, sample);
              check(a.size() == b.size() &&
                    (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0));
            }
            if (x % 7 || y % 3)
              continue;
            const auto a = raw.reconstruct_volume_pixel(x, y);
            const auto b = packed.reconstruct_volume_pixel(x, y);
            check(a.size() == b.size() &&
                  (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0));
          }
          raw.end_export_row(y);
          packed.end_export_row(y);
        }
        const auto a = raw.spill_statistics(), b = packed.spill_statistics();
        check(a.event_raw_bytes == b.event_raw_bytes && a.event_file_bytes == a.event_raw_bytes);
        check(b.event_file_bytes < b.event_raw_bytes);
        if (rebucket)  // Re-bucketing transfers more than the stored stream once.
          check(b.read_bytes > b.event_file_bytes);
        std::cout << "Spill compression " << (rebucket ? "(re-bucketed) " : "") << "raw "
                  << b.event_raw_bytes << " stored " << b.event_file_bytes << " ratio "
                  << double(b.event_raw_bytes) / double(b.event_file_bytes) << '\n';
      }
      check(spill_files() == 0);
      {
        /* First error wins and keeps its cause; the capture reports failure early. */
        Capture duplicate(2, 1, 2, 4 * 1024 * 1024, 0, true, false, false, false, 1, 0, 0, false,
                          root.string());
        check(!duplicate.failed());
        duplicate.record(1, 0, 1, 1);
        duplicate.record(1, 0, 1, 1);
        duplicate.record(5, 0, 0, 1);  // Later errors do not replace the first.
        check(duplicate.failed());
        const std::string message = duplicate.error_message();
        check(message.find("camera sample recorded more than once") == 0 &&
              message.find("pixel 1,0, sample 1") != std::string::npos);
      }
      {
        /* Budget failures state the bytes and the minimum budget that fits. */
        std::string message;
        try {
          Capture tight(512, 4, 64, size_t(16) * 1024 * 1024, 64, true, false, false, false, 1, 0, 0,
                        false, root.string());
        }
        catch (const std::exception &error) {
          message = error.what();
        }
        check(message.find("needs ") != std::string::npos &&
              message.find("minimum deep_memory_mb ") != std::string::npos);
        const int minimum = std::stoi(message.substr(message.rfind(' ') + 1));
        Capture fits(512, 4, 64, size_t(minimum) * 1024 * 1024, 64, true, false, false, false, 1, 0,
                     0, false, root.string());
        rejects([&] {
          Capture below(512, 4, 64, size_t(minimum - 1) * 1024 * 1024, 64, true, false, false, false,
                        1, 0, 0, false, root.string());
        });
        std::cout << "Budget message: " << message << '\n';
        /* A surface row needing more than the staging eighth fails at export with numbers. */
        Capture row(64, 1, 128, 82 * 1024 * 1024, 64, true, false, false, false, 1, 0, 0, false,
                    root.string());
        std::vector<KernelDeepEvent> deep(64);
        for (int i = 0; i < 64; ++i)
          deep[i] = {DEEP_SURFACE, float(i + 1), float(i + 1), .5f, 0};
        for (int x = 0; x < 64; ++x)
          for (int sample = 0; sample < 128; ++sample)
            row.record_events(x, 0, sample, deep.data(), 64);
        check(row.finalize());
        std::string export_message;
        try {
          row.begin_export_row(0);
        }
        catch (const std::exception &error) {
          export_message = error.what();
        }
        std::cout << "Export message: " << export_message << '\n';
        check(export_message.find("Deep export row exceeds --deep-memory-mb budget: largest row y=0") == 0 &&
              export_message.find("staging limit 10747904 bytes") != std::string::npos);
        const int row_minimum = std::stoi(export_message.substr(export_message.rfind(' ') + 1));
        Capture larger(64, 1, 128, size_t(row_minimum) * 1024 * 1024, 64, true, false, false, false,
                       1, 0, 0, false, root.string());
        for (int x = 0; x < 64; ++x)
          for (int sample = 0; sample < 128; ++sample)
            larger.record_events(x, 0, sample, deep.data(), 64);
        check(larger.finalize());
        larger.begin_export_row(0);
        larger.end_export_row(0);
      }
      {
        /* A quota below the reserve fails between passes with both numbers. */
#ifdef _WIN32
        _putenv_s("CYCLES_DEEP_SPILL_MAX_MB", "200");
#else
        setenv("CYCLES_DEEP_SPILL_MAX_MB", "200", 1);
#endif
        Capture quota(4, 4, 4, 32 * 1024 * 1024, 0, true, false, false, false, 1, 0, 0, false,
                      root.string());
#ifdef _WIN32
        _putenv_s("CYCLES_DEEP_SPILL_MAX_MB", "");
#else
        unsetenv("CYCLES_DEEP_SPILL_MAX_MB");
#endif
        quota.record(0, 0, 0, 1);
        check(quota.check_spill_space().empty());
        check(quota.failed());
        const std::string message = quota.error_message();
        std::cout << "Quota message: " << message << '\n';
        check(message.find("deep spill disk space is insufficient") == 0 &&
              message.find("quota 209715200 bytes") != std::string::npos);
        /* A non-adaptive projection that cannot fit fails (sample counts as numbers). */
        Capture fits(4, 4, 4, 32 * 1024 * 1024, 0, true, false, false, false, 1, 0, 0, false,
                     root.string());
        fits.record(0, 0, 0, 1);
        check(fits.check_spill_space().empty() && !fits.failed());
      }
      check(spill_files() == 0);
      fs::remove_all(root);
    }
    rejects([] { Capture c(-1, 2, 1, 1024); });
    rejects([] { Capture c(1, 2, 0, 1024); });
    rejects([] { Capture c(1, 1, 4097, 1024); });
    rejects([] { Capture c(1, 1, 1, 1024, 0, false, false, false, false, 1, 0, -1); });
    rejects([] { Capture c(1, 1, 1, 1024, 0, false, false, false, false, 1, 0, 2); });
    for (const bool spill : {false, true}) {
      Capture limited(1, 1, 2, 64 * 1024 * 1024, 0, spill, true, false, false, 1, 0, 2);
      limited.record(0, 0, 0, 1);
      limited.record(0, 0, 1, 0); // Misses still contribute to the prefix population.
      limited.set_population(0, 0, 2);
      check(limited.finalize() && limited.sample_limit() == 2 && limited.population(0, 0) == 2);
      const auto pixel = limited.reconstruct_pixel(0, 0);
      check(pixel.size() == 1 && pixel[0].alpha == .5);
    }
    rejects([] { Capture c(1000, 1000, 1000, 1024); });
    rejects([] { Capture c(INT32_MAX, INT32_MAX, 4096, SIZE_MAX); });
    Capture capture(2, 1, 4, 8 * single_record_bytes);
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
      Capture c(1, 1, 1, single_record_bytes);
      c.record(0, 0, 0, invalid);
      check(!c.finalize());
    }
    Capture bounds(1, 1, 1, single_record_bytes);
    bounds.record(1, 0, 0, 0);
    check(!bounds.finalize());
    Capture sample(1, 1, 1, single_record_bytes);
    sample.record(0, 0, 1, 0);
    check(!sample.finalize());
    Capture state(1, 1, 1, single_record_bytes);
    state.fail();
    state.record(0, 0, 0, 0);
    check(!state.finalize());
    Capture miss(1, 1, 1, single_record_bytes);
    miss.record(0, 0, 0, 0);
    check(miss.finalize());
    check(miss.reconstruct_pixel(0, 0).empty());
    Capture chains(1, 1, 2, chain_bytes, 2);
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
    rejects([] { Capture c(1, 1, 2, chain_bytes - 1, 2); });
    rejects([] { Capture c(1, 1, 1, 1000, 65); });
    for (const float alpha : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
      Capture c(1, 1, 1, single_record_bytes, 1);
      const KernelDeepEvent event[] = {{DEEP_SURFACE, 2, 2, alpha, 0}};
      c.record_events(0, 0, 0, event, 1);
      check(!c.finalize());
    }
    /* More events than the capacity merge instead of failing, as the kernel
     * does: only surfaces, so the nearest-depth pair of one object merges at
     * the nearer depth with alpha 1-(1-a)(1-b). */
    Capture overflow(1, 1, 1, single_record_bytes, 1);
    overflow.record_events(0, 0, 0, stack, 2);
    check(overflow.finalize());
    check(overflow.event_merged_samples == 1);
    p = overflow.reconstruct_pixel(0, 0);
    check(p.size() == 1 && p[0].depth == 2 && p[0].alpha == .625);
    /* Invalid input is rejected before merging, never merged into validity. */
    const KernelDeepEvent invalid_stack[] = {{DEEP_SURFACE, 2, 2, 1.1f, 0},
                                             {DEEP_SURFACE, 8, 8, 2, 0}};
    Capture invalid_overflow(1, 1, 1, single_record_bytes, 1);
    invalid_overflow.record_events(0, 0, 0, invalid_stack, 2);
    check(!invalid_overflow.finalize());
    check(invalid_overflow.event_merged_samples == 0);
    {
      /* Volume events past the capacity: contiguous events of one object with
       * the most similar extinction merge first, keeping their summed optical
       * depth, so transmittance at every kept boundary is exact. Under the
       * capacity the same events are stored unchanged. */
      const KernelDeepEvent spans[] = {{deep_event_pack(DEEP_VOLUME, 3), 2, 3, 0, .1f},
                                       {deep_event_pack(DEEP_VOLUME, 3), 3, 4, 0, .1f},
                                       {deep_event_pack(DEEP_VOLUME, 3), 4, 5, 0, 2},
                                       {deep_event_pack(DEEP_VOLUME, 3), 5, 6, 0, 2.1f}};
      Capture fits(1, 1, 1, 8 * 1024 * 1024, 4, false, false, true);
      fits.record_events(0, 0, 0, spans, 4);
      check(fits.finalize() && fits.event_merged_samples == 0);
      const auto kept = fits.volume_sample(0, 0, 0).intervals;
      check(kept.size() == 4);
      for (size_t i = 0; i < kept.size(); ++i)
        check(kept[i].front == spans[i].front && kept[i].back == spans[i].back &&
              kept[i].optical_depth == double(spans[i].optical_depth) && kept[i].object == 3);
      Capture merged(1, 1, 1, 8 * 1024 * 1024, 2, false, false, true);
      merged.record_events(0, 0, 0, spans, 4);
      check(merged.finalize() && merged.event_merged_samples == 1);
      const auto intervals = merged.volume_sample(0, 0, 0).intervals;
      check(intervals.size() == 2);
      check(intervals[0].front == 2 && intervals[0].back == 4 && intervals[0].object == 3);
      check(intervals[1].front == 4 && intervals[1].back == 6 && intervals[1].object == 3);
      check(std::abs(intervals[0].optical_depth - (double(.1f) + double(.1f))) < 1e-7);
      check(std::abs(intervals[1].optical_depth - (2 + double(2.1f))) < 1e-6);
      /* Different objects and gaps never merge; surfaces then merge instead. */
      const KernelDeepEvent apart[] = {{deep_event_pack(DEEP_VOLUME, 1), 2, 3, 0, .1f},
                                       {deep_event_pack(DEEP_VOLUME, 2), 3, 4, 0, .1f},
                                       {deep_event_pack(DEEP_VOLUME, 2), 5, 6, 0, .1f},
                                       {deep_event_pack(DEEP_SURFACE, 4), 7, 7, .5f, 1},
                                       {deep_event_pack(DEEP_SURFACE, 4), 7.5f, 7.5f, .5f, -1}};
      Capture surfaces(1, 1, 1, 8 * 1024 * 1024, 4, false, false, true);
      surfaces.record_events(0, 0, 0, apart, 5);
      check(surfaces.finalize() && surfaces.event_merged_samples == 1);
      const auto sample = surfaces.volume_sample(0, 0, 0);
      check(sample.intervals.size() == 3 && sample.camera.events.size() == 1);
      check(sample.camera.events[0].depth == 7 && sample.camera.events[0].alpha == .75 &&
            sample.camera.events[0].facing == 0);
      Capture stuck(1, 1, 1, 8 * 1024 * 1024, 2, false, false, true);
      stuck.record_events(0, 0, 0, apart, 3);
      check(!stuck.finalize() && stuck.event_merged_samples == 0);
    }
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
      for (const KernelDeepEvent invalid : {KernelDeepEvent{deep_event_pack(DEEP_SURFACE, -2), 2, 2, .5f, 0},
                                            KernelDeepEvent{DEEP_SURFACE, 2, 3, .5f, 0},
                                            KernelDeepEvent{DEEP_SURFACE, 2, 2, .5f, 2},
                                            KernelDeepEvent{DEEP_VOLUME, 2, 2, 0, 1},
                                            KernelDeepEvent{DEEP_VOLUME, 2, 3, .5f, 1},
                                            KernelDeepEvent{KernelDeepEventKind(99), 2, 3, 0, 1}})
      {
        Capture c(1, 1, 1, 8 * 1024 * 1024, 1, spill, false, true);
        c.record_events(0, 0, 0, &invalid, 1);
        check(!c.finalize());
      }
      Capture surface_only(1, 1, 1, 4 * 1024 * 1024, 1, spill);
      const KernelDeepEvent medium{DEEP_VOLUME, 2, 3, 0, 1};
      surface_only.record_events(0, 0, 0, &medium, 1);
      check(!surface_only.finalize());
      Capture volume(1, 1, 2, 8 * 1024 * 1024, 3, spill, false, true);
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
    for (const bool spill : {false, true}) {
      /* Adaptive volume mixtures normalize by each pixel's accepted population,
       * including misses, rather than the configured maximum sample count. */
      Capture adaptive_volume(2, 1, 4, 128 * 1024 * 1024, 4, spill, true, true);
      const KernelDeepEvent medium[] = {{DEEP_VOLUME, 1, 2, 0, 1}};
      for (int x = 0; x < 2; ++x) {
        const int population = x ? 3 : 1;
        adaptive_volume.record_events(x, 0, 0, medium, 1);
        for (int sample = 1; sample < population; ++sample)
          adaptive_volume.record_events(x, 0, sample, nullptr, 0);
        adaptive_volume.set_population(x, 0, population);
      }
      check(adaptive_volume.finalize());
      for (int x = 0; x < 2; ++x) {
        const auto intervals = adaptive_volume.reconstruct_volume_pixel(x, 0);
        double transmittance = 1;
        for (const auto &interval : intervals)
          transmittance *= 1 - interval.alpha;
        const double expected = 1 - (1 - std::exp(-1.0)) / (x ? 3 : 1);
        check(std::abs(transmittance - expected) < 1e-7);
      }
    }
    for (const bool spill : {false, true}) {
      Capture compressed(1, 1, 1, 64 * 1024 * 1024, 4,
                          spill, false, true, true, 1, 1e-4f);
      const KernelDeepEvent event{DEEP_VOLUME, 1e8f, 1e8f, 0, .125f};
      const KernelDeepDensity density{{.125f, 1e-10f, 0, 0}, 1e8, 1e8 + 1};
      compressed.record_sample(0, 0, 0, {DEEP_COMPLETE, 1, DEEP_ERROR_NONE}, &event, &density);
      check(compressed.finalize());
      const auto sample = compressed.volume_sample(0, 0, 0);
      check(sample.intervals.size() == 1);
      check(sample.intervals[0].front == density.front && sample.intervals[0].back == density.back);
      check(sample.intervals[0].optical_depth == double(.125f) + double(1e-10f));
      compressed.begin_export_row(0);
      const auto staged = compressed.volume_sample(0, 0, 0);
      check(staged.intervals[0].optical_depth == sample.intervals[0].optical_depth);
      compressed.end_export_row(0);
    }
    std::cout
        << "Capture coverage, lifecycle, concurrent writes, bounds and budget checks passed\n";
    if (run_reducer_tests() != 0)
      return 1;
    if (run_surface_reducer_tests() != 0)
      return 1;
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
