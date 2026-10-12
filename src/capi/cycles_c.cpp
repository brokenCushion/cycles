/* SPDX-License-Identifier: Apache-2.0 */
#include "cycles_c.h"

#include "device/device.h"
#include "util/log.h"
#include "util/path.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>

namespace {
thread_local char error_message[1024] = {};
thread_local bool in_callback = false;
std::mutex api_mutex, log_mutex;
ccl_log_fn log_callback = nullptr;
void *log_user = nullptr;
bool initialized = false, discovered = false;
std::string resource_path, user_path;
ccl::vector<ccl::DeviceInfo> devices;
ccl::vector<std::string> backend_names;

ccl_status fail(ccl_status status, const char *message) noexcept
{
  std::snprintf(error_message, sizeof(error_message), "%s", message);
  return status;
}

template<typename F> ccl_status boundary(F fn) noexcept
{
  if (in_callback) {
    return fail(CCL_ERROR_CALLBACK_REENTRY, "API reentry from a log callback is forbidden");
  }
  error_message[0] = '\0';
  try {
    const ccl_status status = fn();
    if (status == CCL_SUCCESS) {
      error_message[0] = '\0';
    }
    return status;
  }
  catch (const std::bad_alloc &) {
    return fail(CCL_ERROR_OUT_OF_MEMORY, "Allocation failed");
  }
  catch (const std::exception &e) {
    return fail(CCL_ERROR_INTERNAL, e.what());
  }
  catch (...) {
    return fail(CCL_ERROR_INTERNAL, "Unknown native exception");
  }
}

void log_bridge(ccl::LogLevel level, const char *, const char *, const char *message) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_callback) {
      std::fprintf(stderr, "%s: %s\n", ccl::log_level_to_string(level), message);
      return;
    }
    const ccl_log_level severity = level <= ccl::LOG_LEVEL_DFATAL   ? CCL_LOG_FATAL :
                                   level <= ccl::LOG_LEVEL_DERROR   ? CCL_LOG_ERROR :
                                   level <= ccl::LOG_LEVEL_DWARNING ? CCL_LOG_WARNING :
                                   level <= ccl::LOG_LEVEL_INFO     ? CCL_LOG_INFO :
                                   level == ccl::LOG_LEVEL_DEBUG    ? CCL_LOG_DEBUG :
                                                                      CCL_LOG_TRACE;
    in_callback = true;
    try {
      log_callback(severity, message, log_user);
    }
    catch (...) {
      /* A misbehaving C++ host callback must not unwind through Cycles. */
    }
    in_callback = false;
  }
  catch (...) {
    in_callback = false;
  }
}

ccl_status discover()
{
  if (!initialized) {
    return fail(CCL_ERROR_NOT_INITIALIZED, "Call ccl_init first");
  }
  if (!discovered) {
    auto found = ccl::Device::available_devices();
    if (found.size() > UINT32_MAX) {
      return fail(CCL_ERROR_INTERNAL, "Device count exceeds ABI range");
    }
    ccl::vector<std::string> names;
    for (const auto &device : found) {
      names.push_back(ccl::Device::string_from_type(device.type));
    }
    devices = std::move(found);
    backend_names = std::move(names);
    discovered = true;
  }
  return CCL_SUCCESS;
}
}  // namespace

ccl_status CCL_CALL ccl_api_version(ccl_version *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out)) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid version output size");
    }
    out->major = 1;
    out->minor = out->patch = 0;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_has_feature(const char *name, int32_t *supported)
{
  return boundary([&]() -> ccl_status {
    if (!name || !supported) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL feature name or output");
    }
    bool available = std::strcmp(name, "cpu") == 0;
#ifdef WITH_CUDA
    available |= std::strcmp(name, "cuda") == 0;
#endif
#ifdef WITH_OPTIX
    available |= std::strcmp(name, "optix") == 0;
#endif
#ifdef WITH_HIP
    available |= std::strcmp(name, "hip") == 0;
#endif
#ifdef WITH_HIPRT
    available |= std::strcmp(name, "hiprt") == 0;
#endif
#ifdef WITH_METAL
    available |= std::strcmp(name, "metal") == 0;
#endif
#ifdef WITH_ONEAPI
    available |= std::strcmp(name, "oneapi") == 0;
#endif
#ifdef WITH_OSL
    available |= std::strcmp(name, "osl") == 0;
#endif
    *supported = available ? 1 : 0;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_init(const ccl_init_params *params)
{
  return boundary([&]() -> ccl_status {
    if (!params || params->size < sizeof(*params) || !params->resource_path ||
        !params->user_path || !std::filesystem::u8path(params->resource_path).is_absolute() ||
        !std::filesystem::u8path(params->user_path).is_absolute())
    {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Init requires full-size params and absolute paths");
    }
    std::lock_guard<std::mutex> lock(api_mutex);
    if (initialized) {
      return resource_path == params->resource_path && user_path == params->user_path ?
                 CCL_SUCCESS :
                 fail(CCL_ERROR_ALREADY_INITIALIZED, "Resource paths are already initialized");
    }
    resource_path = params->resource_path;
    user_path = params->user_path;
    ccl::path_init(resource_path, user_path);
    ccl::log_init(log_bridge);
    initialized = true;
    return CCL_SUCCESS;
  });
}

const char *CCL_CALL ccl_last_error(void)
{
  return error_message;
}

ccl_status CCL_CALL ccl_set_log_callback(ccl_log_fn fn, void *user)
{
  return boundary([&]() -> ccl_status {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_callback = fn;
    log_user = user;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_device_count(uint32_t *count)
{
  return boundary([&]() -> ccl_status {
    if (!count) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL device count output");
    }
    std::lock_guard<std::mutex> lock(api_mutex);
    const ccl_status status = discover();
    if (status == CCL_SUCCESS) {
      *count = uint32_t(devices.size());
    }
    return status;
  });
}

ccl_status CCL_CALL ccl_device_info(uint32_t index, ccl_device_desc *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out)) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid device output size");
    }
    std::lock_guard<std::mutex> lock(api_mutex);
    const ccl_status status = discover();
    if (status != CCL_SUCCESS) {
      return status;
    }
    if (index >= devices.size()) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Device index out of range");
    }
    const auto &d = devices[index];
    out->id = d.id.c_str();
    out->description = d.description.c_str();
    out->backend = backend_names[index].c_str();
    out->error = d.error_msg.c_str();
    out->display_device = d.display_device;
    out->has_osl = d.has_osl;
    out->has_nanovdb = d.has_nanovdb;
    out->meets_driver_requirement = d.meets_driver_requirement;
    return CCL_SUCCESS;
  });
}
