/* SPDX-License-Identifier: Apache-2.0 */

#include "deep/reconstruction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace ccl::deep {
namespace {

/* Compensated summation keeps small weighted contributions visible. */
class Sum {
 public:
  void add(const double value)
  {
    const double corrected = value - correction_;
    const double next = value_ + corrected;
    correction_ = (next - value_) - corrected;
    value_ = next;
  }
  double value() const
  {
    return value_;
  }

 private:
  double value_ = 0.0;
  double correction_ = 0.0;
};

struct Boundary {
  double depth;
  double alpha;
  size_t sample;
};

}  // namespace

std::vector<SurfaceSample> reconstruct(const PixelLedger &pixel)
{
  std::unordered_set<uint64_t> ids;
  std::vector<const CameraSample *> samples;
  double max_weight = 0.0;
  for (const CameraSample &sample : pixel.samples) {
    if (!sample.complete) {
      throw std::invalid_argument("Deep sample visibility chain is incomplete");
    }
    if (!ids.insert(sample.id).second) {
      throw std::invalid_argument("Duplicate deep camera sample identity");
    }
    if (!std::isfinite(sample.weight) || sample.weight < 0.0) {
      throw std::invalid_argument("Deep sample weight must be finite and nonnegative");
    }
    for (const SurfaceEvent &event : sample.events) {
      if (!std::isfinite(event.depth) || event.depth <= 0.0 || !std::isfinite(event.alpha) ||
          event.alpha < 0.0 || event.alpha > 1.0)
      {
        throw std::invalid_argument("Invalid deep surface depth or local opacity");
      }
    }
    max_weight = std::max(max_weight, sample.weight);
    samples.push_back(&sample);
  }
  if (max_weight == 0.0) {
    throw std::invalid_argument("Deep pixel requires positive total sample weight");
  }
  std::sort(
      samples.begin(), samples.end(), [](const auto *a, const auto *b) { return a->id < b->id; });

  std::vector<double> weights, transmittance(samples.size(), 1.0);
  std::vector<Boundary> boundaries;
  Sum total;
  for (size_t i = 0; i < samples.size(); ++i) {
    /* Common scaling avoids overflow when individually valid weights sum above
     * DBL_MAX. It cancels in the before/after ratio. */
    const double weight = samples[i]->weight / max_weight;
    weights.push_back(weight);
    total.add(weight);
    if (weight == 0.0) {
      continue;
    }
    for (const SurfaceEvent &event : samples[i]->events) {
      if (event.alpha > 0.0) {
        boundaries.push_back({event.depth, event.alpha, i});
      }
    }
  }
  std::sort(boundaries.begin(), boundaries.end(), [](const Boundary &a, const Boundary &b) {
    if (a.depth != b.depth) {
      return a.depth < b.depth;
    }
    if (a.sample != b.sample) {
      return a.sample < b.sample;
    }
    return a.alpha < b.alpha;
  });

  std::vector<SurfaceSample> result;
  double before = total.value();
  for (size_t i = 0; i < boundaries.size() && before > 0.0;) {
    const double depth = boundaries[i].depth;
    do {
      const Boundary &event = boundaries[i++];
      transmittance[event.sample] *= 1.0 - event.alpha;
    } while (i < boundaries.size() && boundaries[i].depth == depth);

    Sum remaining;
    for (size_t j = 0; j < samples.size(); ++j) {
      remaining.add(weights[j] * transmittance[j]);
    }
    double after = remaining.value();
    if (after > before) {
      /* Only rounding-sized increases may be clamped. */
      if (after - before > 8.0 * std::numeric_limits<double>::epsilon() * before) {
        throw std::runtime_error("Deep transmittance unexpectedly increased");
      }
      after = before;
    }
    if (after < before) {
      result.push_back({depth, 1.0 - after / before});
    }
    before = after;
  }
  return result;
}

}  // namespace ccl::deep
