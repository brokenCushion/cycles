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
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace ccl::deep {
std::vector<VolumeInterval> integrate_cubic_density(
    const std::vector<CubicDensityInterval> &segments,
    const double tolerance,
    const size_t max_intervals)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-3 || !max_intervals)
    throw std::invalid_argument("Invalid cubic integration budget");
  std::vector<std::pair<double, int>> boundaries;
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
        if (deep_density_chord_error(clipped, b - a) <= budget) {
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
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-3 || !max_intervals)
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
  struct Run {
    size_t begin, end;
    double total;
  };
  std::vector<Run> runs;

  explicit OpticalDepthCurve(const std::vector<VolumeInterval> &source) : intervals(source)
  {
    size_t count = 0;
    for (size_t i = 0; i < intervals.size(); ++i)
      count += i == 0 || intervals[i].front < intervals[i - 1].back;
    runs.reserve(count);
    prefix.reserve(intervals.size());
    for (size_t i = 0; i < intervals.size(); ++i) {
      if (i == 0 || intervals[i].front < intervals[i - 1].back)
        runs.push_back({i, i, 0});
      Run &run = runs.back();
      prefix.push_back(run.total);
      run.total += intervals[i].optical_depth;
      ++run.end;
    }
  }

  size_t index(const Run &run, double z) const
  {
    return std::lower_bound(intervals.begin() + run.begin, intervals.begin() + run.end, z,
                            [](const VolumeInterval &v, double depth) {
                              return v.back < depth;
                            }) - intervals.begin();
  }

  double at(double z) const
  {
    double tau = 0;
    for (const Run &run : runs) {
      const size_t i = index(run, z);
      if (i == run.end)
        tau += run.total;
      else {
        const auto &v = intervals[i];
        tau += prefix[i] + v.optical_depth *
                              std::clamp((z - v.front) / (v.back - v.front), 0.0, 1.0);
      }
    }
    return tau;
  }

  double rate(double z) const
  {
    double rate = 0;
    for (const Run &run : runs) {
      const size_t i = index(run, z);
      if (i != run.end && intervals[i].front < z && z < intervals[i].back)
        rate += intervals[i].optical_depth / (intervals[i].back - intervals[i].front);
    }
    return rate;
  }
};
}  // namespace

std::vector<IntervalSample> reduce_interval_curve(const std::vector<IntervalSample> &source,
                                                 const double tolerance)
{
  validate_curve(source);
  if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1e-3)
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
        if (alpha < 1) {
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

std::vector<IntervalSample> reconstruct_volume(const std::vector<VolumeCameraSample> &samples,
                                              double tolerance,
                                              size_t max_intervals,
                                              double reduction_tolerance)
{
  if (!std::isfinite(tolerance) || tolerance <= 0 || tolerance > 1e-3 || !max_intervals ||
      !std::isfinite(reduction_tolerance) || reduction_tolerance < 0 ||
      reduction_tolerance > 1e-3)
    throw std::invalid_argument("Invalid volume reconstruction budget");
  std::unordered_set<uint64_t> ids;
  std::vector<double> boundaries;
  double max_weight = 0;
  for (const auto &s : samples) {
    if (!s.camera.complete || !ids.insert(s.camera.id).second ||
        !std::isfinite(s.camera.weight) || s.camera.weight < 0)
      throw std::invalid_argument("Invalid or incomplete volume camera sample");
    max_weight = std::max(max_weight, s.camera.weight);
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
  std::vector<IntervalSample> result;
  double last_error = 0, last_prefix = 1;
  /* A small bounded source window can tighten the triangle-inequality merge
   * bound. Longer chains retain the conservative bound; never reset its budget. */
  std::array<IntervalSample, 64> merge_source;
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
        if (combined_alpha < 1) {
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
                  prefix, -std::log1p(-s.alpha) / (s.back - s.front),
                  std::exp(-rate * (s.front - combined.front)), rate, s.back - s.front));
              prefix *= 1 - s.alpha;
            }
            error = last_prefix * exact +
                    32 * (merge_count + 1) * std::numeric_limits<double>::epsilon();
          }
          if (error <= reduction_tolerance) {
            result.back() = combined;
            last_error = error;
            if (merge_count && merge_count < merge_source.size())
              merge_source[merge_count++] = next;
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
        const double pieces = v.optical_depth > 0 ? std::max(1.0, std::ceil(v.optical_depth / 16.0)) : 0;
        if (pieces > double(max_intervals - count))
          throw std::runtime_error("Volume reconstruction interval budget exceeded");
        count += size_t(pieces);
      }
      result.reserve(count);
      for (const auto &v : samples[0].intervals) {
        const size_t pieces = v.optical_depth > 0 ?
                                  size_t(std::max(1.0, std::ceil(v.optical_depth / 16.0))) : 0;
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
    curves.emplace_back(s.intervals);
  }
  auto raw = [&](double z, bool before) {
    double sum = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
      const auto &s = samples[i];
      const double tau = curves[i].at(z);
      double t = 1;
      for (const auto &e : s.camera.events)
        if (e.depth < z || (!before && e.depth == z))
          t *= 1 - e.alpha;
      sum += (s.camera.weight / max_weight) * t * std::exp(-tau);
    }
    return sum / total;
  };
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const double z = boundaries[i];
    emit(z, z, raw(z, true), raw(z, false));
    if (i + 1 == boundaries.size())
      break;
    const double end = boundaries[i + 1], mid = z + (end - z) / 2;
    double low = std::numeric_limits<double>::infinity(), high = 0;
    for (size_t j = 0; j < samples.size(); ++j) {
      if (samples[j].camera.weight == 0)
        continue;
      const double r = curves[j].rate(mid);
      if (!std::isfinite(r))
        throw std::invalid_argument("Volume extinction rate overflow");
      low = std::min(low, r);
      high = std::max(high, r);
    }
    std::function<void(double, double, double, double, int)> fit;
    fit = [&](double a, double b, double ta, double tb, int level) {
      /* log(sum w exp(-r z))'' = weighted variance(r) <= range(r)^2/4.
       * Linear interpolation error <= curvature*h^2/8. Exponentiation gives
       * absolute T error <= T(a)*range(r)^2*h^2/32 at EVERY interior depth. */
      const double span = (high - low) * (b - a);
      if (ta == 0 || (ta * span * span / 32 <= tolerance && tb / ta > 1e-7)) {
        emit(a, b, ta, tb);
        return;
      }
      const double m = a + (b - a) / 2;
      if (level == 60 || m == a || m == b)
        throw std::runtime_error("Volume interval fitting precision exhausted");
      const double tm = raw(m, false);
      fit(a, m, ta, tm, level + 1);
      fit(m, b, tm, tb, level + 1);
    };
    fit(z, end, raw(z, false), raw(end, true), 0);
  }
  return result;
}
}  // namespace ccl::deep
