/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

/* Templated common implementation part of all CPU kernels.
 *
 * The idea is that particular `.cpp` files sets needed optimization flags and
 * simply includes this file without worry of copying actual implementation over.
 */

#pragma once

// clang-format off
#include "kernel/device/cpu/compat.h"

#ifndef KERNEL_STUB
#    include "kernel/globals.h"

#    include "kernel/device/cpu/image.h"

#    include "kernel/integrator/state.h"
#    include "kernel/integrator/state_flow.h"
#    include "kernel/integrator/state_util.h"

#    include "kernel/integrator/init_from_camera.h"
#    include "kernel/integrator/init_from_bake.h"
#    include "kernel/integrator/megakernel.h"

#    include "kernel/film/adaptive_sampling.h"
#    include "kernel/film/cryptomatte_passes.h"
#    include "kernel/film/read.h"
#    include "kernel/film/volume_guiding_denoise.h"

#    include "kernel/bake/bake.h"

#else
#  define STUB_ASSERT(arch, name) \
    assert(!(#name " kernel stub for architecture " #arch " was called!"))
#endif   /* KERNEL_STUB */
// clang-format on

CCL_NAMESPACE_BEGIN

/* --------------------------------------------------------------------
 * Integrator.
 */

#ifdef KERNEL_STUB
#  define KERNEL_INVOKE(name, ...) (STUB_ASSERT(KERNEL_ARCH, name), 0)
#else
#  define KERNEL_INVOKE(name, ...) integrator_##name(__VA_ARGS__)
#endif

/* TODO: Either use something like get_work_pixel(), or simplify tile which is passed here, so
 * that it does not contain unused fields. */
#define DEFINE_INTEGRATOR_INIT_KERNEL(name) \
  bool KERNEL_FUNCTION_FULL_NAME(integrator_##name)(const ThreadKernelGlobalsCPU *kg, \
                                                    IntegratorStateCPU *state, \
                                                    KernelWorkTile *tile, \
                                                    ccl_global float *render_buffer) \
  { \
    (void)kg; \
    (void)state; \
    (void)tile; \
    (void)render_buffer; \
    return KERNEL_INVOKE( \
        name, kg, state, tile, render_buffer, tile->x, tile->y, tile->start_sample); \
  }

#define DEFINE_INTEGRATOR_SHADE_KERNEL(name) \
  void KERNEL_FUNCTION_FULL_NAME(integrator_##name)(const ThreadKernelGlobalsCPU *kg, \
                                                    IntegratorStateCPU *state, \
                                                    ccl_global float *render_buffer) \
  { \
    (void)kg; \
    (void)state; \
    (void)render_buffer; \
    KERNEL_INVOKE(name, kg, state, render_buffer); \
  }

DEFINE_INTEGRATOR_INIT_KERNEL(init_from_camera)
DEFINE_INTEGRATOR_INIT_KERNEL(init_from_bake)
#ifdef WITH_CYCLES_DEEP_OPAQUE
DEFINE_INTEGRATOR_SHADE_KERNEL(intersect_closest)

/* Independent straight visibility traversal. The accepted camera ray, differentials,
 * time and
 * RNG identity are copied; beauty path state and buffers are never mutated.
 * Scene validation
 * excludes shader nodes with side effects (including AOV writes). */
int KERNEL_FUNCTION_FULL_NAME(deep_surface)(const ThreadKernelGlobalsCPU *kg,
                                            const IntegratorStateCPU *camera,
                                            float *events,
                                            const int max_events)
{
#  ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, deep_surface);
  return -1;
#  else
  IntegratorStateCPU traversal = *camera;
  IntegratorState state = &traversal;
  Ray ray;
  integrator_state_read_ray(state, &ray);
  ray.self.object = OBJECT_NONE;
  ray.self.prim = PRIM_NONE;
  ray.self.light_object = OBJECT_NONE;
  ray.self.light_prim = PRIM_NONE;
  int count = 0;
  for (;;) {
    Intersection isect;
    isect.object = OBJECT_NONE;
    isect.prim = PRIM_NONE;
    if (!scene_intersect(kg, &ray, PATH_RAY_VISIBILITY_CAMERA, &isect))
      return count;
    /* Test for a hit beyond the limit, so a complete chain exactly at capacity succeeds. */
    if (count == max_events || isect.type != PRIMITIVE_TRIANGLE)
      return -1;
    integrator_state_write_isect(state, &isect);
    integrator_state_write_ray(state, &ray);
    ShaderData sd;
    shader_setup_from_ray(kg, &sd, &ray, &isect);
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE & ~KERNEL_FEATURE_NODE_RAYTRACE>(
        kg, state, &sd, nullptr, state->path.visibility, state->path.flag);
    if (sd.runtime_flag & SR_CACHE_MISS)
      return -1;
    const float3 transparency = spectrum_to_rgb(surface_shader_transparency(&sd));
    /* Scalar extinction only: never luminance, throughput or a sampled closure weight. */
    if (!isfinite(transparency.x) || !isfinite(transparency.y) || !isfinite(transparency.z) ||
        transparency.x < 0 || transparency.x > 1 || transparency.y != transparency.x ||
        transparency.z != transparency.x)
      return -1;
    const float depth = transform_point(&kg->data.cam.worldtocamera, ray.P + ray.D * isect.t).z;
    if (!isfinite(depth) || depth <= 0)
      return -1;
    events[2 * count] = depth;
    events[2 * count + 1] = 1.0f - transparency.x;
    ++count;
    if (transparency.x == 0)
      return count;
    const float next = intersection_t_offset(isect.t);
    if (!(next > ray.tmin))
      return -1;
    ray.tmin = next;
    ray.self.object = isect.object;
    ray.self.prim = isect.prim;
    /* Preserve the native transparent path's shader context, without beauty RR/termination. */
    state->path.transparent_bounce++;
    state->path.flag |= PATH_RAY_TRANSPARENT;
    state->path.rng_offset += PRNG_BOUNCE_NUM;
  }
#  endif
}
#endif
DEFINE_INTEGRATOR_SHADE_KERNEL(megakernel)

/* --------------------------------------------------------------------
 * Shader evaluation.
 */

void KERNEL_FUNCTION_FULL_NAME(shader_eval_displace)(const ThreadKernelGlobalsCPU *kg,
                                                     const KernelShaderEvalInput *input,
                                                     float *output,
                                                     const int offset)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, shader_eval_displace);
  (void)kg;
  (void)input;
  (void)output;
  (void)offset;
#else
  uint cache_miss_unused = false;
  kernel_displace_evaluate(kg, input, output, &cache_miss_unused, offset);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(shader_eval_background)(const ThreadKernelGlobalsCPU *kg,
                                                       const KernelShaderEvalInput *input,
                                                       float *output,
                                                       const int offset)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, shader_eval_background);
  (void)kg;
  (void)input;
  (void)output;
  (void)offset;
