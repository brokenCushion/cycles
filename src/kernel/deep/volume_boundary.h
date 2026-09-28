/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "util/defines.h"

CCL_NAMESPACE_BEGIN

/* Double-precision Pluecker test, matching Cycles' triangle barycentric convention.
 * Native FLOAT edge tolerances can report a crossing outside the triangle, or
 * lose one end of an interval narrower than a FLOAT distance ULP. Volume pairing
 * needs the actual crossing. All inputs are finite, static geometry. */
ccl_device_inline bool deep_volume_triangle(const double origin[3],
                                            const double direction[3],
                                            const double vertices[3][3],
                                            double &t, double &u, double &v, bool &back)
{
  double p[3][3], edge[3][3];
  for (int axis = 0; axis < 3; ++axis) {
    for (int i = 0; i < 3; ++i)
      p[i][axis] = vertices[i][axis] - origin[axis];
    edge[0][axis] = p[2][axis] - p[0][axis];
    edge[1][axis] = p[0][axis] - p[1][axis];
    edge[2][axis] = p[1][axis] - p[2][axis];
  }
  double weight[3];
  const int a[3] = {2, 0, 1}, b[3] = {0, 1, 2};
  for (int i = 0; i < 3; ++i) {
    double sum[3];
    for (int axis = 0; axis < 3; ++axis)
      sum[axis] = p[a[i]][axis] + p[b[i]][axis];
    weight[i] = (edge[i][1] * sum[2] - edge[i][2] * sum[1]) * direction[0] +
                (edge[i][2] * sum[0] - edge[i][0] * sum[2]) * direction[1] +
                (edge[i][0] * sum[1] - edge[i][1] * sum[0]) * direction[2];
  }
  if (!((weight[0] >= 0 && weight[1] >= 0 && weight[2] >= 0) ||
        (weight[0] <= 0 && weight[1] <= 0 && weight[2] <= 0)))
    return false;
  const double normal[3] = {
      edge[1][1] * edge[0][2] - edge[1][2] * edge[0][1],
      edge[1][2] * edge[0][0] - edge[1][0] * edge[0][2],
      edge[1][0] * edge[0][1] - edge[1][1] * edge[0][0]};
  const double denominator = normal[0] * direction[0] + normal[1] * direction[1] +
                             normal[2] * direction[2];
  const double total = weight[0] + weight[1] + weight[2];
  if (denominator == 0 || total == 0)
    return false;
  t = (p[0][0] * normal[0] + p[0][1] * normal[1] + p[0][2] * normal[2]) / denominator;
  u = weight[0] / total;
  v = weight[1] / total;
  back = denominator < 0;
  return true;
}

CCL_NAMESPACE_END
