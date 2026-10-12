/* SPDX-License-Identifier: Apache-2.0 */
/* Native Session owns scheduling; this boundary owns host lifetimes only. */
#include "session/output_driver.h"
namespace {
ccl_status scene_access(ccl_scene *scene, bool write)
{
  auto &s = *scene->owner;
  if (s.waiters)
    return fail(CCL_ERROR_IN_USE, "A host thread is waiting on this session");
  if (s.active) {
    if (!write || !s.paused)
      return fail(CCL_ERROR_IN_USE, "Pause before editing; scene reads require an idle session");
    s.native->cancel();
    s.active = false;
  }
  if (write)
    s.needs_reset = true;
  return CCL_SUCCESS;
}
struct CallbackScope {
  CallbackScope()
  {
    in_callback = true;
  }
  ~CallbackScope()
  {
    in_callback = false;
  }
};
template<size_t N> void message(char (&dst)[N], const std::string &src)
{
  size_t length = std::min(src.size(), N - 1);
  // Do not truncate inside a UTF-8 code point.
  if (length < src.size())
    while (length && (static_cast<unsigned char>(src[length]) & 0xc0) == 0x80)
      --length;
  std::memcpy(dst, src.data(), length);
  dst[length] = 0;
}
ccl_progress progress_snapshot(ccl_session &s)
{
  ccl_progress out{};
  out.size = sizeof(out);
  auto &p = s.native->progress;
  out.fraction = p.get_progress();
  p.get_time(out.total_seconds, out.render_seconds);
  out.sample = p.get_current_sample();
  out.error = p.get_error();
  out.cancelled = p.get_cancel();
  out.pause_requested = s.paused;
  out.reset_required = s.needs_reset;
  std::string status, substatus;
  p.get_status(status, substatus);
  message(out.status, status);
  message(out.substatus, substatus);
  message(out.error_message, p.get_error_message());
  return out;
}
class HostOutput final : public ccl::OutputDriver {
  ccl_session &s;
  bool deliver(const Tile &tile, bool final) noexcept
  {
    try {
      if (!s.output_fn)
        return false;
      bool delivered = false;
      for (const auto &pass : s.output_passes) {
        const uint64_t count = uint64_t(tile.size.x) * tile.size.y * pass.channels;
        std::vector<float> pixels(count);
        if (!tile.get_pass_pixels(pass.name, pass.channels, pixels.data())) {
          if (final)
            s.native->progress.set_error("C API: final output pass is unavailable: " + pass.name);
          continue;
        }
        ccl_render_tile out{sizeof(out),
                            tile.offset.x,
                            tile.offset.y,
                            tile.size.x,
                            tile.size.y,
                            tile.full_size.x,
                            tile.full_size.y,
                            pass.channels,
                            int(final),
                            pass.name.c_str(),
                            pixels.data(),
                            count};
        CallbackScope guard;
        if (s.output_fn(&out, s.output_user)) {
          s.native->progress.set_error("C API: host output callback failed");
          return false;
        }
        delivered = true;
      }
      return delivered;
    }
    catch (...) {
      s.native->progress.set_error("C API: exception in output delivery");
      return false;
    }
  }

