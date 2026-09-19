/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "util/defines.h"
#include <cstddef>

CCL_NAMESPACE_BEGIN

/* Rendering settings only. Output paths and compression belong to the host driver. */
struct DeepSettings {
  bool enabled = false;
  bool transparent = false;
  bool volume = false;
  int max_events = 16;
  size_t memory_bytes = size_t(64) * 1024 * 1024;
};

class Scene;
class SessionParams;
void validate_deep_scene(Scene *scene, const SessionParams &params);

CCL_NAMESPACE_END
