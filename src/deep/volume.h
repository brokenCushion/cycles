/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include <cstddef>
#include <atomic>
#include <chrono>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace ccl::deep {

/* Aggregate host-worker elapsed times, not additive frame wall time. Nested
 * pixel time includes read and fitting; subtract those for ledger/decode time.
 * Diagnostics never enter the EXR header or affect numerical decisions. */
struct ExportStatistics {
  enum Stage { Read, Staging, DensityFit, MixtureFit, Pixel, Quantize, Serialize, Count };
  std::atomic<uint64_t> nanoseconds[Count]{};
  double seconds(Stage stage) const { return nanoseconds[stage].load() * 1e-9; }
};
struct ExportTimer {
  ExportStatistics *statistics;
  ExportStatistics::Stage stage;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~ExportTimer()
  {
    if (statistics)
      statistics->nanoseconds[stage].fetch_add(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
  }
};

/* Shared error allocations for capture and FLOAT publication. */
inline constexpr double volume_density_error = 1e-7;
inline constexpr double volume_reconstruction_error = 5e-8;
inline constexpr double volume_coefficient_error = 4e-8;

/* One absolute transmittance bound E. Zero selects the legacy strict path.
 * Non-strict: reserve F=1e-6 for FLOAT publication/coefficient rounding, then
 * eps_ray=(E-F)/2 and eps_host=(E-F)/2. In 3a the existing host cubic fitter
 * spends eps_ray (no device compression yet). Camera averaging is convex,
 * so its per-ray bound survives averaging. Split eps_host equally between
 * mixture fitting/reduction and export coalescing. Balanced averaging divides
 * its allocation across levels; coalescing checks the complete curve.
 * Thus eps_ray + eps_mixture + eps_coalesce + F = E, not one E per merge.
 * F includes the fixed 4e-8 coefficient reserve. Surface reduction instead
 * spends E-F on its globally bounded delayed steps, plus F for FLOAT export.
 * Strict returns the original constants and preserves original arithmetic,
 * including export allowance/coalescing and the optional 1e-3 surface mode. */
struct ErrorBudget {
  double density, reconstruction, coalescing, publication, effective;
};
inline ErrorBudget error_budget(const float error)
{
  if (error == 0)
    return {volume_density_error, volume_reconstruction_error, 2.5e-7,
            1e-6 - volume_density_error - volume_reconstruction_error - volume_coefficient_error,
            1e-6};
  if (!std::isfinite(error) || error <= 1e-6 || error > 1e-2)
    throw std::invalid_argument("Deep error must be strict or greater than 1e-6, up to 0.01");
  const double remainder = double(error) - 1e-6;
  return {remainder / 2, remainder / 4, remainder / 4,
          remainder / 4 + 1e-6 - volume_coefficient_error, double(error)};
}

struct VolumeInterval {
  double front, back;
  /* Integrated scalar extinction sigma_t * physical ray length, NOT axial length. */
  double optical_depth;
  int object = -1;
};

struct VolumeCameraSample {
  CameraSample camera;
  /* Overlapping intervals add extinction. A camera-inside interval starts at
   * the near clip depth. Piecewise homogeneous input only. */
  std::vector<VolumeInterval> intervals;
};

/* Known linear extinction over an axial-depth interval. These are supplied
 * profiles, not a claim that arbitrary shader samples bound the density. */
struct LinearDensityInterval {
  double front, back;
  double sigma_front, sigma_back;
  double physical_length_per_depth = 1;
};

/* Bernstein controls already multiplied by physical cell length. Their mean
 * is the cell optical depth; front/back are axial camera depths. */
struct CubicDensityInterval {
  double front, back;
  double optical_depth[4];
};

/* Fit native cell records with constant-extinction intervals. The tolerance
 * bounds absolute transmittance error in exact arithmetic, including overlaps.
 * FLOAT coefficient/export errors require a separate budget. */
std::vector<VolumeInterval> integrate_cubic_density(
    const std::vector<CubicDensityInterval> &segments,
    double tolerance = volume_density_error,
    size_t max_intervals = 65536);

/* Reference integration. In exact arithmetic, preserves each segment's total
 * optical depth and bounds transmittance error at every interior depth. Double
 * precision is numerically qualified by density_test, not an interval-arithmetic
 * certificate for arbitrary dynamic range. Overlaps add
 * extinction. Capacity is preflighted before allocating output; failure throws.
 * The caller must budget this error separately from reconstruction/FLOAT export. */
std::vector<VolumeInterval> integrate_linear_density(
    const std::vector<LinearDensityInterval> &segments,
    double tolerance = 1e-7,
    size_t max_intervals = 65536);

struct IntervalSample {
  double front, back, alpha;
  int object = -1;
};

/* Reference only: averages sample transmittance, then fits nonoverlapping
 * exponential intervals. Surface steps have front == back. Error is absolute
 * transmittance error at EVERY depth, bounded using log-mixture curvature.
 * Optional streaming reduction adds at most reduction_tolerance absolute curve
 * error, preserves merged-segment endpoint transmittance and never merges
 * surface steps or gaps.
 * Limits fail explicitly. Renderer capture splits its shared error allocation
 * between fitting and reduction; callers must budget their sum. Large camera
 * populations share this allowance across balanced averaging levels. The
 * optional byte budget bounds retained source/index storage, with single-pair
 * and output scratch reserved independently by the renderer. */
std::vector<IntervalSample> reconstruct_volume(std::vector<VolumeCameraSample> samples,
                                              double tolerance = volume_reconstruction_error,
                                              size_t max_intervals = 65536,
                                              double reduction_tolerance = 0,
                                              size_t max_working_bytes = std::numeric_limits<size_t>::max());

/* Object-preserving convex camera average. The same-ID hazard is averaged
 * with transmission weights, not as independent per-object image averages.
 * Different IDs remain overlapping intervals; the sum of their hazards is the
 * ordinary mixture hazard. Fitting shares one absolute T allowance across tree
 * levels and objects. Capacity/memory exhaustion is an explicit failure. */
std::vector<IntervalSample> reconstruct_volume_ids(std::vector<VolumeCameraSample> samples,
                                                  double tolerance,
                                                  size_t max_intervals,
                                                  size_t max_working_bytes);

/* Exact maximum difference between two ordered, nonoverlapping exponential
 * curves, including surface discontinuities and interior stationary points. */
double interval_curve_error(const std::vector<IntervalSample> &a,
                            const std::vector<IntervalSample> &b);
double interval_transmittance(const std::vector<IntervalSample> &samples,
                              double depth,
                              bool before = false);
/* Bounded linear pass over an already reconstructed curve. Preserves endpoint
 * transmittance, surface steps and gaps. Each merged span accumulates its own
 * whole-curve error bound; the caller budgets this separately from fitting. */
std::vector<IntervalSample> reduce_interval_curve(const std::vector<IntervalSample> &source,
                                                 double tolerance);
}  // namespace ccl::deep
