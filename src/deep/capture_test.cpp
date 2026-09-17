/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/capture.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using ccl::deep::OpaqueCapture;
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
    rejects([] { OpaqueCapture c(-1, 2, 1, 1024); });
    rejects([] { OpaqueCapture c(1, 2, 0, 1024); });
    rejects([] { OpaqueCapture c(1, 1, 4097, 1024); });
    rejects([] { OpaqueCapture c(1000, 1000, 1000, 1024); });
    rejects([] { OpaqueCapture c(INT32_MAX, INT32_MAX, 4096, SIZE_MAX); });
    OpaqueCapture capture(2, 1, 4, 32);
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
      OpaqueCapture c(1, 1, 1, 4);
      c.record(0, 0, 0, invalid);
      check(!c.finalize());
    }
    OpaqueCapture bounds(1, 1, 1, 4);
    bounds.record(1, 0, 0, 0);
    check(!bounds.finalize());
    OpaqueCapture sample(1, 1, 1, 4);
    sample.record(0, 0, 1, 0);
    check(!sample.finalize());
    OpaqueCapture state(1, 1, 1, 4);
    state.fail();
    state.record(0, 0, 0, 0);
    check(!state.finalize());
    OpaqueCapture miss(1, 1, 1, 4);
    miss.record(0, 0, 0, 0);
    check(miss.finalize());
    check(miss.reconstruct_pixel(0, 0).empty());
    OpaqueCapture chains(1, 1, 2, 40, 2);
    const float stack[] = {2, .25f, 8, .5f};
    chains.record_events(0, 0, 0, stack, 2);
    check(!chains.finalize());
    chains.record_events(0, 0, 1, nullptr, 0);
    check(chains.finalize());
    p = chains.reconstruct_pixel(0, 0);
    check(p.size() == 2 && p[0].alpha == .125);
    check(std::abs((1 - p[0].alpha) * (1 - p[1].alpha) - .6875) < 1e-12);
    chains.record_events(0, 0, 0, stack, 2);
    check(!chains.finalize());
    rejects([] { OpaqueCapture c(1, 1, 2, 39, 2); });
    rejects([] { OpaqueCapture c(1, 1, 1, 1000, 65); });
    for (const float alpha : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
      OpaqueCapture c(1, 1, 1, 12, 1);
      const float event[] = {2, alpha};
      c.record_events(0, 0, 0, event, 1);
      check(!c.finalize());
    }
    OpaqueCapture overflow(1, 1, 1, 12, 1);
    overflow.record_events(0, 0, 0, stack, 2);
    check(!overflow.finalize());
    std::cout
        << "Capture coverage, lifecycle, concurrent writes, bounds and budget checks passed\n";
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