#else
  uint cache_miss_unused = false;
  kernel_background_evaluate(kg, input, output, &cache_miss_unused, offset);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(shader_eval_curve_shadow_transparency)(
    const ThreadKernelGlobalsCPU *kg,
    const KernelShaderEvalInput *input,
    float *output,
    const int offset)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, shader_eval_curve_shadow_transparency);
  (void)kg;
  (void)input;
  (void)output;
  (void)offset;
#else
  uint cache_miss_unused = false;
  kernel_curve_shadow_transparency_evaluate(kg, input, output, &cache_miss_unused, offset);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(shader_eval_volume_density)(const ThreadKernelGlobalsCPU *kg,
                                                           const KernelShaderEvalInput *input,
                                                           float *output,
                                                           const int offset)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, shader_eval_volume_density);
  (void)kg;
  (void)input;
  (void)output;
  (void)offset;
#else
  uint cache_miss_unused = false;
  kernel_volume_density_evaluate(kg, input, output, &cache_miss_unused, offset);
#endif
}

/* --------------------------------------------------------------------
 * Adaptive sampling.
 */

bool KERNEL_FUNCTION_FULL_NAME(adaptive_sampling_convergence_check)(
    const ThreadKernelGlobalsCPU *kg,
    ccl_global float *render_buffer,
    const int x,
    const int y,
    const float threshold,
    const int reset,
    const int offset,
    const int stride)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, adaptive_sampling_convergence_check);
  (void)kg;
  (void)render_buffer;
  (void)x;
  (void)y;
  (void)threshold;
  (void)reset;
  (void)offset;
  (void)stride;
  return false;
