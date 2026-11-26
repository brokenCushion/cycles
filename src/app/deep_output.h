/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "app/oiio_output_driver.h"

CCL_NAMESPACE_BEGIN

/* Standalone file adapter. Capture and reconstruction belong to PathTrace. */
class DeepOutputDriver final : public OIIOOutputDriver {
 public:
  DeepOutputDriver(string_view beauty_path,
                   string_view pass,
                   LogFunction log,
                   string_view deep_path,
                   string_view records_path,
                   bool reduce);
  void write_render_tile(const Tile &tile) override;
  bool supports_deep_output() const override
  {
    return true;
  }
  void write_deep_render_tile(const DeepTile &tile) override;
  bool deep_camera_records() const override
  {
    return !records_path_.empty() || !deep_diagnostics_path().empty();
  }
  bool deep_surface_reduce() const override
  {
    return reduce_;
  }

 private:
  string deep_path_, records_path_;
  bool reduce_;
};

CCL_NAMESPACE_END
