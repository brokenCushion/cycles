/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "deep/reconstruction.h"
#include <cstddef>

namespace ccl::deep {

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
 * Limits fail explicitly; this is not yet connected to renderer capture. */
std::vector<IntervalSample> reconstruct_volume(const std::vector<VolumeCameraSample> &samples,
                                              double tolerance = 2e-7,
                                              size_t max_intervals = 65536);

/* Exact maximum difference between two ordered, nonoverlapping exponential
 * curves, including surface discontinuities and interior stationary points. */
double interval_curve_error(const std::vector<IntervalSample> &a,
                            const std::vector<IntervalSample> &b);
double interval_transmittance(const std::vector<IntervalSample> &samples,
                              double depth,
                              bool before = false);
}  // namespace ccl::deep
