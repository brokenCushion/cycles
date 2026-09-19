/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "util/defines.h"

CCL_NAMESPACE_BEGIN

/* Bernstein coefficients of density along a ray segment inside one trilinear
 * interpolation cell. Nonnegative corners and endpoints inside the unit cell
 * give nonnegative coefficients without subtracting nearly equal powers.
 * Callers validate finite inputs and account for numerical/export error.
 * This header has no allocation, recursion, host pointers or shader dependency. */
template<typename T> struct DeepCubicDensity {
  T b[4];
};

template<typename T>
ccl_device DeepCubicDensity<T> deep_trilinear_density(
    const ccl_private T *corners, const ccl_private T *start, const ccl_private T *end)
{
  DeepCubicDensity<T> result = {};
  /* Corner index is x + 2*y + 4*z. */
  for (int corner = 0; corner < 8; ++corner) {
    const T x0 = (corner & 1) ? start[0] : T(1) - start[0];
    const T y0 = (corner & 2) ? start[1] : T(1) - start[1];
    const T z0 = (corner & 4) ? start[2] : T(1) - start[2];
    const T x1 = (corner & 1) ? end[0] : T(1) - end[0];
    const T y1 = (corner & 2) ? end[1] : T(1) - end[1];
    const T z1 = (corner & 4) ? end[2] : T(1) - end[2];
    const T value = corners[corner];
    result.b[0] += value * x0 * y0 * z0;
    result.b[1] += value * (x1 * y0 * z0 + x0 * y1 * z0 + x0 * y0 * z1) / T(3);
    result.b[2] += value * (x0 * y1 * z1 + x1 * y0 * z1 + x1 * y1 * z0) / T(3);
    result.b[3] += value * x1 * y1 * z1;
  }
  return result;
}

/* Polar form: equal u,v,w evaluates the density, while mixed arguments give
 * Bernstein controls for a clipped subsegment. All interpolation stays convex. */
template<typename T>
ccl_device T deep_density_blossom(const DeepCubicDensity<T> curve, const T u, const T v, const T w)
{
  const T a = (T(1) - u) * curve.b[0] + u * curve.b[1];
  const T b = (T(1) - u) * curve.b[1] + u * curve.b[2];
  const T c = (T(1) - u) * curve.b[2] + u * curve.b[3];
  const T d = (T(1) - v) * a + v * b;
  const T e = (T(1) - v) * b + v * c;
  return (T(1) - w) * d + w * e;
}

template<typename T>
ccl_device DeepCubicDensity<T> deep_density_restrict(const DeepCubicDensity<T> curve,
                                                   const T u,
                                                   const T v)
{
  return {{deep_density_blossom(curve, u, u, u),
           deep_density_blossom(curve, u, u, v),
           deep_density_blossom(curve, u, v, v),
           deep_density_blossom(curve, v, v, v)}};
}

template<typename T>
ccl_device T deep_density_integral(const DeepCubicDensity<T> curve, const T physical_length)
{
  return physical_length *
         ((curve.b[0] + curve.b[1]) + (curve.b[2] + curve.b[3])) / T(4);
}

/* The primitive is degree four with controls (0, b0, b0+b1, b0+b1+b2,
 * sum(b))/4 * length. Subtract its endpoint chord and apply the convex-hull
 * property. This bounds optical-depth error at EVERY interior depth in exact
 * arithmetic. Nonnegative optical depths make it an absolute transmittance
 * error bound too, since exp(-tau) is 1-Lipschitz on tau >= 0. */
template<typename T>
ccl_device T deep_density_chord_error(const DeepCubicDensity<T> curve, const T physical_length)
{
  const T integral = deep_density_integral(curve, physical_length);
  T prefix = T(0), error = T(0);
  for (int i = 1; i < 4; ++i) {
    prefix += curve.b[i - 1];
    const T difference = (physical_length * prefix - T(i) * integral) / T(4);
    const T absolute = difference < T(0) ? -difference : difference;
    error = error > absolute ? error : absolute;
  }
  return error;
}

CCL_NAMESPACE_END
