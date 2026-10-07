/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
CCL_NAMESPACE_BEGIN

/* Shared CPU/CUDA check. Oracle evaluates the analytic quartic in power form;
 * it does not use the compressor's blossom, restriction or integral helpers. */
ccl_device double compression_exact_tau(const double *b, const double u)
{
  return b[0] * u + 1.5 * (b[1] - b[0]) * u * u +
         (b[0] - 2 * b[1] + b[2]) * u * u * u +
         (-b[0] + 3 * b[1] - 3 * b[2] + b[3]) * u * u * u * u / 4;
}

/* Mixed exact/linear records, with opaque tails represented explicitly. */
ccl_device double compression_record_tau(const KernelDeepEvent event,
                                          const KernelDeepDensity span, const double z)
{
  if (deep_event_type(event) == DEEP_SURFACE)
    return z >= event.front ? 1.7976931348623157e308 : 0;
  double u = (z - span.front) / (span.back - span.front);
  u = u < 0 ? 0 : (u > 1 ? 1 : u);
  if (deep_event_type(event) == DEEP_VOLUME)
    return u * (double(span.optical_depth[0]) + double(span.optical_depth[1]));
  const double b[4] = {span.optical_depth[0], span.optical_depth[1],
                       span.optical_depth[2], span.optical_depth[3]};
  return compression_exact_tau(b, u);
}