#else
  return film_adaptive_sampling_convergence_check(
      kg, render_buffer, x, y, threshold, reset, offset, stride);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(adaptive_sampling_filter_x)(const ThreadKernelGlobalsCPU *kg,
                                                           ccl_global float *render_buffer,
                                                           const int y,
                                                           const int start_x,
                                                           const int width,
                                                           const int offset,
                                                           const int stride)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, adaptive_sampling_filter_x);
  (void)kg;
  (void)render_buffer;
  (void)y;
  (void)start_x;
  (void)width;
  (void)offset;
  (void)stride;
#else
  film_adaptive_sampling_filter_x(kg, render_buffer, y, start_x, width, offset, stride);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(adaptive_sampling_filter_y)(const ThreadKernelGlobalsCPU *kg,
                                                           ccl_global float *render_buffer,
                                                           const int x,
                                                           const int start_y,
                                                           const int height,
                                                           const int offset,
                                                           const int stride)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, adaptive_sampling_filter_y);
  (void)kg;
  (void)render_buffer;
  (void)x;
  (void)start_y;
  (void)height;
  (void)offset;
  (void)stride;
#else
  film_adaptive_sampling_filter_y(kg, render_buffer, x, start_y, height, offset, stride);
#endif
}

/* --------------------------------------------------------------------
 * Cryptomatte.
 */

void KERNEL_FUNCTION_FULL_NAME(cryptomatte_postprocess)(const ThreadKernelGlobalsCPU *kg,
                                                        ccl_global float *render_buffer,
                                                        const int pixel_index)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, cryptomatte_postprocess);
  (void)kg;
  (void)render_buffer;
  (void)pixel_index;
#else
  film_cryptomatte_post(kg, render_buffer, pixel_index);
#endif
}

/* --------------------------------------------------------------------
 * Volume Scattering Probability Guiding.
 */

void KERNEL_FUNCTION_FULL_NAME(volume_guiding_filter_x)(const ThreadKernelGlobalsCPU *kg,
                                                        ccl_global float *render_buffer,
                                                        const int y,
                                                        const int center_x,
                                                        const int min_x,
                                                        const int max_x,
                                                        const int offset,
                                                        const int stride)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, volume_guiding_filter_x);
  (void)kg;
  (void)render_buffer;
  (void)y;
  (void)center_x;
  (void)min_x;
  (void)max_x;
  (void)offset;
  (void)stride;
#else
  volume_guiding_filter_x(kg, render_buffer, y, center_x, min_x, max_x, offset, stride);
#endif
}

void KERNEL_FUNCTION_FULL_NAME(volume_guiding_filter_y)(const ThreadKernelGlobalsCPU *kg,
                                                        ccl_global float *render_buffer,
                                                        const int x,
                                                        const int min_y,
                                                        const int max_y,
                                                        const int offset,
                                                        const int stride)
{
#ifdef KERNEL_STUB
  STUB_ASSERT(KERNEL_ARCH, volume_guiding_filter_y);
  (void)kg;
  (void)render_buffer;
  (void)x;
  (void)min_y;
  (void)max_y;
  (void)offset;
  (void)stride;
#else
  volume_guiding_filter_y(kg, render_buffer, x, min_y, max_y, offset, stride);
#endif
}

/* --------------------------------------------------------------------
 * Film Convert.
 */

#ifdef KERNEL_STUB

