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

ccl_device int check_volume_compression(ccl_global KernelDeepEvent *events,
                                        ccl_global KernelDeepDensity *density)
{
  /* Constant, convex/concave ramps, and adversarial interior peaks/valleys.
   * Large camera depths exercise the double sidecar and FLOAT collapse. */
  const double cases[6][4] = {{.02, .02, .02, .02}, {0, 0, 0, 2},
                              {2, 0, 0, 0}, {0, 8, 8, 0},
                              {8, 0, 0, 8}, {0, 0, 0, 0}};
  for (int mode = 0; mode < 2; ++mode) {
    const double eps = mode ? 1e-4 : 1e-3;
    {
      int count = 0;
      for (int object = 0; object < 2; ++object) {
        DeepVolumeCompression stream{};
        for (int cell = 0; cell < 12; ++cell) {
          const double front = 1 + object * 2.5 + cell + (cell >= 6 ? 2 : 0);
          const double *b = cases[(cell + object) % 6];
          const KernelDeepDensity input = {{float(b[0]), float(b[1]), float(b[2]), float(b[3])},
                                            front, front + 1};
          if (deep_volume_compression_cell(&stream, input, eps * .375,
                                           eps * .25 / 8192,
                                           events, density, 1, 8192, &count) != DEEP_ERROR_NONE)
            return 6;
        }
        if (deep_volume_compression_flush(&stream, eps * .25 / 8192,
                                          events, density, 1, 8192, &count) != DEEP_ERROR_NONE)
          return 7;
      }
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
          double u = (z - density[i].front) / (density[i].back - density[i].front);
          u = u < 0 ? 0 : (u > 1 ? 1 : u);
          approximate += u * (double(density[i].optical_depth[0]) +
                              double(density[i].optical_depth[1]));
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
        if (error != DEEP_ERROR_NONE || count == 0 || count > 8192)
          return 2;
        for (int probe = 0; probe <= 8192; ++probe) {
          const double u = double(probe) / 8192;
          const double z = start + u;
          double tau = 0;
          for (int i = 0; i < count; ++i) {
            if (events[i].kind != DEEP_VOLUME || !(density[i].back > density[i].front))
              return 3;
            double fraction = (z - density[i].front) / (density[i].back - density[i].front);
            fraction = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
            tau += (double(density[i].optical_depth[0]) +
                    double(density[i].optical_depth[1])) * fraction;
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
  return 0;
}
CCL_NAMESPACE_END
