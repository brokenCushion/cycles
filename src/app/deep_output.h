/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "deep/capture.h"
#include "util/string.h"
#include <functional>

CCL_NAMESPACE_BEGIN
class Scene;
class SessionParams;
/* Throws before render if the experimental M3 contract cannot be established. */
void validate_deep_scene(Scene *scene, const SessionParams &params, bool transparent = false);
void write_deep_capture(const deep::OpaqueCapture &capture,
                        const string &path,
                        const string &records_path,
                        const string &beauty_path,
                        bool reduce,
                        const std::function<bool()> &cancelled);
CCL_NAMESPACE_END