#  define KERNEL_FILM_CONVERT_FUNCTION(name, is_float) \
    void KERNEL_FUNCTION_FULL_NAME(film_convert_##name)(const KernelFilmConvert *kfilm_convert, \
                                                        const float *buffer, \
                                                        float *pixel, \
                                                        const int width, \
                                                        const int buffer_stride, \
                                                        const int pixel_stride) \
    { \
      STUB_ASSERT(KERNEL_ARCH, film_convert_##name); \
      (void)kfilm_convert; \
      (void)buffer; \
      (void)pixel; \
      (void)width; \
      (void)buffer_stride; \
      (void)pixel_stride; \
    } \
    void KERNEL_FUNCTION_FULL_NAME(film_convert_half_rgba_##name)( \
        const KernelFilmConvert *kfilm_convert, \
        const float *buffer, \
        half4 *pixel, \
        const int width, \
        const int buffer_stride) \
    { \
      STUB_ASSERT(KERNEL_ARCH, film_convert_##name); \
      (void)kfilm_convert; \
      (void)buffer; \
      (void)pixel; \
      (void)width; \
      (void)buffer_stride; \
    }

#else

#  define KERNEL_FILM_CONVERT_FUNCTION(name, is_float) \
    void KERNEL_FUNCTION_FULL_NAME(film_convert_##name)(const KernelFilmConvert *kfilm_convert, \
                                                        const float *buffer, \
                                                        float *pixel, \
                                                        const int width, \
                                                        const int buffer_stride, \
                                                        const int pixel_stride) \
    { \
      for (int i = 0; i < width; i++, buffer += buffer_stride, pixel += pixel_stride) { \
        film_get_pass_pixel_##name(kfilm_convert, buffer, pixel); \
      } \
    } \
    void KERNEL_FUNCTION_FULL_NAME(film_convert_half_rgba_##name)( \
        const KernelFilmConvert *kfilm_convert, \
        const float *buffer, \
        half4 *pixel, \
        const int width, \
        const int buffer_stride) \
    { \
      for (int i = 0; i < width; i++, buffer += buffer_stride, pixel++) { \
        float pixel_rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f}; \
        film_get_pass_pixel_##name(kfilm_convert, buffer, pixel_rgba); \
        if (is_float) { \
          pixel_rgba[1] = pixel_rgba[0]; \
          pixel_rgba[2] = pixel_rgba[0]; \
        } \
        film_apply_pass_pixel_overlays_rgba(kfilm_convert, buffer, pixel_rgba); \
        *pixel = float4_to_half4_display( \
            make_float4(pixel_rgba[0], pixel_rgba[1], pixel_rgba[2], pixel_rgba[3])); \
      } \
    }

#endif

KERNEL_FILM_CONVERT_FUNCTION(depth, true)
KERNEL_FILM_CONVERT_FUNCTION(mist, true)
KERNEL_FILM_CONVERT_FUNCTION(sample_count, true)
KERNEL_FILM_CONVERT_FUNCTION(volume_majorant, true)
KERNEL_FILM_CONVERT_FUNCTION(float, true)

KERNEL_FILM_CONVERT_FUNCTION(light_path, false)
KERNEL_FILM_CONVERT_FUNCTION(rgbe, false)
KERNEL_FILM_CONVERT_FUNCTION(float3, false)

KERNEL_FILM_CONVERT_FUNCTION(motion, false)
KERNEL_FILM_CONVERT_FUNCTION(cryptomatte, false)
KERNEL_FILM_CONVERT_FUNCTION(shadow_catcher, false)
KERNEL_FILM_CONVERT_FUNCTION(shadow_catcher_matte_with_shadow, false)
KERNEL_FILM_CONVERT_FUNCTION(combined, false)
KERNEL_FILM_CONVERT_FUNCTION(float4, false)

#undef KERNEL_FILM_CONVERT_FUNCTION

#undef KERNEL_INVOKE
#undef DEFINE_INTEGRATOR_SHADE_KERNEL
#undef DEFINE_INTEGRATOR_INIT_KERNEL

#undef KERNEL_STUB
#undef STUB_ASSERT
#undef KERNEL_ARCH

CCL_NAMESPACE_END
