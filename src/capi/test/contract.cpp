/* SPDX-License-Identifier: Apache-2.0 */
/* Private white-box tests exercise exception/callback boundaries without adding exports. */
#include "../cycles_c.cpp"
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#define TEST_CHECK(x) \
  do { \
    if (!(x)) { \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
      std::abort(); \
    } \
  } while (0)

static void CCL_CALL callback(ccl_log_level level, const char *message, void *user)
{
  TEST_CHECK(level == CCL_LOG_INFO && std::strcmp(message, "contract") == 0);
  ++*static_cast<int *>(user);
  TEST_CHECK(ccl_set_log_callback(nullptr, nullptr) == CCL_ERROR_CALLBACK_REENTRY);
  TEST_CHECK(ccl_api_version(nullptr) == CCL_ERROR_CALLBACK_REENTRY);
}

template<typename T, typename Check>
static void check_array(ccl::SocketType::Type type, ccl_socket_type scalar, T element, Check check)
{
  ccl::array<T> values(2);
  values[0] = T{};
  values[1] = element;
  ccl::SocketType socket;
  socket.type = type;
  socket.default_value = &values;
  TEST_CHECK(default_count(socket) == 2);
  auto result = default_value(socket, 1);
  TEST_CHECK(result.type == scalar);
  check(result);
}

static void check_array_defaults()
{
  check_array(ccl::SocketType::BOOLEAN_ARRAY, CCL_SOCKET_BOOLEAN, true, [](auto v) {
    TEST_CHECK(v.signed_value == 1);
  });
  check_array(ccl::SocketType::FLOAT_ARRAY, CCL_SOCKET_FLOAT, -1.25f, [](auto v) {
    TEST_CHECK(v.floats[0] == -1.25f);
  });
  check_array(ccl::SocketType::INT_ARRAY, CCL_SOCKET_INT, -99, [](auto v) {
    TEST_CHECK(v.signed_value == -99);
  });
  const ccl::packed_float3 triple(ccl::make_float3(1.0f, -2.0f, 3.0f));
  for (auto type : {ccl::SocketType::COLOR_ARRAY,
                    ccl::SocketType::VECTOR_ARRAY,
                    ccl::SocketType::POINT_ARRAY,
                    ccl::SocketType::NORMAL_ARRAY})
  {
    const auto scalar = type == ccl::SocketType::COLOR_ARRAY  ? CCL_SOCKET_COLOR :
                        type == ccl::SocketType::VECTOR_ARRAY ? CCL_SOCKET_VECTOR :
                        type == ccl::SocketType::POINT_ARRAY  ? CCL_SOCKET_POINT :
                                                                CCL_SOCKET_NORMAL;
    check_array(type, scalar, triple, [](auto v) {
      TEST_CHECK(v.floats[0] == 1 && v.floats[1] == -2 && v.floats[2] == 3);
    });
  }
  check_array(ccl::SocketType::POINT2_ARRAY,
              CCL_SOCKET_POINT2,
              ccl::make_float2(7, 8),
              [](auto v) { TEST_CHECK(v.floats[0] == 7 && v.floats[1] == 8); });
  check_array(ccl::SocketType::STRING_ARRAY,
              CCL_SOCKET_STRING,
              ccl::ustring("UTF-8: \xce\xbb"),
              [](auto v) { TEST_CHECK(std::strcmp(v.string_value, "UTF-8: \xce\xbb") == 0); });
  ccl::Transform transform;
  for (int i = 0; i < 3; ++i)
    transform[i] = ccl::make_float4(i * 4 + 1, i * 4 + 2, i * 4 + 3, i * 4 + 4);
  check_array(ccl::SocketType::TRANSFORM_ARRAY, CCL_SOCKET_TRANSFORM, transform, [](auto v) {
    for (int i = 0; i < 12; ++i)
      TEST_CHECK(v.floats[i] == i + 1);
  });
  check_array(ccl::SocketType::NODE_ARRAY,
              CCL_SOCKET_NODE,
              static_cast<ccl::Node *>(nullptr),
              [](auto v) { TEST_CHECK(v.node_is_null == 1); });
  TEST_CHECK(boundary([]() -> ccl_status {
               socket_type(ccl::SocketType::NUM_TYPES);
               return CCL_SUCCESS;
             }) == CCL_ERROR_INTERNAL);
  TEST_CHECK(boundary([]() -> ccl_status {
               socket_flags(1 << 20);
               return CCL_SUCCESS;
             }) == CCL_ERROR_INTERNAL);
  std::puts(
      "PASS nonempty default conversion for all eleven array types and unmapped metadata "
      "rejection");
}

