/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/volume.h"
#ifndef CCL_NAMESPACE_BEGIN
#  define CCL_NAMESPACE_BEGIN namespace ccl {
#  define CCL_NAMESPACE_END }
#endif
#include "kernel/deep/density.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <map>
#include <memory>

namespace ccl::deep {
std::vector<VolumeInterval> integrate_cubic_density(
    const std::vector<CubicDensityInterval> &segments,
    const double tolerance,
    const size_t max_intervals)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-2 || !max_intervals)
    throw std::invalid_argument("Invalid cubic integration budget");
  std::vector<std::pair<double, int>> boundaries;
  std::vector<std::pair<double, double>> completed;
  for (const auto &s : segments) {
    if (!std::isfinite(s.front) || !std::isfinite(s.back) || s.front <= 0 ||
        s.back <= s.front)
      throw std::invalid_argument("Invalid cubic density interval");
    bool active = false;
    double sum = 0;
    for (const double b : s.optical_depth) {
      if (!std::isfinite(b) || b < 0)
        throw std::invalid_argument("Invalid cubic density coefficient");
      active |= b > 0;
      sum += b;
    }
    if (!std::isfinite(sum))
      throw std::invalid_argument("Cubic density integral overflow");
    if (active) {
      boundaries.emplace_back(s.front, 1);
      boundaries.emplace_back(s.back, -1);
      completed.emplace_back(s.back, sum / 4);
    }
  }
  /* Only unfinished cells contribute approximation error at any depth.
   * Adjacent cells preserve total tau, so divide by simultaneous overlaps,
   * not by every cell along a long VDB ray. Ends sort before starts. */
  std::sort(boundaries.begin(), boundaries.end());
  size_t active = 0, overlap = 0;
  for (const auto &boundary : boundaries) {
    if (boundary.second > 0)
      overlap = std::max(overlap, ++active);
    else
      --active;
  }
  if (!overlap)
    return {};
  std::sort(completed.begin(), completed.end());
  double cumulative_tau = 0;
  for (auto &entry : completed) {
    cumulative_tau += entry.second;
    entry.second = cumulative_tau;
  }
  const double budget = tolerance / overlap;
  if (!(budget > 0))
    throw std::invalid_argument("Cubic integration budget underflow");
  std::vector<VolumeInterval> result;
  /* Count before allocating output. Iterative depth-first traversal uses a
   * fixed stack; both passes follow precisely the same subdivisions. */
  size_t count = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (const auto &s : segments) {
      const DeepCubicDensity<double> curve = {{s.optical_depth[0], s.optical_depth[1],
                                               s.optical_depth[2], s.optical_depth[3]}};
      if (deep_density_integral(curve, 1.0) == 0)
        continue;
      struct Piece { double front, back; int level; };
      Piece stack[62];
      size_t pending = 1;
      stack[0] = {s.front, s.back, 0};
      while (pending) {
        const Piece p = stack[--pending];
        const double a = (p.front - s.front) / (s.back - s.front);
        const double b = (p.back - s.front) / (s.back - s.front);
        const auto clipped = deep_density_restrict(curve, a, b);
        /* Completed cells preserve exact total tau. Within this cell, both
         * the exact integral and its chord start at the same optical depth.
         * Their transmission error is therefore attenuated by this common
         * prefix. Ignoring unfinished OTHER cells is conservative. This avoids
         * over-subdividing dense cloud interiors without dropping their mass. */
        const auto end = std::upper_bound(completed.begin(), completed.end(), p.front,
            [](double z, const auto &entry) { return z < entry.first; });
        const double prefix = (end == completed.begin() ? 0 : (end - 1)->second) +
                              deep_density_integral(deep_density_restrict(curve, 0.0, a), a);
        if (std::exp(-prefix) * deep_density_chord_error(clipped, b - a) <= budget) {
          if (pass == 0) {
            if (count == max_intervals)
              throw std::invalid_argument("Cubic integration capacity exceeded");
            ++count;
          }
          else
            result.push_back({p.front, p.back, deep_density_integral(clipped, b - a)});
          continue;
        }
        const double middle = p.front + (p.back - p.front) / 2;
        if (p.level == 60 || middle == p.front || middle == p.back)
          throw std::invalid_argument("Cubic subdivision exhausted depth precision");
        stack[pending++] = {middle, p.back, p.level + 1};
        stack[pending++] = {p.front, middle, p.level + 1};
      }
    }
    if (pass == 0)
      result.reserve(count);
  }
  return result;
}

