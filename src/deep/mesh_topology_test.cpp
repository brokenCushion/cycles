/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CCL_NAMESPACE_BEGIN
#  define CCL_NAMESPACE_BEGIN namespace ccl {
#  define CCL_NAMESPACE_END }
#endif
#include "deep/mesh_topology.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using ccl::deep::open_mesh_boundary_edges;

static void check(const bool ok, const char *message)
{
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(1);
  }
}

/* Outward-oriented unit cube: 8 shared vertices, 12 triangles. */
static const float cube_positions[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                           {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
static const int cube_faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                     {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};

static std::vector<int> cube_triangles(const bool open_top)
{
  std::vector<int> tris;
  for (int f = 0; f < 6; ++f) {
    if (open_top && f == 1)
      continue;
    const int *q = cube_faces[f];
    tris.insert(tris.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
  }
  return tris;
}

static std::vector<float> padded(const float (*p)[3], const size_t n, const size_t stride)
{
  std::vector<float> out(n * stride, -7.0f);
  for (size_t i = 0; i < n; ++i)
    for (int a = 0; a < 3; ++a)
      out[i * stride + a] = p[i][a];
  return out;
}

static size_t count(const std::vector<int> &tris, const std::vector<float> &pos, size_t stride)
{
  return open_mesh_boundary_edges(tris.data(), tris.size() / 3, pos.data(), stride);
}

int main()
{
  const auto positions = padded(cube_positions, 8, 4);
  check(count(cube_triangles(false), positions, 4) == 0, "closed cube reported open");
  check(count(cube_triangles(true), positions, 4) == 4, "open-top cube must have 4 boundary edges");
  check(count(cube_triangles(false), padded(cube_positions, 8, 3), 3) == 0,
        "closed cube with packed positions reported open");

  /* Two disjoint closed cubes in one mesh (overlapping shells) stay closed. */
  {
    std::vector<int> tris = cube_triangles(false);
    for (const int v : cube_triangles(false))
      tris.push_back(v + 8);
    float shells[16][3];
    for (int i = 0; i < 8; ++i)
      for (int a = 0; a < 3; ++a) {
        shells[i][a] = cube_positions[i][a];
        shells[i + 8][a] = cube_positions[i][a] + (a == 0 ? 0.5f : 0.25f);
      }
    const auto shell_positions = padded(shells, 16, 4);
    check(count(tris, shell_positions, 4) == 0, "overlapping closed shells reported open");
    check(ccl::deep::mesh_has_proper_self_intersection(
              tris.data(), tris.size() / 3, shell_positions.data(), 4),
          "Crossing closed shells were not detected");
  }

  /* Split vertices: every triangle owns its corners (no shared topology).
   * Welding by exact position closes the cube; -0 and +0 coincide. */
  {
    const auto closed = cube_triangles(false);
    std::vector<float> split;
    std::vector<int> tris;
    for (size_t i = 0; i < closed.size(); ++i) {
      const float *p = cube_positions[closed[i]];
      const float x = (p[0] == 0 && i % 2) ? -0.0f : p[0];
      split.insert(split.end(), {x, p[1], p[2], 0.0f});
      tris.push_back(int(i));
    }
    check(count(tris, split, 4) == 0, "split-vertex closed cube reported open");
    std::vector<int> open(tris.begin(), tris.begin() + 6);
    open.insert(open.end(), tris.begin() + 12, tris.end()); /* drop the top face */
    check(count(open, split, 4) == 4, "split-vertex open cube must have 4 boundary edges");
  }

  /* A single triangle has three boundary edges; an empty mesh has none. */
  {
    const std::vector<int> tri = {0, 1, 2};
    check(count(tri, positions, 4) == 3, "single triangle must have 3 boundary edges");
    check(open_mesh_boundary_edges(nullptr, 0, nullptr, 4) == 0, "empty mesh reported open");
    check(open_mesh_boundary_edges(tri.data(), 1, nullptr, 4) == 3,
          "index-only count must not need positions");
  }

  /* Non-manifold edge shared by three triangles is not a boundary edge. */
  {
    const std::vector<int> fan = {0, 1, 2, 1, 0, 3, 0, 1, 4};
    const float p[5][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}};
    check(count(fan, padded(p, 5, 4), 4) == 6, "fan must have 6 boundary edges");
  }

  std::printf("cycles_deep_mesh_topology: OK\n");
  return 0;
}