int main()
{
  check_array_defaults();
  TEST_CHECK(boundary([]() -> ccl_status { throw std::bad_alloc(); }) == CCL_ERROR_OUT_OF_MEMORY);
  TEST_CHECK(boundary([]() -> ccl_status { throw std::runtime_error("native failure"); }) ==
             CCL_ERROR_INTERNAL);
  TEST_CHECK(std::strcmp(ccl_last_error(), "native failure") == 0);
  std::thread other([] {
    TEST_CHECK(ccl_last_error()[0] == '\0');
    TEST_CHECK(ccl_api_version(nullptr) == CCL_ERROR_INVALID_ARGUMENT);
  });
  other.join();
  TEST_CHECK(std::strcmp(ccl_last_error(), "native failure") == 0);
  TEST_CHECK(boundary([]() -> ccl_status { throw 42; }) == CCL_ERROR_INTERNAL);
  struct Extended {
    ccl_version v;
    uint32_t tail;
  } extended = {{sizeof(Extended), 0, 0, 0}, 12345};
  TEST_CHECK(ccl_api_version(&extended.v) == CCL_SUCCESS && extended.tail == 12345);
  extended.v.size = 0;
  extended.v.major = 999;
  TEST_CHECK(ccl_api_version(&extended.v) == CCL_ERROR_INVALID_ARGUMENT &&
             extended.v.major == 999);
  ccl_init_params invalid = {sizeof(invalid), "relative", "relative"};
  TEST_CHECK(ccl_init(&invalid) == CCL_ERROR_INVALID_ARGUMENT);
  const std::string path = std::filesystem::current_path().string();
  ccl_init_params init = {sizeof(init), path.c_str(), path.c_str()};
  TEST_CHECK(ccl_init(&init) == CCL_SUCCESS);
  std::string changed = path + "/different";
  init.user_path = changed.c_str();
  TEST_CHECK(ccl_init(&init) == CCL_ERROR_ALREADY_INITIALIZED);
  int calls = 0;
  TEST_CHECK(ccl_set_log_callback(callback, &calls) == CCL_SUCCESS);
  TEST_CHECK(boundary([]() -> ccl_status {
               ccl::_log_message(ccl::LOG_LEVEL_INFO_IMPORTANT, "test", "test", "contract");
               return CCL_SUCCESS;
             }) == CCL_SUCCESS);
  TEST_CHECK(calls == 1 && ccl_last_error()[0] == '\0');
  TEST_CHECK(ccl_set_log_callback(nullptr, nullptr) == CCL_SUCCESS);
  for (const auto type : ccl::Device::available_types()) {
    std::string name = ccl::Device::string_from_type(type);
    for (char &ch : name) {
      ch = char(std::tolower(static_cast<unsigned char>(ch)));
    }
    int32_t supported = 0;
    TEST_CHECK(ccl_has_feature(name.c_str(), &supported) == CCL_SUCCESS && supported == 1);
  }
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i)
    readers.emplace_back([] {
      for (int j = 0; j < 100; ++j) {
        uint32_t count = 0;
        TEST_CHECK(ccl_node_type_count(&count) == CCL_SUCCESS && count > 0);
        struct ExtendedNode {
          ccl_node_type_desc node;
          uint64_t tail;
        } n = {};
        n.node.size = sizeof(n);
        n.tail = UINT64_C(0x123456789);
        TEST_CHECK(ccl_node_type_info(uint32_t(j) % count, &n.node) == CCL_SUCCESS);
        TEST_CHECK(n.tail == UINT64_C(0x123456789) && n.node.name);
      }
    });
  for (auto &reader : readers)
    reader.join();
  uint32_t count;
  TEST_CHECK(ccl_device_count(&count) == CCL_SUCCESS);
  const auto native = ccl::Device::available_devices();
  TEST_CHECK(count == native.size());
  for (uint32_t i = 0; i < count; ++i) {
    ccl_device_desc desc = {};
    desc.size = sizeof(desc);
    TEST_CHECK(ccl_device_info(i, &desc) == CCL_SUCCESS);
    TEST_CHECK(native[i].id == desc.id && native[i].description == desc.description);
    TEST_CHECK(ccl::Device::string_from_type(native[i].type) == desc.backend);
    TEST_CHECK(native[i].error_msg == desc.error && native[i].has_osl == bool(desc.has_osl));
    TEST_CHECK(native[i].has_nanovdb == bool(desc.has_nanovdb));
    TEST_CHECK(native[i].display_device == bool(desc.display_device));
    TEST_CHECK(native[i].meets_driver_requirement == bool(desc.meets_driver_requirement));
  }
  std::puts(
      "PASS exception containment, TLS, struct sizes, init, callback reentry, native device "
      "parity");
}
