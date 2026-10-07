/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "util/defines.h"
#include <cstddef>
#include <vector>
#include <string>
#include <utility>
#include <cstdint>

CCL_NAMESPACE_BEGIN

/* Rendering settings only. Output paths and compression belong to the host driver. */
struct DeepSettings {
  bool enabled = false;
  bool transparent = false;
  bool volume = false;
  /* Derived by scene preflight; not a host-facing rendering mode. */
  bool volume_grid = false;
  float error = 1e-3f; /* 0 = strict, preserving legacy payload and headers. */
  float z_tolerance = 1e-4f; /* Relative surface depth span; strict forces zero. */
  std::vector<std::pair<uint32_t, std::string>> object_manifest;
  bool ids = false; /* Optional per-sample object identity; never a flat pass. */
  int samples = 0; /* 0 = all accepted beauty samples; otherwise their first N. */
  int max_events = 16;
  size_t memory_bytes = size_t(64) * 1024 * 1024;
};

class Scene;
class SessionParams;
void validate_deep_scene(Scene *scene, SessionParams &params);

CCL_NAMESPACE_END
