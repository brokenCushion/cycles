/* SPDX-License-Identifier: Apache-2.0 */
/* Private white-box tests exercise exception/callback boundaries without adding exports. */
#include "../cycles_c.cpp"
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#define CHECK(x) \
  do { \
    if (!(x)) { \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
      std::abort(); \
    } \
  } while (0)

static void CCL_CALL callback(ccl_log_level level, const char *message, void *user)
{
  CHECK(level == CCL_LOG_INFO && std::strcmp(message, "contract") == 0);
  ++*static_cast<int *>(user);
  CHECK(ccl_set_log_callback(nullptr, nullptr) == CCL_ERROR_CALLBACK_REENTRY);
  CHECK(ccl_api_version(nullptr) == CCL_ERROR_CALLBACK_REENTRY);
}

int main()
{
  CHECK(boundary([]() -> ccl_status { throw std::bad_alloc(); }) == CCL_ERROR_OUT_OF_MEMORY);
  CHECK(boundary([]() -> ccl_status { throw std::runtime_error("native failure"); }) ==
        CCL_ERROR_INTERNAL);
  CHECK(std::strcmp(ccl_last_error(), "native failure") == 0);
  std::thread other([] {
    CHECK(ccl_last_error()[0] == '\0');
    CHECK(ccl_api_version(nullptr) == CCL_ERROR_INVALID_ARGUMENT);
  });
  other.join();
  CHECK(std::strcmp(ccl_last_error(), "native failure") == 0);
  CHECK(boundary([]() -> ccl_status { throw 42; }) == CCL_ERROR_INTERNAL);
  struct Extended {
    ccl_version v;
    uint32_t tail;
  } extended = {{sizeof(Extended), 0, 0, 0}, 12345};
  CHECK(ccl_api_version(&extended.v) == CCL_SUCCESS && extended.tail == 12345);
  extended.v.size = 0;
  extended.v.major = 999;
  CHECK(ccl_api_version(&extended.v) == CCL_ERROR_INVALID_ARGUMENT && extended.v.major == 999);
  ccl_init_params invalid = {sizeof(invalid), "relative", "relative"};
  CHECK(ccl_init(&invalid) == CCL_ERROR_INVALID_ARGUMENT);
  const std::string path = std::filesystem::current_path().string();
  ccl_init_params init = {sizeof(init), path.c_str(), path.c_str()};
  CHECK(ccl_init(&init) == CCL_SUCCESS);
  std::string changed = path + "/different";
  init.user_path = changed.c_str();
  CHECK(ccl_init(&init) == CCL_ERROR_ALREADY_INITIALIZED);
  int calls = 0;
  CHECK(ccl_set_log_callback(callback, &calls) == CCL_SUCCESS);
  CHECK(boundary([]() -> ccl_status {
          ccl::_log_message(ccl::LOG_LEVEL_INFO_IMPORTANT, "test", "test", "contract");
          return CCL_SUCCESS;
        }) == CCL_SUCCESS);
  CHECK(calls == 1 && ccl_last_error()[0] == '\0');
  CHECK(ccl_set_log_callback(nullptr, nullptr) == CCL_SUCCESS);
  for (const auto type : ccl::Device::available_types()) {
    std::string name = ccl::Device::string_from_type(type);
    for (char &ch : name) {
      ch = char(std::tolower(static_cast<unsigned char>(ch)));
    }
    int32_t supported = 0;
    CHECK(ccl_has_feature(name.c_str(), &supported) == CCL_SUCCESS && supported == 1);
  }
  uint32_t count;
  CHECK(ccl_device_count(&count) == CCL_SUCCESS);
  const auto native = ccl::Device::available_devices();
  CHECK(count == native.size());
  for (uint32_t i = 0; i < count; ++i) {
    ccl_device_desc desc = {};
    desc.size = sizeof(desc);
    CHECK(ccl_device_info(i, &desc) == CCL_SUCCESS);
    CHECK(native[i].id == desc.id && native[i].description == desc.description);
    CHECK(ccl::Device::string_from_type(native[i].type) == desc.backend);
    CHECK(native[i].error_msg == desc.error && native[i].has_osl == bool(desc.has_osl));
    CHECK(native[i].has_nanovdb == bool(desc.has_nanovdb));
    CHECK(native[i].display_device == bool(desc.display_device));
    CHECK(native[i].meets_driver_requirement == bool(desc.meets_driver_requirement));
  }
  std::puts(
      "PASS exception containment, TLS, struct sizes, init, callback reentry, native device "
      "parity");
}
