/* SPDX-License-Identifier: Apache-2.0 */
/* Deliberately C11; loads the DLL without an import library or Cycles headers. */
#include "../cycles_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#  define OPEN(p) LoadLibraryExA(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)
#  define SYMBOL(h, n) GetProcAddress(h, n)
#else
#  include <dlfcn.h>
#  define OPEN(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#  define SYMBOL(h, n) dlsym(h, n)
#endif
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
      return 1; \
    } \
  } while (0)
#define LOAD(name, result, args) \
  result(CCL_CALL *name) args = (result(CCL_CALL *) args)SYMBOL(library, #name); \
  CHECK(name)

int main(int argc, char **argv)
{
  CHECK(argc == 4);
#ifdef _WIN32
  HMODULE library = OPEN(argv[1]);
#else
  void *library = OPEN(argv[1]);
#endif
  CHECK(library);
  LOAD(ccl_api_version, ccl_status, (ccl_version *));
  LOAD(ccl_has_feature, ccl_status, (const char *, int32_t *));
  LOAD(ccl_init, ccl_status, (const ccl_init_params *));
  LOAD(ccl_last_error, const char *, (void));
  LOAD(ccl_set_log_callback, ccl_status, (ccl_log_fn, void *));
  LOAD(ccl_device_count, ccl_status, (uint32_t *));
  LOAD(ccl_device_info, ccl_status, (uint32_t, ccl_device_desc *));
  ccl_version version = {sizeof(version), 0, 0, 0};
  CHECK(ccl_api_version(&version) == CCL_SUCCESS && version.major == 1);
  printf("Cycles C API %u.%u.%u\n", version.major, version.minor, version.patch);
  int32_t supported = -1;
  CHECK(ccl_has_feature("cpu", &supported) == CCL_SUCCESS && supported == 1);
  CHECK(ccl_has_feature("deep", &supported) == CCL_SUCCESS && supported == 0);
  CHECK(ccl_has_feature("unknown", &supported) == CCL_SUCCESS && supported == 0);
  uint32_t count = 999;
  CHECK(ccl_device_count(&count) == CCL_ERROR_NOT_INITIALIZED && count == 999);
  CHECK(strlen(ccl_last_error()) > 0);
  ccl_init_params params = {sizeof(params), argv[2], argv[3]};
  CHECK(ccl_set_log_callback(NULL, NULL) == CCL_SUCCESS);
  CHECK(ccl_init(&params) == CCL_SUCCESS);
  CHECK(ccl_init(&params) == CCL_SUCCESS);
  CHECK(ccl_device_count(&count) == CCL_SUCCESS && count > 0);
  CHECK(ccl_last_error()[0] == '\0');
  int cpu = 0;
  for (uint32_t i = 0; i < count; ++i) {
    ccl_device_desc device = {0};
    device.size = sizeof(device);
    CHECK(ccl_device_info(i, &device) == CCL_SUCCESS);
    CHECK(device.id && device.description && device.backend && device.error);
    cpu |= strcmp(device.backend, "CPU") == 0;
    printf(
        "%u: %s | %s | %s | %s\n", i, device.backend, device.id, device.description, device.error);
  }
  CHECK(cpu);
  ccl_device_desc invalid = {0};
  invalid.size = sizeof(invalid);
  CHECK(ccl_device_info(count, &invalid) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(invalid.id == NULL);
  CHECK(ccl_device_count(NULL) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_api_version(NULL) == CCL_ERROR_INVALID_ARGUMENT);
  puts("PASS C11 dynamic host");
  return 0; /* Library lifetime is process lifetime in Phase 1. */
}