 public:
  explicit HostOutput(ccl_session &owner) : s(owner) {}
  void write_render_tile(const Tile &tile) override
  {
    deliver(tile, true);
  }
  bool update_render_tile(const Tile &tile) override
  {
    return deliver(tile, false);
  }
};
void install_session_bridges(ccl_session &s)
{
  s.native->set_output_driver(ccl::make_unique<HostOutput>(s));
  s.native->progress.set_update_callback([&s] {
    try {
      if (s.progress_fn) {
        auto progress = progress_snapshot(s);
        CallbackScope guard;
        s.progress_fn(&progress, s.progress_user);
      }
    }
    catch (...) {
      s.native->progress.set_error("C API: exception in progress callback");
    }
  });
}
template<typename F> ccl_status session_access(ccl_session *s, F fn)
{
  return boundary([&] {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!sessions.count(s))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session handle");
    return fn();
  });
}
ccl_status idle(ccl_session &s)
{
  return s.active || s.waiters ? fail(CCL_ERROR_IN_USE, "Session must be idle") : CCL_SUCCESS;
}
ccl_status render_error(ccl_session &s)
{
  return s.native->progress.get_error() ?
             fail(CCL_ERROR_INTERNAL, s.native->progress.get_error_message().c_str()) :
             CCL_SUCCESS;
}
}  // namespace
ccl_status CCL_CALL ccl_session_reset(ccl_session *s, const ccl_buffer_params *p)
{
  return session_access(s, [&]() -> ccl_status {
    if (!p || p->size < sizeof(*p) || p->width <= 0 || p->height <= 0 || p->full_x < 0 ||
        p->full_y < 0 || p->full_width < p->width || p->full_height < p->height ||
        p->full_x > p->full_width - p->width || p->full_y > p->full_height - p->height ||
        p->samples <= 0 || p->samples > ccl::Integrator::MAX_SAMPLES ||
        !std::isfinite(p->time_limit) || p->time_limit < 0 || p->tile_size < 0 ||
        (p->use_resolution_divider != 0 && p->use_resolution_divider != 1) ||
        (p->temp_directory && !std::filesystem::u8path(p->temp_directory).is_absolute()))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid buffer settings");
    if (auto st = scene_access(&s->scene, true))
      return st;
    for (const auto &request : s->output_passes) {
      auto *pass = ccl::Pass::find(s->scene.native->passes, request.name);
      if (!pass)
        return fail(CCL_ERROR_NOT_FOUND, "Output pass not found");
      if (pass->get_info().num_components != request.channels)
        return fail(CCL_ERROR_TYPE_MISMATCH, "Output pass channel count mismatch");
    }
    auto params = s->params;
    params.samples = p->samples;
    params.time_limit = p->time_limit;
    params.use_auto_tile = p->tile_size != 0;
    params.tile_size = p->tile_size ? p->tile_size : 2048;
    params.use_resolution_divider = bool(p->use_resolution_divider);
    params.temp_dir = p->temp_directory ? p->temp_directory : user_path;
    ccl::BufferParams buffers;
    buffers.width = p->width;
    buffers.height = p->height;
    buffers.full_x = p->full_x;
    buffers.full_y = p->full_y;
    buffers.full_width = p->full_width;
    buffers.full_height = p->full_height;
    s->native->progress.reset();
    s->native->reset(params, buffers);
    s->native->set_pause(s->paused);
    s->params = params;
    s->buffers = buffers;
    s->configured = true;
    s->needs_reset = false;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_start(ccl_session *s)
{
  return session_access(s, [&]() -> ccl_status {
    if (auto st = idle(*s))
      return st;
    if (!s->configured || s->needs_reset)
      return fail(CCL_ERROR_IN_USE, "Reset required before starting");
    s->native->start();
    s->active = true;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_pause(ccl_session *s, int32_t pause)
{
  return session_access(s, [&]() -> ccl_status {
    if (pause != 0 && pause != 1)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Pause must be 0 or 1");
    if (s->params.background)
      return fail(CCL_ERROR_UNSUPPORTED, "Background sessions cannot pause");
    if (!pause && s->needs_reset)
      return fail(CCL_ERROR_IN_USE, "Reset required after editing or cancellation");
    s->paused = bool(pause);
    s->native->set_pause(bool(pause));
    if (!pause && !s->active && s->configured) {
      if (s->waiters)
        return fail(CCL_ERROR_IN_USE, "A host thread is waiting on this session");
      s->native->start();
      s->active = true;
    }
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_wait(ccl_session *s)
{
  return boundary([&]() -> ccl_status {
    std::unique_lock<std::mutex> lock(api_mutex);
    if (auto st = editing())
      return st;
    if (!sessions.count(s))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session handle");
    ++s->waiters;
    lock.unlock();
    s->native->wait();
    lock.lock();
    --s->waiters;
    s->active = false;
    return render_error(*s);
  });
}
ccl_status CCL_CALL ccl_session_cancel(ccl_session *s)
{
  return session_access(s, [&]() -> ccl_status {
    s->native->cancel();
    s->active = false;
    s->needs_reset = true;
    return render_error(*s);
  });
}
ccl_status CCL_CALL ccl_session_set_samples(ccl_session *s, int32_t samples)
{
  return session_access(s, [&]() -> ccl_status {
    if (samples <= 0 || samples > ccl::Integrator::MAX_SAMPLES)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid sample limit");
    s->params.samples = samples;
    s->native->set_samples(samples);
    // Before start, reset still holds a copy of SessionParams.
    if (s->configured && !s->active)
      s->native->reset(s->params, s->buffers);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_progress(ccl_session *s, ccl_progress *out)
{
  return session_access(s, [&]() -> ccl_status {
    if (!out || out->size < sizeof(*out))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid progress output");
    auto value = progress_snapshot(*s);
    value.size = out->size;
    *out = value;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_set_progress_callback(ccl_session *s,
                                                      ccl_progress_fn fn,
                                                      void *user)
{
  return session_access(s, [&]() -> ccl_status {
    if (auto st = idle(*s))
      return st;
    s->progress_fn = fn;
    s->progress_user = user;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_session_set_output_callback(
    ccl_session *s, const ccl_output_pass *passes, uint32_t count, ccl_output_fn fn, void *user)
{
  return session_access(s, [&]() -> ccl_status {
    if (auto st = idle(*s))
      return st;
    if (count && (!passes || !fn))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Missing passes/callback");
    std::vector<ccl_session::OutputPass> copy;
    std::unordered_set<std::string> names;
    for (uint32_t i = 0; i < count; ++i) {
      const auto &p = passes[i];
      if (p.size < sizeof(p) || !p.name || !*p.name || p.channels < 1 || p.channels > 4 ||
          !names.insert(p.name).second)
        return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid or duplicate output pass");
      copy.push_back({p.name, p.channels});
    }
    s->output_passes = std::move(copy);
    s->output_fn = count ? fn : nullptr;
    s->output_user = user;
    s->needs_reset = true;
    return CCL_SUCCESS;
  });
}
