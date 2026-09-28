/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include "deep/exr_writer.h"
#include "deep/publication.h"
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
