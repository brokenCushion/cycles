/* SPDX-License-Identifier: Apache-2.0 */
#include "cycles_c.h"

#include "device/device.h"
#include "graph/node_type.h"
#include "util/log.h"
#include "util/path.h"
#include "util/transform.h"
#include <algorithm>

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
    return fail(CCL_ERROR_CALLBACK_REENTRY, "API reentry from a callback is forbidden");
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
  const bool previous_callback = in_callback;
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
    in_callback = previous_callback;
  }
  catch (...) {
    in_callback = previous_callback;
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

#define CCL_BOOTSTRAP_FORWARD(name, args)
#include "bootstrap_api.inc"

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

namespace {
ccl::vector<const ccl::NodeType *> node_types;
bool reflected = false;
const char *text(ccl::ustring value)
{
  return value.empty() ? "" : value.c_str();
}
ccl_status reflect()
{
  if (!initialized)
    return fail(CCL_ERROR_NOT_INITIALIZED, "Call ccl_init first");
  if (!reflected) {
    auto names = ccl::NodeType::type_names();
    std::sort(names.begin(), names.end(), [](auto a, auto b) {
      return std::strcmp(a.c_str(), b.c_str()) < 0;
    });
    ccl::vector<const ccl::NodeType *> found;
    for (const auto &name : names)
      found.push_back(ccl::NodeType::find(name));
    if (found.size() > UINT32_MAX)
      return fail(CCL_ERROR_INTERNAL, "Too many node types");
    node_types = std::move(found);
    reflected = true;
  }
  return CCL_SUCCESS;
}
ccl_status socket(const char *name,
                  ccl_socket_direction direction,
                  uint32_t index,
                  const ccl::SocketType *&out)
{
  if (!name || (direction != CCL_SOCKET_INPUT && direction != CCL_SOCKET_OUTPUT))
    return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node name or direction");
  const auto status = reflect();
  if (status != CCL_SUCCESS)
    return status;
  for (const auto *node : node_types) {
    if (std::strcmp(node->name.c_str(), name) != 0)
      continue;
    const auto &sockets = direction == CCL_SOCKET_INPUT ? node->inputs : node->outputs;
    if (index >= sockets.size())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Socket index out of range");
    out = &sockets[index];
    return CCL_SUCCESS;
  }
  return fail(CCL_ERROR_NOT_FOUND, "Unknown node type");
}
ccl_socket_type socket_type(ccl::SocketType::Type type)
{
  switch (type) {
    case ccl::SocketType::UNDEFINED:
      return CCL_SOCKET_UNDEFINED;
    case ccl::SocketType::BOOLEAN:
      return CCL_SOCKET_BOOLEAN;
    case ccl::SocketType::FLOAT:
      return CCL_SOCKET_FLOAT;
    case ccl::SocketType::INT:
      return CCL_SOCKET_INT;
    case ccl::SocketType::UINT:
      return CCL_SOCKET_UINT;
    case ccl::SocketType::UINT64:
      return CCL_SOCKET_UINT64;
    case ccl::SocketType::COLOR:
      return CCL_SOCKET_COLOR;
    case ccl::SocketType::VECTOR:
      return CCL_SOCKET_VECTOR;
    case ccl::SocketType::POINT:
      return CCL_SOCKET_POINT;
    case ccl::SocketType::NORMAL:
      return CCL_SOCKET_NORMAL;
    case ccl::SocketType::POINT2:
      return CCL_SOCKET_POINT2;
    case ccl::SocketType::CLOSURE:
      return CCL_SOCKET_CLOSURE;
    case ccl::SocketType::STRING:
      return CCL_SOCKET_STRING;
    case ccl::SocketType::ENUM:
      return CCL_SOCKET_ENUM;
    case ccl::SocketType::TRANSFORM:
      return CCL_SOCKET_TRANSFORM;
    case ccl::SocketType::NODE:
      return CCL_SOCKET_NODE;
    case ccl::SocketType::BOOLEAN_ARRAY:
      return CCL_SOCKET_BOOLEAN_ARRAY;
    case ccl::SocketType::FLOAT_ARRAY:
      return CCL_SOCKET_FLOAT_ARRAY;
    case ccl::SocketType::INT_ARRAY:
      return CCL_SOCKET_INT_ARRAY;
    case ccl::SocketType::COLOR_ARRAY:
      return CCL_SOCKET_COLOR_ARRAY;
    case ccl::SocketType::VECTOR_ARRAY:
      return CCL_SOCKET_VECTOR_ARRAY;
    case ccl::SocketType::POINT_ARRAY:
      return CCL_SOCKET_POINT_ARRAY;
    case ccl::SocketType::NORMAL_ARRAY:
      return CCL_SOCKET_NORMAL_ARRAY;
    case ccl::SocketType::POINT2_ARRAY:
      return CCL_SOCKET_POINT2_ARRAY;
    case ccl::SocketType::STRING_ARRAY:
      return CCL_SOCKET_STRING_ARRAY;
    case ccl::SocketType::TRANSFORM_ARRAY:
      return CCL_SOCKET_TRANSFORM_ARRAY;
    case ccl::SocketType::NODE_ARRAY:
      return CCL_SOCKET_NODE_ARRAY;
    default:
      throw std::runtime_error("Unmapped native socket type");
  }
}
uint32_t socket_flags(int flags)
{
  uint32_t result = 0;
  int known = 0;
  known |= ccl::SocketType::LINKABLE;
  if (flags & ccl::SocketType::LINKABLE)
    result |= CCL_SOCKET_LINKABLE;
  known |= ccl::SocketType::ANIMATABLE;
  if (flags & ccl::SocketType::ANIMATABLE)
    result |= CCL_SOCKET_ANIMATABLE;
  known |= ccl::SocketType::SVM_INTERNAL;
  if (flags & ccl::SocketType::SVM_INTERNAL)
    result |= CCL_SOCKET_SVM_INTERNAL;
  known |= ccl::SocketType::OSL_INTERNAL;
  if (flags & ccl::SocketType::OSL_INTERNAL)
    result |= CCL_SOCKET_OSL_INTERNAL;
  known |= ccl::SocketType::LINK_TEXTURE_GENERATED;
  if (flags & ccl::SocketType::LINK_TEXTURE_GENERATED)
    result |= CCL_SOCKET_LINK_TEXTURE_GENERATED;
  known |= ccl::SocketType::LINK_TEXTURE_NORMAL;
  if (flags & ccl::SocketType::LINK_TEXTURE_NORMAL)
    result |= CCL_SOCKET_LINK_TEXTURE_NORMAL;
  known |= ccl::SocketType::LINK_TEXTURE_UV;
  if (flags & ccl::SocketType::LINK_TEXTURE_UV)
    result |= CCL_SOCKET_LINK_TEXTURE_UV;
  known |= ccl::SocketType::LINK_TEXTURE_INCOMING;
  if (flags & ccl::SocketType::LINK_TEXTURE_INCOMING)
    result |= CCL_SOCKET_LINK_TEXTURE_INCOMING;
  known |= ccl::SocketType::LINK_INCOMING;
  if (flags & ccl::SocketType::LINK_INCOMING)
    result |= CCL_SOCKET_LINK_INCOMING;
  known |= ccl::SocketType::LINK_NORMAL;
  if (flags & ccl::SocketType::LINK_NORMAL)
    result |= CCL_SOCKET_LINK_NORMAL;
  known |= ccl::SocketType::LINK_POSITION;
  if (flags & ccl::SocketType::LINK_POSITION)
    result |= CCL_SOCKET_LINK_POSITION;
  known |= ccl::SocketType::LINK_TANGENT;
  if (flags & ccl::SocketType::LINK_TANGENT)
    result |= CCL_SOCKET_LINK_TANGENT;
  known |= ccl::SocketType::LINK_OSL_INITIALIZER;
  if (flags & ccl::SocketType::LINK_OSL_INITIALIZER)
    result |= CCL_SOCKET_LINK_OSL_INITIALIZER;
  if (flags & ~known)
    throw std::runtime_error("Unmapped native socket flag");
  return result;
}
uint64_t default_count(const ccl::SocketType &s)
{
  if (!s.default_value || s.type == ccl::SocketType::CLOSURE)
    return 0;
  switch (s.type) {
    case ccl::SocketType::BOOLEAN_ARRAY:
      return static_cast<const ccl::array<bool> *>(s.default_value)->size();
    case ccl::SocketType::FLOAT_ARRAY:
      return static_cast<const ccl::array<float> *>(s.default_value)->size();
    case ccl::SocketType::INT_ARRAY:
      return static_cast<const ccl::array<int> *>(s.default_value)->size();
    case ccl::SocketType::COLOR_ARRAY:
      return static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value)->size();
    case ccl::SocketType::VECTOR_ARRAY:
      return static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value)->size();
    case ccl::SocketType::POINT_ARRAY:
      return static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value)->size();
    case ccl::SocketType::NORMAL_ARRAY:
      return static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value)->size();
    case ccl::SocketType::POINT2_ARRAY:
      return static_cast<const ccl::array<ccl::float2> *>(s.default_value)->size();
    case ccl::SocketType::STRING_ARRAY:
      return static_cast<const ccl::array<ccl::ustring> *>(s.default_value)->size();
    case ccl::SocketType::TRANSFORM_ARRAY:
      return static_cast<const ccl::array<ccl::Transform> *>(s.default_value)->size();
    case ccl::SocketType::NODE_ARRAY:
      return static_cast<const ccl::array<ccl::Node *> *>(s.default_value)->size();
    default:
      return 1;
  }
}
ccl_default_value default_value(const ccl::SocketType &s, uint64_t element)
{
  ccl_default_value value = {};
  const void *data = s.default_value;
  value.type = socket_type(s.type);
  switch (s.type) {
    case ccl::SocketType::BOOLEAN_ARRAY:
      data = &(*static_cast<const ccl::array<bool> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_BOOLEAN;
      break;
    case ccl::SocketType::FLOAT_ARRAY:
      data = &(*static_cast<const ccl::array<float> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_FLOAT;
      break;
    case ccl::SocketType::INT_ARRAY:
      data = &(*static_cast<const ccl::array<int> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_INT;
      break;
    case ccl::SocketType::COLOR_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::packed_float3> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_COLOR;
      break;
    case ccl::SocketType::VECTOR_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::packed_float3> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_VECTOR;
      break;
    case ccl::SocketType::POINT_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::packed_float3> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_POINT;
      break;
    case ccl::SocketType::NORMAL_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::packed_float3> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_NORMAL;
      break;
    case ccl::SocketType::POINT2_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::float2> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_POINT2;
      break;
    case ccl::SocketType::STRING_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::ustring> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_STRING;
      break;
    case ccl::SocketType::TRANSFORM_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::Transform> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_TRANSFORM;
      break;
    case ccl::SocketType::NODE_ARRAY:
      data = &(*static_cast<const ccl::array<ccl::Node *> *>(data))[size_t(element)];
      value.type = CCL_SOCKET_NODE;
      break;
    default:
      break;
  }
  switch (value.type) {
    case CCL_SOCKET_BOOLEAN:
      value.signed_value = *static_cast<const bool *>(data);
      break;
    case CCL_SOCKET_INT:
    case CCL_SOCKET_ENUM:
      value.signed_value = *static_cast<const int *>(data);
      break;
    case CCL_SOCKET_UINT:
      value.unsigned_value = *static_cast<const unsigned int *>(data);
      break;
    case CCL_SOCKET_UINT64:
      value.unsigned_value = *static_cast<const uint64_t *>(data);
      break;
    case CCL_SOCKET_FLOAT:
      value.floats[0] = *static_cast<const float *>(data);
      break;
    case CCL_SOCKET_COLOR:
    case CCL_SOCKET_VECTOR:
    case CCL_SOCKET_POINT:
    case CCL_SOCKET_NORMAL:
      if (s.is_array()) {
        const auto &v = *static_cast<const ccl::packed_float3 *>(data);
        value.floats[0] = v.x;
        value.floats[1] = v.y;
        value.floats[2] = v.z;
      }
      else {
        const auto &v = *static_cast<const ccl::float3 *>(data);
        value.floats[0] = v.x;
        value.floats[1] = v.y;
        value.floats[2] = v.z;
      }
      break;
    case CCL_SOCKET_POINT2: {
      const auto &v = *static_cast<const ccl::float2 *>(data);
      value.floats[0] = v.x;
      value.floats[1] = v.y;
      break;
    }
    case CCL_SOCKET_TRANSFORM: {
      const auto &v = *static_cast<const ccl::Transform *>(data);
      for (int row = 0; row < 3; ++row) {
        value.floats[row * 4] = v[row].x;
        value.floats[row * 4 + 1] = v[row].y;
        value.floats[row * 4 + 2] = v[row].z;
        value.floats[row * 4 + 3] = v[row].w;
      }
      break;
    }
    case CCL_SOCKET_STRING:
      value.string_value = text(*static_cast<const ccl::ustring *>(data));
      break;
    case CCL_SOCKET_NODE:
      value.node_is_null = *static_cast<ccl::Node *const *>(data) == nullptr;
      break;
    default:
      throw std::runtime_error("Unrepresentable default type");
  }
  return value;
}
ccl_socket_desc describe_socket(const ccl::SocketType *s, uint32_t size)
{
  uint32_t enums = 0;
  if (s->enum_values)
    for (const auto &entry : *s->enum_values) {
      (void)entry;
      ++enums;
    }
  return {size,
          text(s->name),
          text(s->ui_name),
          socket_type(s->type),
          socket_flags(s->flags),
          s->node_type ? text(s->node_type->name) : "",
          s->default_value && s->type != ccl::SocketType::CLOSURE,
          default_count(*s),
          enums};
}
}  // namespace

ccl_status CCL_CALL ccl_node_type_count(uint32_t *count)
{
  return boundary([&]() -> ccl_status {
    if (!count)
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL node count");
    std::lock_guard<std::mutex> lock(api_mutex);
    const auto status = reflect();
    if (status == CCL_SUCCESS)
      *count = uint32_t(node_types.size());
    return status;
  });
}
ccl_status CCL_CALL ccl_node_type_info(uint32_t index, ccl_node_type_desc *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid node output size");
    std::lock_guard<std::mutex> lock(api_mutex);
    const auto status = reflect();
    if (status != CCL_SUCCESS)
      return status;
    if (index >= node_types.size())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Node index out of range");
    const auto &n = *node_types[index];
    *out = {out->size,
            text(n.name),
            n.base ? text(n.base->name) : "",
            n.type == ccl::NodeType::SHADER,
            !n.create,
            uint32_t(n.inputs.size()),
            uint32_t(n.outputs.size())};
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_socket_info(const char *name,
                                    ccl_socket_direction direction,
                                    uint32_t index,
                                    ccl_socket_desc *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid socket output size");
    std::lock_guard<std::mutex> lock(api_mutex);
    const ccl::SocketType *s;
    const auto status = socket(name, direction, index, s);
    if (status != CCL_SUCCESS)
      return status;
    *out = describe_socket(s, out->size);
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_socket_default_value(const char *name,
                                             ccl_socket_direction direction,
                                             uint32_t index,
                                             uint64_t element,
                                             ccl_default_value *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid default output size");
    std::lock_guard<std::mutex> lock(api_mutex);
    const ccl::SocketType *s;
    const auto status = socket(name, direction, index, s);
    if (status != CCL_SUCCESS)
      return status;
    if (!s->default_value || s->type == ccl::SocketType::CLOSURE)
      return fail(CCL_ERROR_NO_DEFAULT, "Socket has no default");
    if (element >= default_count(*s))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Default element out of range");
    auto value = default_value(*s, element);
    if (value.type == CCL_SOCKET_NODE && !value.node_is_null)
      return fail(CCL_ERROR_UNSUPPORTED, "Non-null native node default");
    value.size = out->size;
    *out = value;
    return CCL_SUCCESS;
  });
}
ccl_status CCL_CALL ccl_socket_enum_value(const char *name,
                                          ccl_socket_direction direction,
                                          uint32_t index,
                                          uint32_t enum_index,
                                          ccl_enum_value *out)
{
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid enum output size");
    std::lock_guard<std::mutex> lock(api_mutex);
    const ccl::SocketType *s;
    const auto status = socket(name, direction, index, s);
    if (status != CCL_SUCCESS)
      return status;
    std::vector<std::pair<ccl::ustring, int>> enums;
    if (s->enum_values)
      for (const auto &entry : *s->enum_values)
        enums.push_back(entry);
    std::sort(enums.begin(), enums.end(), [](const auto &a, const auto &b) {
      return std::strcmp(a.first.c_str(), b.first.c_str()) < 0;
    });
    if (enum_index >= enums.size())
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Enum index out of range");
    *out = {out->size, text(enums[enum_index].first), enums[enum_index].second};
    return CCL_SUCCESS;
  });
}

#include "scene_api.inc"

#include "shader_api.inc"

#include "geometry_api.inc"

#include "session_api.inc"

#include "deep_api.inc"
