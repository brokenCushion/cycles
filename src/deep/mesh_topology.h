/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Open-boundary detection for deep volume meshes. Diagnostics only: the count
 * never changes capture or output. A ray that leaves a medium through a hole
 * finds no exit crossing, so its interval stays open to the camera clip end. */

#include "kernel/deep/volume_boundary.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ccl::deep {

namespace detail {
inline uint64_t undirected_edge_key(uint32_t a, uint32_t b)
{
  return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
}

/* Undirected edges used by exactly one triangle; degenerate edges are ignored. */
template<class Vertex> size_t single_use_edges(const int *triangles, const size_t count, Vertex vertex)
{
  std::unordered_map<uint64_t, uint32_t> uses;
  uses.reserve(count * 3);
  for (size_t i = 0; i < count; ++i)
    for (int j = 0; j < 3; ++j) {
      const uint32_t a = vertex(triangles[3 * i + j]);
      const uint32_t b = vertex(triangles[3 * i + (j + 1) % 3]);
      if (a != b)
        ++uses[undirected_edge_key(a, b)];
    }
  size_t boundary = 0;
  for (const auto &edge : uses)
    boundary += edge.second == 1;
  return boundary;
}

struct PositionKey {
  uint32_t bits[3];
  bool operator==(const PositionKey &other) const
  {
    return bits[0] == other.bits[0] && bits[1] == other.bits[1] && bits[2] == other.bits[2];
  }
};
struct PositionHash {
  size_t operator()(const PositionKey &key) const
  {
    uint64_t h = 1469598103934665603ull;
    for (const uint32_t bits : key.bits)
      h = (h ^ bits) * 1099511628211ull;
    return size_t(h);
  }
};
}  // namespace detail

/* Boundary edges of an indexed triangle mesh: edges used by exactly one
 * triangle after welding by vertex index. A mesh with split vertices is then
 * welded by exact position (+0 and -0 coincide), so only real holes count.
 * positions holds `stride` floats per vertex, xyz first. Linear expected time;
 * indices must already be validated against the vertex count. */
inline size_t open_mesh_boundary_edges(const int *triangles,
                                       const size_t count,
                                       const float *positions,
                                       const size_t stride)
{
  const size_t by_index = detail::single_use_edges(
      triangles, count, [](const int v) { return uint32_t(v); });
  if (by_index == 0 || !positions)
    return by_index;
  std::unordered_map<detail::PositionKey, uint32_t, detail::PositionHash> welded;
  welded.reserve(count * 3);
  std::unordered_map<int, uint32_t> canonical;
  canonical.reserve(count * 3);
  for (size_t i = 0; i < 3 * count; ++i) {
    const int v = triangles[i];
    if (canonical.count(v))
      continue;
    detail::PositionKey key;
    for (int axis = 0; axis < 3; ++axis) {
      float value = positions[size_t(v) * stride + axis];
      if (value == 0.0f)
        value = 0.0f; /* -0 -> +0 */
      std::memcpy(&key.bits[axis], &value, sizeof(float));
    }
    canonical[v] = welded.emplace(key, uint32_t(welded.size())).first->second;
  }
  return detail::single_use_edges(
      triangles, count, [&](const int v) { return canonical.find(v)->second; });
}

/* Bounded sweep for proper crossings. Boundary-touching and coplanar
 * triangles are outside this diagnostic. Large meshes and dense candidate
 * sets are only checked within the preflight budget. */
inline bool mesh_has_proper_self_intersection(const int *triangles,
                                               const size_t count,
                                               const float *positions,
                                               const size_t stride)
{
  constexpr size_t triangle_limit = 8192;
  constexpr size_t pair_limit = 65536;
  if (!positions || count < 2 || count > triangle_limit)
    return false;
  struct Bounds {
    size_t triangle;
    float lower[3], upper[3];
  };
  std::vector<Bounds> boxes;
  boxes.reserve(count);
  const auto vertex = [&](const size_t triangle, const int corner) {
    return positions + size_t(triangles[3 * triangle + corner]) * stride;
  };
  for (size_t i = 0; i < count; ++i) {
    const float *a = vertex(i, 0), *b = vertex(i, 1), *c = vertex(i, 2);
    Bounds box{i, {}, {}};
    for (int axis = 0; axis < 3; ++axis) {
      box.lower[axis] = std::min({a[axis], b[axis], c[axis]});
      box.upper[axis] = std::max({a[axis], b[axis], c[axis]});
    }
    boxes.push_back(box);
  }
  std::sort(boxes.begin(), boxes.end(), [](const Bounds &a, const Bounds &b) {
    return a.lower[0] < b.lower[0] || (a.lower[0] == b.lower[0] && a.triangle < b.triangle);
  });
  const auto crosses = [&](const size_t a, const size_t b) {
    double target[3][3];
    for (int corner = 0; corner < 3; ++corner)
      for (int axis = 0; axis < 3; ++axis)
        target[corner][axis] = vertex(b, corner)[axis];
    for (int edge = 0; edge < 3; ++edge) {
      const float *p = vertex(a, edge), *q = vertex(a, (edge + 1) % 3);
      double origin[3], direction[3];
      for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = p[axis];
        direction[axis] = double(q[axis]) - double(p[axis]);
      }
      double t, u, v;
      bool back;
      constexpr double tolerance = 1e-10;
      if (deep_volume_triangle(origin, direction, target, t, u, v, back) &&
          t > tolerance && t < 1 - tolerance && u > tolerance && v > tolerance &&
          u + v < 1 - tolerance)
        return true;
    }
    return false;
  };
  size_t pairs = 0;
  for (size_t i = 0; i < boxes.size(); ++i) {
    const Bounds &a = boxes[i];
    for (size_t j = i + 1; j < boxes.size() && boxes[j].lower[0] <= a.upper[0]; ++j) {
      if (++pairs > pair_limit)
        return false;
      const Bounds &b = boxes[j];
      if (b.lower[1] <= a.upper[1] && a.lower[1] <= b.upper[1] &&
          b.lower[2] <= a.upper[2] && a.lower[2] <= b.upper[2] &&
          (crosses(a.triangle, b.triangle) || crosses(b.triangle, a.triangle)))
        return true;
    }
  }
  return false;
}

}  // namespace ccl::deep
