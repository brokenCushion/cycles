/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include "deep/exr_writer.h"
#include "deep/publication.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
using namespace ccl::deep;
static void check(bool value)
{
  if (!value)
    throw std::runtime_error("M5 assertion failed");
}
template<class F> static void rejects(F f)
{
  bool failed = false;
  try {
    f();
  }
  catch (const std::exception &) {
    failed = true;
  }
  check(failed);
}
int main(int argc, char **argv)
{
  try {
    check(argc == 2);
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    rejects([] { Capture c(100000, 10000, 4096, 1024, 64, true); });
    const KernelDeepEvent stack[] = {{DEEP_SURFACE, 2, 2, .25f, 0}, {DEEP_SURFACE, 8, 8, .5f, 0}};
    Capture memory(8, 3, 8, 100000, 2), disk(8, 3, 8, 8 * 1024 * 1024, 2, true);
    check(!disk.finalize());
    rejects([&] { disk.reconstruct_pixel(0, 0); });
    auto record = [&](int parity) {
      for (int y = 0; y < 3; ++y)
        for (int x = parity; x < 8; x += 2)
          for (int s = 7; s >= 0; --s) {
            const int n = (x + y + s) % 3;
            memory.record_events(x, y, s, stack, n);
            disk.record_events(x, y, s, stack, n);
          }
    };
    std::thread a(record, 0), b(record, 1);
    a.join();
    b.join();
    check(memory.finalize() && disk.finalize());
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 8; ++x) {
        auto p = memory.reconstruct_pixel(x, y), q = disk.reconstruct_pixel(x, y);
        check(p.size() == q.size());
        for (size_t i = 0; i < p.size(); ++i)
          check(p[i].depth == q[i].depth && p[i].alpha == q[i].alpha);
      }
    disk.record_events(0, 0, 0, stack, 1);
    check(!disk.finalize());
    Capture opaque(2, 1, 1, 4 * 1024 * 1024, 0, true);
    opaque.record(0, 0, 0, .123456789f);
    opaque.record(1, 0, 0, 0);
    check(opaque.finalize() && opaque.value(0, 0, 0) == .123456789f && opaque.value(1, 0, 0) == 0);

    std::vector<SurfaceSample> source;
    for (int i = 0; i < 20000; ++i)
      source.push_back({double(i + 1), .00001});
    const auto reduced = reduce_surface(source, .000999);
    check(reduced.size() < source.size() / 10);
    double before = 1, after = 1, max_error = 0;
    size_t j = 0;
    for (const auto &s : source) {
      max_error = std::max(max_error, std::abs(before - after));
      before *= 1 - s.alpha;
      if (j < reduced.size() && reduced[j].depth == s.depth)
        after *= 1 - reduced[j++].alpha;
      max_error = std::max(max_error, std::abs(before - after));
    }
    check(max_error <= .000999 && std::abs(before - after) < 1e-12);
    check(reduce_surface(source, 0).size() == source.size());
    SurfaceImage image;
    image.display_window = image.data_window = {0, 0, 0, 2};
    image.reduction_error = .001;
    const auto path = directory / "production.exr";
    write_deep_exr_rows(
        path, image, [&](int) { return std::vector<std::vector<SurfaceSample>>{source}; });
    const auto read_file = [&] {
      std::ifstream f(path, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    };
    const auto original = read_file();
    for (int failure = 0; failure < 3; ++failure) {
      rejects([&] {
        write_deep_exr_rows(path, image, [&](int y) {
          if (y == 1) {
            if (failure == 0)
              throw std::runtime_error("cancelled");
            if (failure == 1)
              throw std::bad_alloc();
            throw std::ios_base::failure("injected disk-full write failure");
          }
          return std::vector<std::vector<SurfaceSample>>{source};
        });
      });
      check(read_file() == original);
    }
    rejects([&] {
      write_deep_exr_rows(
          path,
          image,
          [&](int) { return std::vector<std::vector<SurfaceSample>>{source}; },
          [] { throw std::runtime_error("cancelled after final scanline"); });
    });
    check(read_file() == original);
    image.reduction_error = 0;
    image.display_window = image.data_window = {-1, 0, 1, 2};
    for (int failure = 0; failure < 4; ++failure) {
      int calls = 0;
      rejects([&] {
        write_volume_exr_pixels(
            path,
            image,
            [&](int x, int y) {
              check(x == calls % 3 - 1 && y == calls / 3);
              ++calls;
              if (x == 0 && y == 1) {
                if (failure == 0)
                  throw std::runtime_error("cancelled during volume export");
                if (failure == 2)
                  throw std::bad_alloc();
                if (failure == 3)
                  throw std::ios_base::failure("injected volume source I/O failure");
              }
              return std::vector<IntervalSample>{{2, 8, .5}};
            },
            [&] {
              if (failure == 1)
                throw std::runtime_error("cancelled before volume publication");
            });
      });
      check(calls == (failure == 1 ? 9 : 5));
      check(read_file() == original);
    }
    image.volume_row_sample_limit = 2;
    int row_limit_calls = 0;
    rejects([&] {
      write_volume_exr_pixels(path, image, [&](int, int) {
        ++row_limit_calls;
        return std::vector<IntervalSample>{{2, 8, .5}};
      });
    });
    check(row_limit_calls == 3 && read_file() == original);
    /* The exact row limit succeeds and resets on the next row, including
     * offset windows and empty pixels. */
    write_volume_exr_pixels(directory / "volume-row-budget.exr", image, [&](int x, int) {
      return x == 0 ? std::vector<IntervalSample>{} :
                      std::vector<IntervalSample>{{2, 8, .5}};
    });
    /* Reduction must release source-sized reservations before row budgeting.
     * This constant-density source publishes one interval within the same
     * whole-curve allowance, so a one-sample row budget is sufficient. */
    SurfaceImage reduced_row{{0, 0, 0, 0}, {0, 0, 0, 0}};
    reduced_row.volume_row_sample_limit = 1;
    write_volume_exr_pixels(directory / "volume-reduced-row-budget.exr", reduced_row,
                           [](int, int) {
      std::vector<IntervalSample> intervals;
      for (int i = 0; i < 128; ++i)
        intervals.push_back({2 + i / 128.0, 2 + (i + 1) / 128.0,
                             -std::expm1(-1 / 1024.0)});
      return intervals;
    });
#ifdef _WIN32
    /* A real OS replacement failure after serialization, without filling a
     * drive or altering permissions. Deny deletion of our completed fixture. */
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE);
    bool replacement_failed = false;
    int locked_pixels = 0;
    image.volume_row_sample_limit = 0;
    try {
      write_volume_exr_pixels(path, image, [&](int, int) {
        ++locked_pixels;
        return std::vector<IntervalSample>{{2, 8, .5}};
      });
    }
    catch (const std::system_error &) {
      replacement_failed = true;
    }
    catch (...) {
      CloseHandle(locked);
      throw;
    }
    CloseHandle(locked);
    check(replacement_failed && locked_pixels == 9 && read_file() == original);
#endif
    {
      /* Real shared spill reads and adaptive partial batches must produce the
       * same file with bounded parallel reconstruction, including offset windows. */
      Capture capture(24, 2, 17, size_t(512) * 1024 * 1024,
                      4, true, true, true, true, 4);
      Capture reference(24, 2, 17, size_t(512) * 1024 * 1024,
                        4, false, true, true, true);
      check(capture.volume_export_workers() == 3);
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 24; ++x) {
          const int count = x % 3 == 0 ? 9 : (x % 3 == 1 ? 16 : 17);
          for (int sample = 0; sample < count; ++sample) {
            const KernelDeepEvent events[] = {
                {DEEP_VOLUME, 2, 8, 0, .1f + .01f * (sample % 5)},
                {DEEP_SURFACE, 9 + .01f * sample, 9 + .01f * sample, .3f, 0}};
            for (Capture *storage : {&capture, &reference})
              storage->record_events(x, y, sample, events, sample % 7 ? 2 : 0);
          }
          capture.set_population(x, y, count);
          reference.set_population(x, y, count);
        }
      check(capture.finalize());
      SurfaceImage threaded{{-5, 9, 18, 10}, {-5, 9, 18, 10}};
      threaded.compression = DeepCompression::Zips;
      threaded.volume_row_sample_limit = capture.volume_row_sample_limit();
      const auto reference_path = directory / "parallel-reference.exr";
      const auto parallel_path = directory / "parallel.exr";
      write_volume_exr_pixels(reference_path, threaded, [&](int x, int y) {
        return reference.reconstruct_volume_pixel(x + 5, 10 - y);
      });
      threaded.volume_export_workers = capture.volume_export_workers();
      std::atomic<int> active{0}, peak{0};
      write_volume_exr_pixels(parallel_path, threaded, [&](int x, int y) {
        const int current = active.fetch_add(1) + 1;
        int previous = peak.load();
        while (previous < current && !peak.compare_exchange_weak(previous, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto result = capture.reconstruct_volume_pixel(x + 5, 10 - y);
        active.fetch_sub(1);
        return result;
      }, {}, [&](int y) { capture.begin_export_row(10 - y); },
             [&](int y) { capture.end_export_row(10 - y); });
      check(active == 0 && peak <= 4);
      if (std::thread::hardware_concurrency() > 1)
        check(peak > 1);
      const auto contents = [](const std::filesystem::path &file) {
        std::ifstream f(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      };
      const auto expected = contents(reference_path);
      check(contents(parallel_path) == expected);
      for (int failure = 0; failure < 4; ++failure) {
        threaded.volume_row_sample_limit = failure == 2 ? 1 : capture.volume_row_sample_limit();
        rejects([&] {
          write_volume_exr_pixels(parallel_path, threaded, [&](int x, int) {
            active.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            active.fetch_sub(1);
            if (failure == 0 && x == 0)
              throw std::runtime_error("injected parallel worker failure");
            return std::vector<IntervalSample>{{2, 8, .5}};
          }, [&] {
            if (failure == 1)
              throw std::runtime_error("cancelled before parallel publication");
          }, [&](int y) {
            if (failure == 3 && y == 10)
              throw std::ios_base::failure("injected sequential band read failure");
          });
        });
        check(active == 0 && contents(parallel_path) == expected);
      }
    }
    for (const auto &entry : std::filesystem::directory_iterator(directory))
      check(entry.path().filename().string().find(".partial-") == std::string::npos);
    std::cout << "PASS spill identity/completeness/concurrency, preflight memory limit, atomic "
                 "failures\n"
              << "Reduction " << source.size() << " -> " << reduced.size()
              << ", max boundary error " << max_error << '\n';
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
