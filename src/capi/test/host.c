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
  LOAD(ccl_node_type_count, ccl_status, (uint32_t *));
  LOAD(ccl_node_type_info, ccl_status, (uint32_t, ccl_node_type_desc *));
  LOAD(ccl_socket_info,
       ccl_status,
       (const char *, ccl_socket_direction, uint32_t, ccl_socket_desc *));
  LOAD(ccl_socket_default_value,
       ccl_status,
       (const char *, ccl_socket_direction, uint32_t, uint64_t, ccl_default_value *));
  LOAD(ccl_socket_enum_value,
       ccl_status,
       (const char *, ccl_socket_direction, uint32_t, uint32_t, ccl_enum_value *));
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
  uint32_t nodes_before_init = 999;
  CHECK(ccl_node_type_count(&nodes_before_init) == CCL_ERROR_NOT_INITIALIZED &&
        nodes_before_init == 999);
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
  CHECK(ccl_node_type_count(&count) == CCL_SUCCESS && count > 0);
  for (uint32_t n = 0; n < count; ++n) {
    ccl_node_type_desc node = {0};
    node.size = sizeof(node);
    CHECK(ccl_node_type_info(n, &node) == CCL_SUCCESS);
    printf("NODE %s %s %d %d %u %u\n",
           node.name,
           node.base,
           node.is_shader,
           node.is_abstract,
           node.input_count,
           node.output_count);
    for (int32_t direction = 0; direction < 2; ++direction) {
      uint32_t sockets = direction ? node.output_count : node.input_count;
      for (uint32_t i = 0; i < sockets; ++i) {
        ccl_socket_desc socket = {0};
        socket.size = sizeof(socket);
        CHECK(ccl_socket_info(node.name, direction, i, &socket) == CCL_SUCCESS);
        CHECK(socket.name && socket.ui_name && socket.node_type);
        ccl_default_value invalid_default = {0};
        invalid_default.size = sizeof(invalid_default);
        CHECK(ccl_socket_default_value(
                  node.name, direction, i, socket.default_count, &invalid_default) ==
              (socket.has_default ? CCL_ERROR_INVALID_ARGUMENT : CCL_ERROR_NO_DEFAULT));
        CHECK(invalid_default.type == 0);
        ccl_enum_value invalid_enum = {0};
        invalid_enum.size = sizeof(invalid_enum);
        CHECK(ccl_socket_enum_value(node.name, direction, i, socket.enum_count, &invalid_enum) ==
              CCL_ERROR_INVALID_ARGUMENT);
        CHECK(invalid_enum.name == NULL);
        printf("SOCKET %d %s | %s | %d %u %s %d %llu %u\n",
               direction,
               socket.name,
               socket.ui_name,
               socket.type,
               socket.flags,
               socket.node_type,
               socket.has_default,
               (unsigned long long)socket.default_count,
               socket.enum_count);
        for (uint64_t j = 0; j < socket.default_count; ++j) {
          ccl_default_value value = {0};
          value.size = sizeof(value);
          CHECK(ccl_socket_default_value(node.name, direction, i, j, &value) == CCL_SUCCESS);
          CHECK(value.type != CCL_SOCKET_STRING || value.string_value != NULL);
          printf("DEFAULT %d %d %llu %d %s",
                 value.type,
                 value.signed_value,
                 (unsigned long long)value.unsigned_value,
                 value.node_is_null,
                 value.string_value ? value.string_value : "");
          for (int k = 0; k < 12; ++k)
            printf(" %a", (double)value.floats[k]);
          puts("");
        }
        for (uint32_t j = 0; j < socket.enum_count; ++j) {
          ccl_enum_value value = {0};
          value.size = sizeof(value);
          CHECK(ccl_socket_enum_value(node.name, direction, i, j, &value) == CCL_SUCCESS);
          printf("ENUM %s %d\n", value.name, value.value);
        }
      }
    }
  }
  ccl_node_type_desc bad_node = {0};
  CHECK(ccl_node_type_info(0, &bad_node) == CCL_ERROR_INVALID_ARGUMENT);
  bad_node.size = sizeof(bad_node);
  CHECK(ccl_node_type_info(count, &bad_node) == CCL_ERROR_INVALID_ARGUMENT &&
        bad_node.name == NULL);
  CHECK(ccl_node_type_count(NULL) == CCL_ERROR_INVALID_ARGUMENT);
  ccl_socket_desc bad_socket = {0};
  bad_socket.size = sizeof(bad_socket);
  CHECK(ccl_socket_info("missing_node", 0, 0, &bad_socket) == CCL_ERROR_NOT_FOUND);
  CHECK(bad_socket.name == NULL);
  CHECK(ccl_socket_info("camera", -1, 0, &bad_socket) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_socket_info("camera", 0, UINT32_MAX, &bad_socket) == CCL_ERROR_INVALID_ARGUMENT);
  CHECK(ccl_socket_info(NULL, 0, 0, &bad_socket) == CCL_ERROR_INVALID_ARGUMENT);
  puts("PASS C11 dynamic host");
  return 0; /* Library lifetime is process lifetime in Phase 1. */
}