std::vector<VolumeInterval> integrate_linear_density(
    const std::vector<LinearDensityInterval> &segments,
    const double tolerance,
    const size_t max_intervals)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-2 || !max_intervals)
    throw std::invalid_argument("Invalid density integration budget");
  size_t active = 0;
  for (const auto &s : segments) {
    if (!std::isfinite(s.front) || !std::isfinite(s.back) || s.front <= 0 ||
        s.back <= s.front || !std::isfinite(s.sigma_front) ||
        !std::isfinite(s.sigma_back) || s.sigma_front < 0 || s.sigma_back < 0 ||
        !std::isfinite(s.physical_length_per_depth) || s.physical_length_per_depth <= 0)
      throw std::invalid_argument("Invalid linear density interval");
    active += s.sigma_front > 0 || s.sigma_back > 0;
  }
  if (!active)
    return {};
  /* Linear tau interpolation error is |sigma'| * h^2 / 8. exp(-tau)
   * is 1-Lipschitz for nonnegative tau. Divide the budget among all supplied
   * profiles so overlapping media are also bounded. Completed pieces have
   * exact trapezoidal integrals, so errors do not accumulate along a profile. */
  const double budget = tolerance / active;
  if (!(budget > 0))
    throw std::invalid_argument("Density integration budget underflow");
  const auto divisions = [&](const LinearDensityInterval &s) -> size_t {
    if (s.sigma_front == 0 && s.sigma_back == 0)
      return 0;
    const double length = (s.back - s.front) * s.physical_length_per_depth;
    const double tau = length * (s.sigma_front * .5 + s.sigma_back * .5);
    const double curvature = std::abs(s.sigma_back - s.sigma_front) * length;
    if (!std::isfinite(tau) || !std::isfinite(curvature) || !std::isfinite(length))
      throw std::invalid_argument("Density integral overflow");
    const double n = std::max(1.0, std::ceil(std::sqrt(curvature / (8 * budget))));
    if (!std::isfinite(n) || n > double(max_intervals) ||
        n >= double(std::numeric_limits<size_t>::max()))
      throw std::invalid_argument("Density integration capacity exceeded");
    return size_t(n);
  };
  size_t count = 0;
  for (const auto &s : segments) {
    const size_t n = divisions(s);
    if (n > max_intervals - count)
      throw std::invalid_argument("Density integration capacity exceeded");
    count += n;
  }
  std::vector<VolumeInterval> result;
  result.reserve(count);
  for (const auto &s : segments) {
    const size_t n = divisions(s);
    for (size_t i = 0; i < n; ++i) {
      const double a = double(i) / n, b = double(i + 1) / n;
      const double front = i ? s.front + (s.back - s.front) * a : s.front;
      const double back = i + 1 == n ? s.back : s.front + (s.back - s.front) * b;
      if (!(back > front))
        throw std::invalid_argument("Density subdivision exhausted depth precision");
      const double mid = .5 * ((front - s.front) / (s.back - s.front) +
                              (back - s.front) / (s.back - s.front));
      const double sigma = (1 - mid) * s.sigma_front + mid * s.sigma_back;
      const double tau = sigma * (back - front) * s.physical_length_per_depth;
      if (!std::isfinite(tau) || tau < 0)
        throw std::invalid_argument("Invalid density integral");
      if (tau > 0)
        result.push_back({front, back, tau});
    }
  }
  return result;
}

namespace {
/* FLOAT alpha loses optical-depth precision near one. Bound each continuous
 * interval to tau <= 2, including merges, before the writer's whole-curve check.
 * Subdivision remains subject to the existing per-pixel interval budget. */
constexpr double max_interval_tau = 2;
const double max_interval_alpha = -std::expm1(-max_interval_tau);

/* Exact extrema of the difference of two exponentials on one span. */
double exponential_error(double ta, double ra, double tb, double rb, double width)
{
  double error = std::max(std::abs(ta - tb),
                          std::abs(ta * std::exp(-ra * width) - tb * std::exp(-rb * width)));
  if (ra > 0 && rb > 0 && ra != rb && ta > 0 && tb > 0) {
    const double x = (std::log(ra) + std::log(ta) - std::log(rb) - std::log(tb)) / (ra - rb);
    if (x > 0 && x < width)
      error = std::max(error, std::abs(ta * std::exp(-ra * x) - tb * std::exp(-rb * x)));
  }
  return error;
}

/* The merger handles two adjacent volume intervals, so its bound needs just
 * two spans. Avoid allocating/sorting general curve vectors at every boundary. */
double merge_error(const IntervalSample &a, const IntervalSample &b, const IntervalSample &merged)
{
  const double wa = a.back - a.front, wb = b.back - b.front;
  const double ra = -std::log1p(-a.alpha) / wa;
  const double rb = -std::log1p(-b.alpha) / wb;
  const double rm = -std::log1p(-merged.alpha) / (merged.back - merged.front);
  return std::max(exponential_error(1, ra, 1, rm, wa),
                  exponential_error(1 - a.alpha, rb, std::exp(-rm * wa), rm, wb));
}

void validate_curve(const std::vector<IntervalSample> &samples)
{
  double previous = 0;
  for (const auto &s : samples) {
    if (!std::isfinite(s.front) || !std::isfinite(s.back) || s.front <= 0 ||
        s.front < previous || s.back < s.front || !std::isfinite(s.alpha) ||
        s.alpha <= 0 || s.alpha > 1 || (s.back > s.front && s.alpha == 1))
      throw std::invalid_argument("Invalid deep exponential interval");
    previous = s.back;
  }
}

/* Evaluate sorted boundary queries without rescanning completed intervals. */
struct CurveCursor {
  const std::vector<IntervalSample> &samples;
  size_t index = 0;
  double prefix = 1;
  double at(const double z, const bool before)
  {
    while (index < samples.size()) {
      const auto &s = samples[index];
      if (s.front == s.back) {
        if (!(s.front < z || (!before && s.front == z)))
          break;
      }
      else if (s.back > z)
        break;
      prefix *= 1 - s.alpha;
      ++index;
    }
    if (index < samples.size()) {
      const auto &s = samples[index];
      if (s.back > s.front && z > s.front)
        return prefix * std::exp(std::log1p(-s.alpha) * ((z - s.front) / (s.back - s.front)));
    }
    return prefix;
  }
  double rate(const double z) const
  {
    if (index < samples.size()) {
      const auto &s = samples[index];
      if (s.front < z && z < s.back)
        return -std::log1p(-s.alpha) / (s.back - s.front);
    }
    return 0;
  }
};

/* Each grid crossing emits an ordered interval run. Index those runs separately
 * so overlapping grids add optical depth without rescanning every cell at each
 * fitting query. Arbitrarily shuffled input remains valid, with shorter runs. */
struct OpticalDepthCurve {
  const std::vector<VolumeInterval> &intervals;
  std::vector<double> prefix;
  std::vector<SurfaceEvent> surfaces;
  std::vector<double> surface_prefix;
  struct Run {
    size_t begin, end;
    double total;
    mutable size_t cursor;
  };
  std::vector<Run> runs;

