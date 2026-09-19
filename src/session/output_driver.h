/* SPDX-FileCopyrightText: 2021-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#ifdef WITH_CYCLES_DEEP_OPAQUE
#  include "deep/volume.h"
#  include <stdexcept>
#endif
#include "util/math.h"
#include "util/string.h"
#include "util/types.h"

CCL_NAMESPACE_BEGIN

/* Output driver for reading render buffers.
 *
 * Host applications implement this interface for outputting render buffers for offline rendering.
 * Drivers can be used to copy the buffers into the host application or write them directly to
 * disk. This interface may also be used for interactive display, however the DisplayDriver is more
 * efficient for that purpose.
 */
class OutputDriver {
 public:
  OutputDriver() = default;
  virtual ~OutputDriver() = default;

  class Tile {
   public:
    Tile(const int2 offset,
         const int2 size,
         const int2 full_size,
         const string_view layer,
         const string_view view)
        : offset(offset), size(size), full_size(full_size), layer(layer), view(view)
    {
    }
    virtual ~Tile() = default;

    const int2 offset;
    const int2 size;
    const int2 full_size;
    const string layer;
    const string view;

    virtual bool get_pass_pixels(const string_view pass_name,
                                 const int num_channels,
                                 float *pixels) const = 0;
    virtual bool set_pass_pixels(const string_view pass_name,
                                 const int num_channels,
                                 const float *pixels) const = 0;
  };

#ifdef WITH_CYCLES_DEEP_OPAQUE
  /* Completed, full-frame deep data. Coordinates follow render buffers (Y up).
   * Depths are positive camera-axis distances; equal front/back denotes a surface.
   * Pull one pixel at a time to bound reconstruction memory. Returned vectors are
   * owned by the caller. Serialize reads on the callback thread; the tile itself
   * is valid only during the callback. Do not reset the session from this callback.
   * Current contract has matching data/display windows with origin (0, 0).
   * No RGB channels are supplied: alpha describes averaged camera visibility. */
  class DeepTile {
   public:
    DeepTile(int width, int height, bool volume, string_view layer, string_view view)
        : width(width), height(height), volume(volume), layer(layer), view(view) {}
    virtual ~DeepTile() = default;
    const int width, height;
    const bool volume;
    const string layer, view;
    virtual std::vector<deep::IntervalSample> get_pixel(int x, int y) const = 0;
    /* Actual accepted camera population, including misses. Diagnostic reads
     * expose local surface alpha and volume optical depth, before reconstruction. */
    virtual int population(int x, int y) const = 0;
    virtual deep::VolumeCameraSample get_camera_sample(int x, int y, int sample) const = 0;
    virtual bool cancelled() const = 0;
  };

  virtual bool supports_deep_output() const { return false; }
  /* Called after all workers finish and completeness is verified. Throw to report
   * host delivery failure through Session::progress. No callback on cancellation. */
  virtual void write_deep_render_tile(const DeepTile & /* tile */)
  {
    throw std::runtime_error("Output driver does not support deep output");
  }
#endif

  /* Write tile once it has finished rendering. */
  virtual void write_render_tile(const Tile &tile) = 0;

  /* Update tile while rendering is in progress. Return true if any update
   * was performed. */
  virtual bool update_render_tile(const Tile & /* tile */)
  {
    return false;
  }

  /* For baking, read render pass PASS_BAKE_PRIMITIVE/SEED/DIFFERENTIAL
   * to determine which shading points to use for baking at each pixel. Return
   * true if any data was read. */
  virtual bool read_render_tile(const Tile & /* tile */)
  {
    return false;
  }
};

CCL_NAMESPACE_END
