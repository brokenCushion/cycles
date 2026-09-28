/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include <cstddef>

namespace ccl::deep {

/* Shared error allocations for capture and FLOAT publication. */
inline constexpr double volume_density_error = 1e-7;
inline constexpr double volume_reconstruction_error = 5e-8;
inline constexpr double volume_coefficient_error = 4e-8;

struct VolumeInterval {
  double front, back;
  /* Integrated scalar extinction sigma_t * physical ray length, NOT axial length. */
  double optical_depth;
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
};

/* Reference only: averages sample transmittance, then fits nonoverlapping
 * exponential intervals. Surface steps have front == back. Error is absolute
 * transmittance error at EVERY depth, bounded using log-mixture curvature.
 * Optional streaming reduction adds at most reduction_tolerance absolute curve
 * error, preserves merged-segment endpoint transmittance and never merges
 * surface steps or gaps.
 * Limits fail explicitly. Renderer capture splits its shared error allocation
 * between fitting and reduction; callers must budget their sum. */
std::vector<IntervalSample> reconstruct_volume(const std::vector<VolumeCameraSample> &samples,
                                              double tolerance = volume_reconstruction_error,
                                              size_t max_intervals = 65536,
                                              double reduction_tolerance = 0);

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