  explicit OpticalDepthCurve(const std::vector<VolumeInterval> &source,
                             const std::vector<SurfaceEvent> &events)
      : intervals(source), surfaces(events)
  {
    std::sort(surfaces.begin(), surfaces.end(), [](const auto &a, const auto &b) {
      return a.depth < b.depth;
    });
    surface_prefix.reserve(surfaces.size());
    double transmission = 1;
    for (const auto &event : surfaces) {
      transmission *= 1 - event.alpha;
      surface_prefix.push_back(transmission);
    }
    size_t count = 0;
    for (size_t i = 0; i < intervals.size(); ++i)
      count += i == 0 || intervals[i].front < intervals[i - 1].back;
    runs.reserve(count);
    prefix.reserve(intervals.size());
    for (size_t i = 0; i < intervals.size(); ++i) {
      if (i == 0 || intervals[i].front < intervals[i - 1].back)
        runs.push_back({i, i, 0, i});
      Run &run = runs.back();
      prefix.push_back(run.total);
      run.total += intervals[i].optical_depth;
      ++run.end;
    }
  }

  size_t index(const Run &run, double z, bool ordered = false) const
  {
    if (ordered) {
      while (run.cursor < run.end && intervals[run.cursor].back < z)
        ++run.cursor;
      return run.cursor;
    }
    return std::lower_bound(intervals.begin() + run.begin,
                            intervals.begin() + run.end,
                            z,
                            [](const VolumeInterval &v, double depth) { return v.back < depth; }) -
           intervals.begin();
  }

  double at(double z, bool ordered = false) const
  {
    double tau = 0;
    for (const Run &run : runs) {
      const size_t i = index(run, z, ordered);
      if (i == run.end)
        tau += run.total;
      else {
        const auto &v = intervals[i];
        tau += prefix[i] +
               v.optical_depth * std::clamp((z - v.front) / (v.back - v.front), 0.0, 1.0);
      }
    }
    return tau;
  }

  double rate(double z, bool ordered = false) const
  {
    double rate = 0;
    for (const Run &run : runs) {
      const size_t i = index(run, z, ordered);
      if (i != run.end && intervals[i].front < z && z < intervals[i].back)
        rate += intervals[i].optical_depth / (intervals[i].back - intervals[i].front);
    }
    return rate;
  }

  double transparency(double z, bool before) const
  {
    const auto end = before ? std::lower_bound(surfaces.begin(),
                                               surfaces.end(),
                                               z,
                                               [](const SurfaceEvent &event, double depth) {
                                                 return event.depth < depth;
                                               }) :
                              std::upper_bound(surfaces.begin(),
                                               surfaces.end(),
                                               z,
                                               [](double depth, const SurfaceEvent &event) {
                                                 return depth < event.depth;
                                               });
    return end == surfaces.begin() ? 1 : surface_prefix[size_t(end - surfaces.begin() - 1)];
  }
};
}  // namespace

std::vector<IntervalSample> reduce_interval_curve(const std::vector<IntervalSample> &source,
                                                 const double tolerance)
{
  validate_curve(source);
  if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1e-2)
    throw std::invalid_argument("Invalid volume export reduction budget");
  std::vector<IntervalSample> result;
  result.reserve(source.size());
  double prefix = 1, last_prefix = 1, last_error = 0;
  for (const auto &next : source) {
    bool merged = false;
    if (tolerance > 0 && !result.empty()) {
      const auto &previous = result.back();
      if (previous.front < previous.back && previous.back == next.front && next.front < next.back) {
        const double alpha = -std::expm1(std::log1p(-previous.alpha) + std::log1p(-next.alpha));
        const IntervalSample combined{previous.front, next.back, alpha};
        if (alpha <= max_interval_alpha) {
          const double error = last_error + last_prefix * merge_error(previous, next, combined) +
                               32 * std::numeric_limits<double>::epsilon();
          if (error <= tolerance) {
            result.back() = combined;
            last_error = error;
            merged = true;
          }
        }
      }
    }
    if (!merged) {
      result.push_back(next);
      last_prefix = prefix;
      last_error = 0;
    }
    prefix *= 1-next.alpha;
  }
  return result;
}

double interval_transmittance(const std::vector<IntervalSample> &samples,
                              double depth,
                              bool before)
{
  double t = 1;
  for (const auto &s : samples) {
    if (s.front == s.back) {
      if (s.front < depth || (!before && s.front == depth))
        t *= 1 - s.alpha;
    }
    else {
      const double fraction = std::clamp((depth - s.front) / (s.back - s.front), 0.0, 1.0);
      t *= std::exp(std::log1p(-s.alpha) * fraction);
    }
  }
  return t;
}