ccl_device int check_volume_compression(ccl_global KernelDeepEvent *events,
                                        ccl_global KernelDeepDensity *density)
{
  {
    const KernelDeepEvent sentinel{deep_event_pack(DEEP_SURFACE, 9), 7, 7, .5f, 0};
    events[0] = events[1] = sentinel;
    const KernelDeepDensity cell{{.02f,.02f,.02f,.02f},1,2};
    KernelDeepWriteState counted;
    counted.count_only = true;
    int count = 0;
    if (deep_volume_exact(cell, nullptr, density, 1, 8192, &count, 9, &counted) !=
        DEEP_ERROR_NONE || count != 1 || counted.written_bytes || counted.failed)
      return 13;
    KernelDeepWriteState bounded;
    bounded.limit = 1;
    count = 0;
    deep_volume_exact(cell, events, density, 1, 8192, &count, 9, &bounded);
    if (bounded.failed || bounded.written_bytes != sizeof(KernelDeepEvent) + sizeof(KernelDeepDensity))
      return 14;
    deep_volume_exact(cell, events, density, 1, 8192, &count, 9, &bounded);
    if (!bounded.failed || events[1].front != sentinel.front ||
        deep_event_type(events[1]) != DEEP_SURFACE)
      return 15; // Assigned range overflow cannot overwrite the next lane.
    bounded = {};
    bounded.limit = 2;
    bounded.compact_density = true;
    bounded.density_limit = 1;
    deep_write_event(events, density, 0, sentinel, nullptr, &bounded);
    deep_write_event(events, density, 1,
        {deep_event_pack(DEEP_VOLUME_CUBIC, 9),1,2,0,0}, &cell, &bounded);
    if (bounded.failed || bounded.events != 2 || bounded.companions != 1 ||
        density[0].front != cell.front || density[0].back != cell.back ||
        bounded.written_bytes != 2*sizeof(KernelDeepEvent) + sizeof(KernelDeepDensity))
      return 17;
    deep_write_event(events, density, 1,
        {deep_event_pack(DEEP_VOLUME_CUBIC, 9),3,4,0,0}, &cell, &bounded);
    if (!bounded.failed || bounded.companions != 1)
      return 18;
    const KernelDeepEvent packed{deep_event_pack(DEEP_VOLUME_CUBIC, int(DEEP_OBJECT_MASK)),0,0,0,0};
    if (deep_event_type(packed) != DEEP_VOLUME_CUBIC || deep_event_object(packed) != DEEP_OBJECT_MASK ||
        deep_event_pack(DEEP_SURFACE, int(DEEP_OBJECT_MASK) + 1) != DEEP_EVENT_KIND_MASK)
      return 16;
  }
  /* Constant, convex/concave ramps, and adversarial interior peaks/valleys.
   * Large camera depths exercise the double sidecar and FLOAT collapse. */
  const double cases[6][4] = {{.02, .02, .02, .02}, {0, 0, 0, 2},
                              {2, 0, 0, 0}, {0, 8, 8, 0},
                              {8, 0, 0, 8}, {0, 0, 0, 0}};
  {
    DeepVolumeCompression stream{};
    stream.object = 17;
    int count = 0;
    const KernelDeepDensity foreground{{20, 20, 20, 20}, 1, 2};
    const KernelDeepDensity background{{0, 8, 8, 0}, 4, 5};
    if (deep_volume_compression_cell(&stream, foreground, 1e-5, 1e-8,
                                     events, density, 1, 8192, &count) != DEEP_ERROR_NONE ||
        deep_volume_compression_cell(&stream, background, 1e-5, 1e-8,
                                     events, density, 1, 8192, &count) != DEEP_ERROR_NONE ||
        deep_volume_compression_flush(&stream, 1e-8, events, density,
                                      1, 8192, &count) != DEEP_ERROR_NONE || count != 2 ||
        !stream.terminated || deep_event_type(events[1]) != DEEP_SURFACE || stream.cutoff < 4 ||
        deep_event_object(events[0]) != 17 || deep_event_object(events[1]) != 17)
      return 9; /* Vacuum must preserve absorbed prefix, not force subdivisions. */
  }
  for (int mode = 0; mode < 2; ++mode) {
    const double eps = mode ? 1e-4 : 1e-3;
    {
      int count = 0;
      for (int object = 0; object < 2; ++object) {
        DeepVolumeCompression stream{};
        stream.object = object + 7;
        const int first = count;
        for (int cell = 0; cell < 12; ++cell) {
          const double front = 1 + object * 2.5 + cell + (cell >= 6 ? 2 : 0);
          const double *b = cases[(cell + object) % 6];
          const KernelDeepDensity input = {{float(b[0]), float(b[1]), float(b[2]), float(b[3])},
                                            front, front + 1};
          if (deep_volume_compression_cell(&stream, input, eps * .375,
                                           eps * .25 / 8192,
                                           events, density, 1, 8192, &count) != DEEP_ERROR_NONE)
            return 6;
          if (stream.terminated)
            break;
        }
        if (deep_volume_compression_flush(&stream, eps * .25 / 8192,
                                          events, density, 1, 8192, &count) != DEEP_ERROR_NONE)
          return 7;
        for (int i = first; i < count; ++i)
          if (deep_event_object(events[i]) != object + 7)
            return 15; // Exact, compressed and opaque records retain their object.
      }
      if (count > 24)
        return 10; /* No ray may expand relative to its exact cell stream. */
      for (int probe = 0; probe <= 8192; ++probe) {
        const double z = 20 * double(probe) / 8192;
        double exact = 0, approximate = 0;
        for (int object = 0; object < 2; ++object)
          for (int cell = 0; cell < 12; ++cell) {
            const double front = 1 + object * 2.5 + cell + (cell >= 6 ? 2 : 0);
            double u = z - front;
            u = u < 0 ? 0 : (u > 1 ? 1 : u);
            exact += compression_exact_tau(cases[(cell + object) % 6], u);
          }
        for (int i = 0; i < count; ++i) {
          approximate += compression_record_tau(events[i], density[i], z);
        }
        if (fabs(exp(-exact) - exp(-approximate)) > eps)
          return 8;
      }
    }
    for (int test = 0; test < 6; ++test) {
      for (int translated = 0; translated < 2; ++translated) {
        const double start = translated ? 1e8 : 1;
        DeepVolumeCompression stream{};
        int count = 0;
        const KernelDeepDensity cell = {{float(cases[test][0]), float(cases[test][1]),
                                         float(cases[test][2]), float(cases[test][3])},
                                        start, start + 1};
        auto error = deep_volume_compression_cell(&stream, cell, eps * .75,
                                                  eps * .25 / 8192,
                                                  events, density, 1, 8192, &count);
        if (error != DEEP_ERROR_NONE)
          return 1;
        error = deep_volume_compression_flush(&stream, eps * .25 / 8192,
                                              events, density, 1, 8192, &count);
        if (error != DEEP_ERROR_NONE || count == 0 || count > 1)
          return 2;
        if (density[0].front != cell.front || density[0].back != cell.back)
          return 13;
        for (int i = 0; i < 4; ++i)
          if (density[0].optical_depth[i] != cell.optical_depth[i])
            return 14; /* Exact fallback is a byte-preserving cell copy. */
        for (int probe = 0; probe <= 8192; ++probe) {
          const double u = double(probe) / 8192;
          const double z = start + u;
          double tau = 0;
          for (int i = 0; i < count; ++i) {
            if (deep_event_type(events[i]) != DEEP_VOLUME_CUBIC || !(density[i].back > density[i].front))
              return 3; /* A singleton retains its exact cubic bytes. */
            tau += compression_record_tau(events[i], density[i], z);
          }
          if (fabs(exp(-tau) - exp(-compression_exact_tau(cases[test], u))) > eps)
            return 4;
        }
        /* A full buffer must fail, including the final flush. */
        stream = {};
        count = 1;
        error = deep_volume_compression_cell(&stream, cell, eps * .75,
                                              eps * .25, events, density, 1, 1, &count);
        if (error == DEEP_ERROR_NONE)
          error = deep_volume_compression_flush(&stream, eps * .25,
                                                events, density, 1, 1, &count);
        if (error != DEEP_ERROR_EVENT_CAPACITY)
          return 5;
      }
    }
  }
  {
    DeepVolumeCompression stream{};
    stream.object = 17;
    int count = 0;
    for (int i = 0; i < 10; ++i) {
      const KernelDeepDensity cell{{.02f,.02f,.02f,.02f}, double(i+1), double(i+2)};
      if (deep_volume_compression_cell(&stream, cell, 1e-5, 1e-8,
                                       events, density, 1, 8192, &count) != DEEP_ERROR_NONE)
        return 11;
    }
    if (deep_volume_compression_flush(&stream, 1e-8, events, density, 1, 8192, &count) !=
        DEEP_ERROR_NONE || count != 1 || deep_event_type(events[0]) != DEEP_VOLUME || deep_event_object(events[0]) != 17)
      return 12;
  }
  return 0;
}
CCL_NAMESPACE_END
