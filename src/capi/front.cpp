/* SPDX-License-Identifier: Apache-2.0 */
#include "dispatch_table.h"
#include "windows_preflight.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>

namespace {
std::atomic<const ccl_backend_table *> dispatch{nullptr};
std::mutex load_mutex;
ccl_log_fn pending_log = nullptr;
void *pending_log_user = nullptr;
thread_local bool front_error = true;
thread_local std::string error_message;
thread_local bool error_allocation_failed = false;

ccl_status front_fail(ccl_status status, const char *message) noexcept
{
  front_error = true;
  try {
    error_message = message;
    error_allocation_failed = false;
  }
  catch (...) {
    error_allocation_failed = true;
    return CCL_ERROR_OUT_OF_MEMORY;
  }
  return status;
}
ccl_status fail(ccl_status status, const char *message) noexcept
{
  return front_fail(status, message);
}
template<class F> ccl_status boundary(F fn) noexcept
{
  front_error = true;
  error_message.clear();
  error_allocation_failed = false;
  try {
    return fn();
  }
  catch (const std::bad_alloc &) {
    return fail(CCL_ERROR_OUT_OF_MEMORY, "Allocation failed");
  }
  catch (const std::exception &e) {
    return fail(CCL_ERROR_INTERNAL, e.what());
  }
  catch (...) {
    return fail(CCL_ERROR_INTERNAL, "Unknown loader exception");
  }
}
}  // namespace

/* Bootstrap calls remain usable without loading native dependencies. After publication
 * they go through the backend, including its callback-reentry and error boundaries. */
#define CCL_BOOTSTRAP_FORWARD(name, args) \
  if (const auto *api = dispatch.load(std::memory_order_acquire)) { \
    front_error = false; \
    return api->name args; \
  }
#include "bootstrap_api.inc"

const char *CCL_CALL ccl_last_error(void)
{
  const auto *api = dispatch.load(std::memory_order_acquire);
  return front_error || !api ? (error_allocation_failed ? "Cannot allocate loader error message" :
                                                          error_message.c_str()) :
                               api->ccl_last_error();
}

ccl_status CCL_CALL ccl_set_log_callback(ccl_log_fn fn, void *user)
{
  CCL_BOOTSTRAP_FORWARD(ccl_set_log_callback, (fn, user));
  return boundary([&]() -> ccl_status {
    std::lock_guard<std::mutex> lock(load_mutex);
    CCL_BOOTSTRAP_FORWARD(ccl_set_log_callback, (fn, user));
    pending_log = fn;
    pending_log_user = user;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_init(const ccl_init_params *params)
{
  CCL_BOOTSTRAP_FORWARD(ccl_init, (params));
  return boundary([&]() -> ccl_status {
    if (!params || params->size < sizeof(*params) || !params->resource_path ||
        !params->user_path || !std::filesystem::u8path(params->resource_path).is_absolute() ||
        !std::filesystem::u8path(params->user_path).is_absolute())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Init requires full-size params and absolute paths");
    std::lock_guard<std::mutex> lock(load_mutex);
    CCL_BOOTSTRAP_FORWARD(ccl_init, (params));
    HMODULE front = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ccl_init),
                            &front))
      return fail(CCL_ERROR_INTERNAL, "Cannot locate cycles_c front DLL");
    const auto directory = loader::module_path(front).parent_path();
    const auto backend_path = directory / L"cycles_c_backend_private.dll";
    const auto dependencies = std::filesystem::u8path(params->resource_path);
    const auto mismatch = loader::preflight(backend_path, dependencies);
    if (!mismatch.empty())
      return fail(CCL_ERROR_DEPENDENCY_MISMATCH, mismatch.c_str());
    // Dependencies must be installed alongside the backend, or already loaded by the host.
    HMODULE backend = LoadLibraryExW(backend_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!backend)
      return fail(CCL_ERROR_DEPENDENCY_MISMATCH,
                  "Private backend could not load after import preflight; verify the complete "
                  "version-matched dependency installation");
    // Retain the backend until process exit, even on a rejected handshake: native teardown
    // cannot safely be assumed unloadable after static registration has run.
    auto getter = reinterpret_cast<ccl_table_getter>(
        GetProcAddress(backend, "cycles_c_private_get_table"));
    const auto *api = getter ? getter(1, 7, sizeof(ccl_backend_table), CCL_TABLE_SIGNATURE) :
                               nullptr;
    if (!api)
      return fail(CCL_ERROR_DEPENDENCY_MISMATCH,
                  "Private backend API 1.7 table handshake mismatch");
    ccl_status status = api->ccl_set_log_callback(pending_log, pending_log_user);
    if (status == CCL_SUCCESS)
      status = api->ccl_init(params);
    if (status != CCL_SUCCESS)
      return fail(status, api->ccl_last_error());
    dispatch.store(api, std::memory_order_release);
    front_error = false;
    return CCL_SUCCESS;
  });
}

#include "dispatch_forward.inc"