double interval_curve_error(const std::vector<IntervalSample> &a,
                            const std::vector<IntervalSample> &b)
{
  validate_curve(a);
  validate_curve(b);
  std::vector<double> boundaries;
  for (const auto *curve : {&a, &b})
    for (const auto &s : *curve) {
      boundaries.push_back(s.front);
      boundaries.push_back(s.back);
    }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  double error = 0;
  CurveCursor ca{a}, cb{b};
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const double z = boundaries[i];
    for (bool before : {true, false})
      error = std::max(error, std::abs(ca.at(z, before) - cb.at(z, before)));
    if (i + 1 == boundaries.size())
      break;
    const double h = boundaries[i + 1] - z;
    const double ta = ca.at(z, false), tb = cb.at(z, false);
    const double ra = ca.rate(z + h / 2), rb = cb.rate(z + h / 2);
    if (ra > 0 && rb > 0 && ra != rb && ta > 0 && tb > 0) {
      const double x = (std::log(ra) + std::log(ta) - std::log(rb) - std::log(tb)) /
                       (ra - rb);
      if (x > 0 && x < h)
        error = std::max(error, std::abs(ta * std::exp(-ra * x) - tb * std::exp(-rb * x)));
    }
  }
  return error;
}

std::vector<IntervalSample> reconstruct_volume(std::vector<VolumeCameraSample> samples,
                                              double tolerance,
                                              size_t max_intervals,
                                              double reduction_tolerance,
                                              size_t max_working_bytes)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-2 || !max_intervals ||
      !std::isfinite(reduction_tolerance) || reduction_tolerance < 0 ||
      reduction_tolerance > 1e-2)
    throw std::invalid_argument("Invalid volume reconstruction budget");
  std::unordered_set<uint64_t> ids;
  std::vector<double> boundaries;
  double max_weight = 0;
  size_t input_slots = 0;
  for (const auto &s : samples) {
    if (!s.camera.complete || !ids.insert(s.camera.id).second ||
        !std::isfinite(s.camera.weight) || s.camera.weight < 0)
      throw std::invalid_argument("Invalid or incomplete volume camera sample");
    max_weight = std::max(max_weight, s.camera.weight);
    const size_t slots = s.intervals.capacity() + s.camera.events.capacity();
    if (slots > max_working_bytes / 128 - input_slots)
      throw std::runtime_error("Deep volume pixel reconstruction exceeds --deep-memory-mb budget");
    input_slots += slots;
    for (const auto &v : s.intervals) {
      if (!std::isfinite(v.front) || !std::isfinite(v.back) || v.front <= 0 ||
          v.back <= v.front || !std::isfinite(v.optical_depth) || v.optical_depth < 0 ||
          !std::isfinite(v.optical_depth / (v.back - v.front)))
        throw std::invalid_argument("Invalid volume extinction interval");
      boundaries.push_back(v.front);
      boundaries.push_back(v.back);
    }
    for (const auto &srf : s.camera.events) {
      if (!std::isfinite(srf.depth) || srf.depth <= 0 || !std::isfinite(srf.alpha) ||
          srf.alpha < 0 || srf.alpha > 1)
        throw std::invalid_argument("Invalid volume surface event");
      boundaries.push_back(srf.depth);
    }
  }
  if (!max_weight)
    throw std::invalid_argument("Volume pixel requires positive sample weight");
  if (samples.size() > 8) {
    /* Balanced convex averages add at most one fitting/reduction allowance
     * per level, rather than rescanning every ray at every union boundary.
     * Reuse the same two-ray fitter and divide the existing total budget. */
    std::vector<double>().swap(boundaries);
    std::vector<VolumeCameraSample> working;
    working.reserve(samples.size());
    for (auto &sample : samples)
      if (sample.camera.weight > 0) {
        working.push_back(std::move(sample));
        working.back().camera.weight /= max_weight;
      }
    /* Production callers transfer their ledger rather than retaining another
     * full copy during reconstruction. */
    std::vector<VolumeCameraSample>().swap(samples);
    if (working.size() <= 8)
      return reconstruct_volume(std::move(working), tolerance, max_intervals,
                                reduction_tolerance, max_working_bytes);
    size_t levels = 0;
    for (size_t n = working.size(); n > 1; n = (n + 1) / 2)
      ++levels;
    auto slots = [](const VolumeCameraSample &sample) {
      return sample.intervals.capacity() + sample.camera.events.capacity();
    };
    size_t working_bytes = 0;
    for (const auto &sample : working)
      working_bytes += slots(sample) * 96;
    auto check_memory = [&]() {
      if (working_bytes > max_working_bytes)
        throw std::runtime_error("Deep volume pixel reconstruction exceeds --deep-memory-mb budget: tree " +
            std::to_string(working_bytes) + ", budget " + std::to_string(max_working_bytes));
    };
    check_memory();
    std::vector<VolumeCameraSample> pair;
    pair.reserve(2);
    while (working.size() > 1) {
      size_t count = 0;
      for (size_t i = 0; i < working.size(); i += 2) {
        if (i + 1 == working.size()) {
          working[count++] = std::move(working[i]);
          break;
        }
        pair.clear();
        pair.push_back(std::move(working[i]));
        pair.push_back(std::move(working[i + 1]));
        const size_t previous_bytes = (slots(pair[0]) + slots(pair[1])) * 96;
        auto curve = reconstruct_volume(pair, tolerance / levels, max_intervals,
                                         reduction_tolerance / levels);
        if (working.size() == 2)
          return curve;
        VolumeCameraSample merged{{pair[0].camera.id,
                                    pair[0].camera.weight + pair[1].camera.weight, true, {}}, {}};
        for (const auto &span : curve)
          if (span.front == span.back)
            merged.camera.events.push_back({span.front, span.alpha});
          else
            merged.intervals.push_back({span.front, span.back, -std::log1p(-span.alpha)});
        pair.clear();
        working_bytes = working_bytes - previous_bytes + slots(merged) * 96;
        check_memory();
        working[count++] = std::move(merged);
      }
      working.resize(count);
    }
  }
  std::vector<IntervalSample> result;
  double last_error = 0, last_prefix = 1;
  /* A small bounded source window can tighten the triangle-inequality merge
   * bound. Longer chains retain the conservative bound; never reset its budget. */
  std::array<IntervalSample, 64> merge_source;
  /* Source rates stay fixed while successive candidate merges are tested. */
  std::array<double, 64> merge_rates;
  size_t merge_count = 0;
  auto emit = [&](double front, double back, double a, double b) {
    if (a <= b || a == 0)
      return;
    const double alpha = 1 - b / a;
    if (front < back && alpha == 1)
      throw std::runtime_error("Volume interval opacity cannot be represented");
    const IntervalSample next{front, back, alpha};
    if (reduction_tolerance > 0 && !result.empty()) {
      const auto &previous = result.back();
      if (previous.front < previous.back && previous.back == front && front < back) {
        const double combined_alpha = -std::expm1(std::log1p(-previous.alpha) +
                                                 std::log1p(-alpha));
        const IntervalSample combined{previous.front, back, combined_alpha};
        if (combined_alpha <= max_interval_alpha) {
          /* The merge preserves endpoint transmittance. Its interior error
           * adds to the last segment's accumulated bound, never to a fresh
           * budget on each merge. Completed segments are never changed again.
           * Therefore max(segment errors), not their sum, bounds the curve. */
          double error = last_error + last_prefix *
              merge_error(previous, next, combined) +
              32 * std::numeric_limits<double>::epsilon();
          if (error > reduction_tolerance && merge_count) {
            const double rate = -std::log1p(-combined_alpha) / (back - combined.front);
            double prefix = 1, exact = 0;
            for (size_t i = 0; i <= merge_count; ++i) {
              const auto &s = i == merge_count ? next : merge_source[i];
              exact = std::max(exact, exponential_error(
                  prefix, i == merge_count ? -std::log1p(-s.alpha) / (s.back - s.front) :
                                             merge_rates[i],
                  std::exp(-rate * (s.front - combined.front)), rate, s.back - s.front));
              /* The maximum cannot decrease. Preserve the same roundoff
               * allowance, but stop examining a merge already rejected. */
              if (last_prefix * exact +
                      32 * (merge_count + 1) * std::numeric_limits<double>::epsilon() >
                  reduction_tolerance)
                break;
              prefix *= 1 - s.alpha;
            }
            error = last_prefix * exact +
                    32 * (merge_count + 1) * std::numeric_limits<double>::epsilon();
          }
          if (error <= reduction_tolerance) {
            result.back() = combined;
            last_error = error;
            if (merge_count && merge_count < merge_source.size()) {
              merge_source[merge_count] = next;
              merge_rates[merge_count++] = -std::log1p(-alpha) / (back - front);
            }
            else
              merge_count = 0;
            return;
          }
        }
      }
    }
    if (result.size() == max_intervals)
      throw std::runtime_error("Volume reconstruction interval budget exceeded");
    result.push_back(next);
    last_error = 0;
    last_prefix = a;
    merge_source[0] = next;
    merge_rates[0] = reduction_tolerance > 0 && front < back ?
                         -std::log1p(-alpha) / (back - front) : 0;
    merge_count = 1;
  };
  /* A single ordered medium already is an exponential curve. Avoid repeatedly
   * evaluating every interval at every boundary (quadratic for native VDBs).
   * Overlaps or surface events retain the general mixture reconstruction. */
  if (samples.size() == 1 && samples[0].camera.events.empty() && reduction_tolerance == 0) {
    bool ordered = true;
    double previous = 0;
    for (const auto &v : samples[0].intervals) {
      ordered &= v.front >= previous;
      previous = v.back;
    }
    if (ordered) {
      size_t count = 0;
      for (const auto &v : samples[0].intervals) {
        const double pieces = v.optical_depth > 0 ? std::max(1.0, std::ceil(v.optical_depth / max_interval_tau)) : 0;
        if (pieces > double(max_intervals - count))
          throw std::runtime_error("Volume reconstruction interval budget exceeded");
        count += size_t(pieces);
      }
      result.reserve(count);
      for (const auto &v : samples[0].intervals) {
        const size_t pieces = v.optical_depth > 0 ?
                                  size_t(std::max(1.0, std::ceil(v.optical_depth / max_interval_tau))) : 0;
        for (size_t i = 0; i < pieces; ++i) {
          const double front = i ? v.front + (v.back - v.front) * (double(i) / pieces) : v.front;
          const double back = i + 1 == pieces ? v.back :
                                v.front + (v.back - v.front) * (double(i + 1) / pieces);
          if (!(back > front))
            throw std::runtime_error("Volume interval exhausted depth precision");
          const double tau = v.optical_depth * ((back - front) / (v.back - v.front));
          const double alpha = -std::expm1(-tau);
          if (alpha == 1)
            throw std::runtime_error("Volume interval opacity cannot be represented");
          if (alpha > 0)
            result.push_back({front, back, alpha});
        }
      }
      return result;
    }
  }
  double total = 0;
  std::vector<OpticalDepthCurve> curves;
  curves.reserve(samples.size());
  for (const auto &s : samples) {
    total += s.camera.weight / max_weight;
    curves.emplace_back(s.intervals, s.camera.events);
  }
  auto raw = [&](double z, bool before, double *first_weight = nullptr, bool ordered = false) {
    double sum = 0, first = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
      const auto &s = samples[i];
      const double tau = curves[i].at(z, ordered);
      const double t = curves[i].transparency(z, before);
      const double contribution = (s.camera.weight / max_weight) * t * std::exp(-tau);
      if (i == 0)
        first = contribution;
      sum += contribution;
    }
    if (first_weight)
      *first_weight = sum > 0 ? first / sum : .5;
    return sum / total;
  };
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  const bool pair = samples.size() == 2;
  /* Boundary queries sweep forward; recursive fitting retains random access. */
  double before = boundaries.empty() ? 1 : raw(boundaries.front(), true, nullptr, true);
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const double z = boundaries[i];
    double wa = .5;
    const double ta = raw(z, false, pair ? &wa : nullptr, true);
    emit(z, z, before, ta);
    if (i + 1 == boundaries.size())
      break;
    const double end = boundaries[i + 1], mid = z + (end - z) / 2;
    double low = std::numeric_limits<double>::infinity(), high = 0;
    for (size_t j = 0; j < samples.size(); ++j) {
      /* Extinguished rays contribute exactly zero throughout this span.
       * Their rates cannot affect the log-mixture's weighted variance. */
      if (samples[j].camera.weight == 0 || curves[j].transparency(mid, false) == 0)
        continue;
      const double r = curves[j].rate(mid, true);
      if (!std::isfinite(r))
        throw std::invalid_argument("Volume extinction rate overflow");
      low = std::min(low, r);
      high = std::max(high, r);
    }
    auto fit = [&](auto &&self, double a, double b, double ta, double tb,
                   double wa, double wb, int level) -> void {
      /* log(sum w exp(-r z))'' = weighted variance(r) <= range(r)^2/4.
       * Linear interpolation error <= curvature*h^2/8. Exponentiation gives
       * absolute T error <= T(a)*range(r)^2*h^2/32 at EVERY interior depth. */
      const double span = (high - low) * (b - a);
      if (ta == 0 || (ta * span * span / 32 <= tolerance &&
                     tb / ta >= std::exp(-max_interval_tau))) {
        emit(a, b, ta, tb);
        return;
      }
      if (pair && tb / ta >= std::exp(-max_interval_tau)) {
        /* Between boundaries the two rates are constant. Their normalized
         * weights vary monotonically, so max(w0*w1) is at an endpoint or
         * the equal-weight crossing. Recompute this bound for each child:
         * an almost extinguished component must not force tiny tail steps. */
        double variance = .25;
        if ((wa < .5 && wb < .5) || (wa > .5 && wb > .5))
          variance = std::min(.25, std::max(wa * (1 - wa), wb * (1 - wb)) +
                                      32 * std::numeric_limits<double>::epsilon());
        if (ta * span * span * variance / 8 <= tolerance) {
          emit(a, b, ta, tb);
          return;
        }
      }
      const double m = a + (b - a) / 2;
      if (level == 60 || m == a || m == b)
        throw std::runtime_error("Volume interval fitting precision exhausted");
      double wm = .5;
      const double tm = raw(m, false, pair ? &wm : nullptr);
      self(self, a, m, ta, tm, wa, wm, level + 1);
      self(self, m, b, tm, tb, wm, wb, level + 1);
    };
    double wb = .5;
    const double tb = raw(end, true, pair ? &wb : nullptr, true);
    fit(fit, z, end, ta, tb, wa, wb, 0);
    /* The next surface query is this same depth and before-event value. */
    before = tb;
  }
  return result;
}

