/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "deep/capture.h"
#include "util/string.h"

CCL_NAMESPACE_BEGIN
class Scene;
class SessionParams;
/* Throws before render if the experimental M3 contract cannot be established. */
void validate_deep_scene(Scene *scene, const SessionParams &params);
void write_deep_capture(const deep::OpaqueCapture &capture,
                        const string &path,
                        const string &records_path);
CCL_NAMESPACE_END
