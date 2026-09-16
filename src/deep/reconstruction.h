/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <cstdint>
#include <vector>

namespace ccl::deep {

struct SurfaceEvent {
  double depth;
  /* Local opacity, before attenuation by earlier events. */
  double alpha;
};

struct CameraSample {
  /* Unique within this pixel ledger, including across capture batches. */
  uint64_t id;
  double weight;
  /* True only after a verified miss or fully resolved visibility chain.
   * Cancellation, traversal limits and cache retries do not complete a sample. */
  bool complete = false;
  /* A completed empty event list is a miss and still contributes weight. */
  std::vector<SurfaceEvent> events;
};

struct PixelLedger {
  int x;
  int y;
  std::vector<CameraSample> samples;
};

struct SurfaceSample {
  double depth;
  /* Effective pixel opacity, not intrinsic material opacity. ZBack equals depth. */
  double alpha;
};

/* Double-precision surface reference. Input order is immaterial; depths must be
 * finite and positive, alpha in [0, 1], and weights finite and nonnegative.
 * Requires complete, uniquely identified samples and positive total weight.
 * Invalid input throws std::invalid_argument. No quantization or reduction.
 * Deliberately recomputes weighted transmittance at each boundary: this is an
 * understandable oracle, not a production memory/performance implementation. */
std::vector<SurfaceSample> reconstruct(const PixelLedger &pixel);

}  // namespace ccl::deep
