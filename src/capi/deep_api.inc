/* SPDX-License-Identifier: Apache-2.0 */
#ifdef WITH_CYCLES_DEEP_OPAQUE
struct ccl_deep_tile {
  const ccl::OutputDriver::DeepTile &native;
  int row = -1, cached_x = -1, cached_y = -1;
  std::vector<ccl::deep::IntervalSample> pixel;
  explicit ccl_deep_tile(const ccl::OutputDriver::DeepTile &t) : native(t) {}
  void finish_row()
  {
    const int previous = row;
    row = -1;
    if (previous >= 0)
      native.end_row(previous);
  }
  ~ccl_deep_tile()
  {
    try {
      finish_row();
    }
    catch (...) { /* Preserve the original callback/reconstruction exception. */
    }
  }
};
namespace {
thread_local ccl_deep_tile *active_deep_tile = nullptr;
void deliver_deep(ccl_session &s, const ccl::OutputDriver::DeepTile &native)
{
  if (!s.deep_fn)
    throw std::runtime_error("C API: deep callback is missing");
  auto objects = native.object_manifest(), holdouts = native.holdout_manifest();
  std::vector<ccl_deep_object> public_objects, public_holdouts;
  for (const auto &entry : objects)
    public_objects.push_back({entry.first, entry.second.c_str()});
  for (const auto &entry : holdouts)
    public_holdouts.push_back({entry.first, entry.second.c_str()});
  ccl_deep_info info{sizeof(info),
                     native.width,
                     native.height,
                     int(native.volume),
                     int(native.ids()),
                     native.sample_limit(),
                     native.layer.c_str(),
                     native.view.c_str(),
                     native.error(),
                     native.z_tolerance(),
                     native.volume_shader_error(),
                     native.volume_step_min(),
                     native.volume_step_max(),
                     int(native.volume_shader_adaptive()),
                     native.volume_row_sample_limit(),
                     public_objects.data(),
                     public_holdouts.data(),
                     public_objects.size(),
                     public_holdouts.size()};
  ccl_deep_tile tile(native);
  struct Active {
    explicit Active(ccl_deep_tile *tile)
    {
      active_deep_tile = tile;
    }
    ~Active()
    {
      active_deep_tile = nullptr;
    }
  } active(&tile);
  {
    CallbackScope guard;
    int32_t status;
    try { status = s.deep_fn(&tile, &info, s.deep_user); }
    catch (...) { throw std::runtime_error("C API: exception in deep output callback"); }
    if (status)
      throw std::runtime_error("C API: deep output callback failed");
  }
  tile.finish_row();
}
}  // namespace
#endif

ccl_status CCL_CALL ccl_session_set_deep_output(ccl_session *s,
                                                const ccl_deep_params *p,
                                                ccl_deep_fn fn,
                                                void *user)
{
  return session_access(s, [&]() -> ccl_status {
#ifdef WITH_CYCLES_DEEP_OPAQUE
    if (p && (p->size < sizeof(*p) || !fn || p->samples < 0 ||
              p->samples > ccl::Integrator::MAX_SAMPLES || p->max_events <= 0 ||
              !p->memory_bytes || p->memory_bytes > SIZE_MAX || !std::isfinite(p->error) ||
              p->error < 0 || !std::isfinite(p->z_tolerance) || p->z_tolerance < 0 ||
              !std::isfinite(p->volume_step) || p->volume_step < 0))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid deep output settings");
    if (p)
      for (auto b : {p->transparent, p->volume, p->volume_shader_eval, p->ids})
        if (b != 0 && b != 1)
          return fail(CCL_ERROR_INVALID_ARGUMENT, "Deep bool must be 0 or 1");
    if (auto status = scene_access(&s->scene, true))
      return status;
    ccl::DeepSettings settings;
    if (p) {
      settings.enabled = true;
      settings.transparent = bool(p->transparent);
      settings.volume = bool(p->volume);
      settings.volume_shader_eval = bool(p->volume_shader_eval);
      settings.ids = bool(p->ids);
      settings.samples = p->samples;
      settings.max_events = p->max_events;
      settings.memory_bytes = size_t(p->memory_bytes);
      settings.error = p->error;
      settings.z_tolerance = p->z_tolerance;
      settings.volume_step = p->volume_step;
    }
    s->params.deep = settings;
    s->deep_fn = p ? fn : nullptr;
    s->deep_user = p ? user : nullptr;
    return CCL_SUCCESS;
#else
    return fail(CCL_ERROR_UNSUPPORTED, "This build has no deep visibility extension");
#endif
  });
}

ccl_status CCL_CALL ccl_deep_tile_get_pixel(ccl_deep_tile *tile,
                                            int32_t x,
                                            int32_t y,
                                            ccl_deep_sample *samples,
                                            uint64_t capacity,
                                            uint64_t *count,
                                            uint32_t *accepted)
{
  /* This is the sole callback-safe reader; no session mutex or host callback runs here. */
  error_message[0] = 0;
  try {
#ifdef WITH_CYCLES_DEEP_OPAQUE
    if (!tile || tile != active_deep_tile || !in_callback || !count || !accepted ||
        (!samples && capacity) || x < 0 || y < 0 || x >= tile->native.width ||
        y >= tile->native.height)
      return fail(CCL_ERROR_INVALID_ARGUMENT,
                  "Invalid deep tile, callback thread or pixel arguments");
    if (tile->native.cancelled())
      return fail(CCL_ERROR_INTERNAL, "Deep output cancelled");
    const int native_y = tile->native.height - 1 - y;
    if (tile->cached_x != x || tile->cached_y != y) {
      tile->cached_x = tile->cached_y = -1;
      if (tile->row != native_y) {
        tile->finish_row();
        tile->row = native_y;
        tile->native.begin_row(native_y);
      }
      tile->pixel = tile->native.get_pixel(x, native_y);
      tile->cached_x = x;
      tile->cached_y = y;
    }
    const int population = tile->native.population(x, native_y);
    if (population < 0)
      return fail(CCL_ERROR_INTERNAL, "Invalid native camera population");
    *count = tile->pixel.size();
    *accepted = uint32_t(population);
    if (!samples && !capacity)
      return CCL_SUCCESS;
    if (capacity < *count)
      return fail(CCL_ERROR_BUFFER_TOO_SMALL, "Deep pixel buffer is too small");
    for (size_t i = 0; i < tile->pixel.size(); ++i) {
      const auto &p = tile->pixel[i];
      samples[i] = {p.front, p.back, p.alpha, p.object, p.facing};
    }
    return CCL_SUCCESS;
#else
    return fail(CCL_ERROR_UNSUPPORTED, "This build has no deep visibility extension");
#endif
  }
  catch (const std::bad_alloc &) {
    return fail(CCL_ERROR_OUT_OF_MEMORY, "Deep pixel allocation failed");
  }
  catch (const std::exception &e) {
    return fail(CCL_ERROR_INTERNAL, e.what());
  }
  catch (...) {
    return fail(CCL_ERROR_INTERNAL, "Deep pixel reconstruction failed");
  }
}