std::vector<IntervalSample> reconstruct_volume_ids(std::vector<VolumeCameraSample> samples,
                                                  const double tolerance,
                                                  const size_t max_intervals,
                                                  const size_t max_working_bytes)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-2 || !max_intervals)
    throw std::invalid_argument("Invalid deep ID reconstruction budget");
  struct TaggedCurve {
    uint64_t camera;
    double weight;
    std::vector<IntervalSample> spans;
  };
  size_t levels = 1;
  for (size_t n = samples.size(); n > 1; n = (n + 1) / 2)
    ++levels;
  const double allowance = tolerance / levels;
  std::vector<TaggedCurve> working;
  std::unordered_set<uint64_t> cameras;
  double max_weight = 0;
  for (const auto &sample : samples) {
    if (!sample.camera.complete || !cameras.insert(sample.camera.id).second ||
        !std::isfinite(sample.camera.weight) || sample.camera.weight < 0)
      throw std::invalid_argument("Invalid deep ID camera ledger");
    max_weight = std::max(max_weight, sample.camera.weight);
  }
  if (max_weight == 0)
    throw std::invalid_argument("Deep ID pixel requires positive camera weight");
  auto check_size = [&](const std::vector<IntervalSample> &spans,
                        const int object, const size_t added) {
    if (added <= max_intervals - std::min(max_intervals, spans.size())) return;
    std::map<int, size_t> counts;
    for (const auto &span : spans) ++counts[span.object];
    counts[object] += added;
    std::string message = "Deep ID reconstruction interval budget exceeded: intervals=" +
                          std::to_string(spans.size() + added) + ", limit=" +
                          std::to_string(max_intervals) + ", per_object=";
    for (const auto &entry : counts)
      message += std::to_string(entry.first) + ":" + std::to_string(entry.second) + " ";
    throw std::runtime_error(message);
  };
  for (auto &sample : samples) {
    if (sample.camera.weight == 0) continue;
    std::map<int, VolumeCameraSample> objects;
    for (const auto &v : sample.intervals) {
      if (v.object < 0) throw std::invalid_argument("Deep ID interval lacks object index");
      objects[v.object].intervals.push_back(v);
    }
    for (const auto &v : sample.camera.events) {
      if (v.object < 0) throw std::invalid_argument("Deep ID surface lacks object index");
      objects[v.object].camera.events.push_back(v);
    }
    TaggedCurve curve{sample.camera.id, sample.camera.weight / max_weight, {}};
    for (auto &entry : objects) {
      entry.second.camera.id = sample.camera.id;
      entry.second.camera.weight = 1;
      entry.second.camera.complete = true;
      const auto spans = reconstruct_volume({std::move(entry.second)}, allowance / objects.size(),
                                            max_intervals, 0);
      check_size(curve.spans, entry.first, spans.size());
      for (const auto &v : spans)
        curve.spans.push_back({v.front, v.back, v.alpha, entry.first});
    }
    working.push_back(std::move(curve));
  }
  std::vector<VolumeCameraSample>().swap(samples);
  auto bytes = [](const TaggedCurve &c) { return c.spans.capacity() * 128; };
  auto check_memory = [&] {
    size_t retained = 0;
    for (const auto &c : working) {
      if (bytes(c) > max_working_bytes - retained)
        throw std::runtime_error("Deep ID pixel exceeds reconstruction memory budget");
      retained += bytes(c);
    }
  };
  check_memory();
  auto combine = [&](const TaggedCurve &left, const TaggedCurve &right) {
    std::map<int, size_t> objects;
    std::vector<double> boundaries;
    for (const auto *side : {&left, &right})
      for (const auto &v : side->spans) {
        objects.emplace(v.object, 0);
        boundaries.push_back(v.front);
        boundaries.push_back(v.back);
      }
    size_t index = 0;
    for (auto &entry : objects) entry.second = index++;
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    std::vector<VolumeCameraSample> ledgers[2];
    std::vector<OpticalDepthCurve> curves[2];
    for (int side = 0; side < 2; ++side) {
      ledgers[side].resize(objects.size());
      for (const auto &v : (side == 0 ? left : right).spans) {
        auto &ledger = ledgers[side][objects.at(v.object)];
        if (v.front == v.back) ledger.camera.events.push_back({v.front, v.alpha});
        else ledger.intervals.push_back({v.front, v.back, -std::log1p(-v.alpha)});
      }
      curves[side].reserve(objects.size());
      for (auto &ledger : ledgers[side]) {
        std::sort(ledger.intervals.begin(), ledger.intervals.end(), [](const auto &a, const auto &b) {
          return a.front < b.front;
        });
        curves[side].emplace_back(ledger.intervals, ledger.camera.events);
      }
    }
    auto transmission = [&](int side, double z, bool before) {
      double tau = 0, t = side == 0 ? left.weight : right.weight;
      for (const auto &curve : curves[side]) {
        tau += curve.at(z);
        t *= curve.transparency(z, before);
      }
      return t * std::exp(-tau);
    };
    TaggedCurve output{left.camera, left.weight + right.weight, {}};
    std::vector<double> object_transmission(objects.size(), 1);
    std::vector<double> merge_errors(objects.size(), 0), merge_prefixes(objects.size(), 1);
    std::vector<size_t> last_spans(objects.size(), SIZE_MAX);
    auto emit = [&](double front, double back, double alpha, int object) {
      if (alpha <= 0) return;
      if (!std::isfinite(alpha) || alpha > 1 || (front < back && alpha == 1))
        throw std::runtime_error("Deep ID opacity cannot be represented");
      const size_t index = objects.at(object);
      const IntervalSample next{front, back, alpha, object};
      bool merged = false;
      if (last_spans[index] != SIZE_MAX) {
        auto &previous = output.spans[last_spans[index]];
        if (previous.front < previous.back && previous.back == front && front < back) {
          const double combined_alpha = -std::expm1(std::log1p(-previous.alpha) + std::log1p(-alpha));
          const IntervalSample combined{previous.front, back, combined_alpha, object};
          const double error = merge_errors[index] + merge_prefixes[index] *
              merge_error(previous, next, combined) + 32 * std::numeric_limits<double>::epsilon();
          if (combined_alpha <= max_interval_alpha && error <= allowance / (2 * objects.size())) {
            previous = combined;
            merge_errors[index] = error;
            merged = true;
          }
        }
      }
      if (!merged) {
        check_size(output.spans, object, 1);
        last_spans[index] = output.spans.size();
        merge_errors[index] = 0;
        merge_prefixes[index] = object_transmission[index];
        output.spans.push_back(next);
      }
      object_transmission[index] *= 1 - alpha;
    };
    std::vector<double> rates[2];
    rates[0].resize(objects.size()); rates[1].resize(objects.size());
    for (size_t k = 0; k < boundaries.size(); ++k) {
      const double z = boundaries[k];
      double t[2] = {transmission(0, z, true), transmission(1, z, true)};
      /* Deterministic same-depth object order. Each before/after ratio
       * telescopes to the ordinary camera-average surface opacity. */
      for (const auto &entry : objects) {
        const double before = t[0] + t[1];
        for (int side = 0; side < 2; ++side)
          for (const auto &event : ledgers[side][entry.second].camera.events)
            if (event.depth == z) t[side] *= 1 - event.alpha;
        const double after = t[0] + t[1];
        if (before > after) emit(z, z, 1 - after / before, entry.first);
      }
      if (k + 1 == boundaries.size()) break;
      const double end = boundaries[k + 1], mid = z + (end - z) / 2;
      double r[2] = {0, 0};
      for (int side = 0; side < 2; ++side)
        for (size_t i = 0; i < objects.size(); ++i) {
          rates[side][i] = curves[side][i].rate(mid);
          r[side] += rates[side][i];
        }
      auto fit = [&](auto &&self, double a, double b, int level) -> void {
        const double l = transmission(0, a, false), rr = transmission(1, a, false);
        if (l + rr == 0) return;
        const double h = b - a, delta = r[0] - r[1];
        /* r_id(z)=w0(z)*r0_id+w1(z)*r1_id; w0'=-delta*w0*w1.
         * Therefore |(log T_id)''| <= |r0_id-r1_id|*|delta|*max(w0*w1).
         * The weight is monotone between source boundaries, so this
         * variance maximum is at an endpoint or the half-weight crossing.
         * Extinguished camera groups have zero variance and no rate cost.
         * Chord error <= that*h^2/8. exp(-tau) is 1-Lipschitz for
         * nonnegative tau, with Lipschitz constant T_id(a) after its
         * already emitted prefix. This prefix is exact at each completed
         * span endpoint, so opaque tails need not be fitted as visible
         * unit-transmission curves. Share allowance across IDs and tree levels;
         * products and convex averages add their absolute T bounds.
         * Split each level equally between this chord fit and same-ID
         * streaming coalescing (the existing merge_error bound). Each
         * coalesced span retains its accumulated bound, never a fresh
         * allowance per merge. Both preserve span endpoint transmission.
         * Integral hazards preserve every span endpoint exactly. No
         * independent per-object image average (E[AB] != E[A]E[B]). */
        const double w = l / (l + rr);
        double integral, variance = 0;
        if (w == 0 || w == 1 || delta == 0) integral = w * h;
        else {
          const double logit = std::log(w) - std::log1p(-w) - delta * h;
          const double e = std::exp(-std::abs(logit));
          const double wb = logit >= 0 ? 1 / (1 + e) : e / (1 + e);
          variance = .25;
          if ((w < .5 && wb < .5) || (w > .5 && wb > .5))
            variance = std::min(.25, std::max(w * (1-w), wb * (1-wb)) +
                                    32 * std::numeric_limits<double>::epsilon());
          if (delta > 0) integral = -std::log1p(w * std::expm1(-delta * h)) / delta;
          else integral = h + std::log1p((1 - w) * std::expm1(delta * h)) / (-delta);
        }
        integral = std::clamp(integral, 0.0, h);
        double curvature = 0, maximum_tau = 0;
        for (size_t i = 0; i < objects.size(); ++i) {
          curvature = std::max(curvature,
              object_transmission[i] * std::abs(rates[0][i] - rates[1][i]) * std::abs(delta));
          maximum_tau = std::max(maximum_tau,
              rates[0][i] * integral + rates[1][i] * (h - integral));
        }
        if (curvature * variance * h * h / 8 > allowance / (2 * objects.size()) ||
            maximum_tau > max_interval_tau) {
          const double m = a + h / 2;
          if (level == 60 || m == a || m == b)
            throw std::runtime_error("Deep ID fitting precision exhausted");
          self(self, a, m, level + 1); self(self, m, b, level + 1);
          return;
        }
        for (const auto &entry : objects) {
          const size_t i = entry.second;
          const double tau = rates[0][i] * integral + rates[1][i] * (h - integral);
          emit(a, b, -std::expm1(-tau), entry.first);
        }
      };
      fit(fit, z, end, 0);
    }
    return output;
  };
  while (working.size() > 1) {
    size_t count = 0;
    for (size_t i = 0; i < working.size(); i += 2) {
      if (i + 1 == working.size()) working[count++] = std::move(working[i]);
      else {
        auto curve = combine(working[i], working[i + 1]);
        if (bytes(curve) > max_working_bytes)
          throw std::runtime_error("Deep ID merged curve exceeds memory budget");
        working[count++] = std::move(curve);
      }
    }
    working.resize(count); check_memory();
  }
  auto result = std::move(working.front().spans);
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    if (a.front != b.front) return a.front < b.front;
    if (a.back != b.back) return a.back < b.back;
    return a.object < b.object;
  });
  return result;
}

}  // namespace ccl::deep
