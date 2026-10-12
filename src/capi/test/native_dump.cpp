/* SPDX-License-Identifier: Apache-2.0 */
/* Independent native dump: no wrapper implementation is linked or called. */
#include "../cycles_c.h"
#include "graph/node_type.h"
#include "util/transform.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static int api_type(ccl::SocketType::Type t)
{
  const char *name = ccl::SocketType::type_name(t).c_str();
  if (std::strcmp(name, "undefined") == 0)
    return CCL_SOCKET_UNDEFINED;
  if (std::strcmp(name, "boolean") == 0)
    return CCL_SOCKET_BOOLEAN;
  if (std::strcmp(name, "float") == 0)
    return CCL_SOCKET_FLOAT;
  if (std::strcmp(name, "int") == 0)
    return CCL_SOCKET_INT;
  if (std::strcmp(name, "uint") == 0)
    return CCL_SOCKET_UINT;
  if (std::strcmp(name, "uint64") == 0)
    return CCL_SOCKET_UINT64;
  if (std::strcmp(name, "color") == 0)
    return CCL_SOCKET_COLOR;
  if (std::strcmp(name, "vector") == 0)
    return CCL_SOCKET_VECTOR;
  if (std::strcmp(name, "point") == 0)
    return CCL_SOCKET_POINT;
  if (std::strcmp(name, "normal") == 0)
    return CCL_SOCKET_NORMAL;
  if (std::strcmp(name, "point2") == 0)
    return CCL_SOCKET_POINT2;
  if (std::strcmp(name, "closure") == 0)
    return CCL_SOCKET_CLOSURE;
  if (std::strcmp(name, "string") == 0)
    return CCL_SOCKET_STRING;
  if (std::strcmp(name, "enum") == 0)
    return CCL_SOCKET_ENUM;
  if (std::strcmp(name, "transform") == 0)
    return CCL_SOCKET_TRANSFORM;
  if (std::strcmp(name, "node") == 0)
    return CCL_SOCKET_NODE;
  if (std::strcmp(name, "array_boolean") == 0)
    return CCL_SOCKET_BOOLEAN_ARRAY;
  if (std::strcmp(name, "array_float") == 0)
    return CCL_SOCKET_FLOAT_ARRAY;
  if (std::strcmp(name, "array_int") == 0)
    return CCL_SOCKET_INT_ARRAY;
  if (std::strcmp(name, "array_color") == 0)
    return CCL_SOCKET_COLOR_ARRAY;
  if (std::strcmp(name, "array_vector") == 0)
    return CCL_SOCKET_VECTOR_ARRAY;
  if (std::strcmp(name, "array_point") == 0)
    return CCL_SOCKET_POINT_ARRAY;
  if (std::strcmp(name, "array_normal") == 0)
    return CCL_SOCKET_NORMAL_ARRAY;
  if (std::strcmp(name, "array_point2") == 0)
    return CCL_SOCKET_POINT2_ARRAY;
  if (std::strcmp(name, "array_string") == 0)
    return CCL_SOCKET_STRING_ARRAY;
  if (std::strcmp(name, "array_transform") == 0)
    return CCL_SOCKET_TRANSFORM_ARRAY;
  if (std::strcmp(name, "array_node") == 0)
    return CCL_SOCKET_NODE_ARRAY;
  throw std::runtime_error("Unknown native type name");
}
static void emit_default(ccl::SocketType::Type type, const void *data)
{
  int32_t signed_value = 0;
  uint64_t unsigned_value = 0;
  int node_null = 0;
  float floats[12] = {};
  const char *text = "";
  switch (type) {
    case ccl::SocketType::BOOLEAN:
      signed_value = *static_cast<const bool *>(data);
      break;
    case ccl::SocketType::INT:
    case ccl::SocketType::ENUM:
      signed_value = *static_cast<const int *>(data);
      break;
    case ccl::SocketType::UINT:
      unsigned_value = *static_cast<const unsigned int *>(data);
      break;
    case ccl::SocketType::UINT64:
      unsigned_value = *static_cast<const uint64_t *>(data);
      break;
    case ccl::SocketType::FLOAT:
      std::memcpy(floats, data, sizeof(float));
      break;
    case ccl::SocketType::POINT2:
      std::memcpy(floats, data, 2 * sizeof(float));
      break;
    case ccl::SocketType::VECTOR:
    case ccl::SocketType::COLOR:
    case ccl::SocketType::NORMAL:
    case ccl::SocketType::POINT:
      std::memcpy(floats, data, 3 * sizeof(float));
      break;
    case ccl::SocketType::TRANSFORM:
      static_assert(sizeof(ccl::Transform) == 12 * sizeof(float));
      std::memcpy(floats, data, sizeof(ccl::Transform));
      break;
    case ccl::SocketType::STRING:
      text = static_cast<const ccl::ustring *>(data)->empty() ?
                 "" :
                 static_cast<const ccl::ustring *>(data)->c_str();
      break;
    case ccl::SocketType::NODE:
      node_null = *static_cast<ccl::Node *const *>(data) == nullptr;
      break;
    default:
      throw std::runtime_error("Unexpected scalar default");
  }
  std::printf("DEFAULT %d %d %llu %d %s",
              api_type(type),
              signed_value,
              (unsigned long long)unsigned_value,
              node_null,
              text);
  for (float f : floats)
    std::printf(" %a", double(f));
  std::puts("");
}
static uint64_t defaults(const ccl::SocketType &s, bool emit)
{
  if (!s.default_value || s.type == ccl::SocketType::CLOSURE)
    return 0;
  switch (s.type) {
    case ccl::SocketType::BOOLEAN_ARRAY: {
      const auto &values = *static_cast<const ccl::array<bool> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::BOOLEAN, &value);
      return values.size();
    }
    case ccl::SocketType::FLOAT_ARRAY: {
      const auto &values = *static_cast<const ccl::array<float> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::FLOAT, &value);
      return values.size();
    }
    case ccl::SocketType::INT_ARRAY: {
      const auto &values = *static_cast<const ccl::array<int> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::INT, &value);
      return values.size();
    }
    case ccl::SocketType::COLOR_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::COLOR, &value);
      return values.size();
    }
    case ccl::SocketType::VECTOR_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::VECTOR, &value);
      return values.size();
    }
    case ccl::SocketType::POINT_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::POINT, &value);
      return values.size();
    }
    case ccl::SocketType::NORMAL_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::packed_float3> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::NORMAL, &value);
      return values.size();
    }
    case ccl::SocketType::POINT2_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::float2> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::POINT2, &value);
      return values.size();
    }
    case ccl::SocketType::STRING_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::ustring> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::STRING, &value);
      return values.size();
    }
    case ccl::SocketType::TRANSFORM_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::Transform> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::TRANSFORM, &value);
      return values.size();
    }
    case ccl::SocketType::NODE_ARRAY: {
      const auto &values = *static_cast<const ccl::array<ccl::Node *> *>(s.default_value);
      if (emit)
        for (const auto &value : values)
          emit_default(ccl::SocketType::NODE, &value);
      return values.size();
    }
    default:
      if (emit)
        emit_default(s.type, s.default_value);
      return 1;
  }
}
int main()
{
  auto names = ccl::NodeType::type_names();
  std::sort(names.begin(), names.end(), [](auto a, auto b) {
    return std::strcmp(a.c_str(), b.c_str()) < 0;
  });
  for (auto name : names) {
    const auto &n = *ccl::NodeType::find(name);
    std::printf("NODE %s %s %d %d %u %u\n",
                n.name.c_str(),
                n.base ? n.base->name.c_str() : "",
                n.type == ccl::NodeType::SHADER,
                !n.create,
                unsigned(n.inputs.size()),
                unsigned(n.outputs.size()));
    for (int direction = 0; direction < 2; ++direction) {
      for (const auto &s : direction ? n.outputs : n.inputs) {
        std::vector<std::pair<std::string, int>> enums;
        if (s.enum_values)
          for (const auto &entry : *s.enum_values)
            enums.emplace_back(entry.first.c_str(), entry.second);
        std::sort(enums.begin(), enums.end());
        /* Assert the flag mapping explicitly; never silently accept a new native flag. */
        static_assert(ccl::SocketType::LINKABLE == CCL_SOCKET_LINKABLE);
        static_assert(ccl::SocketType::ANIMATABLE == CCL_SOCKET_ANIMATABLE);
        static_assert(ccl::SocketType::SVM_INTERNAL == CCL_SOCKET_SVM_INTERNAL);
        static_assert(ccl::SocketType::OSL_INTERNAL == CCL_SOCKET_OSL_INTERNAL);
        static_assert(ccl::SocketType::LINK_TEXTURE_GENERATED ==
                      CCL_SOCKET_LINK_TEXTURE_GENERATED);
        static_assert(ccl::SocketType::LINK_TEXTURE_NORMAL == CCL_SOCKET_LINK_TEXTURE_NORMAL);
        static_assert(ccl::SocketType::LINK_TEXTURE_UV == CCL_SOCKET_LINK_TEXTURE_UV);
        static_assert(ccl::SocketType::LINK_TEXTURE_INCOMING == CCL_SOCKET_LINK_TEXTURE_INCOMING);
        static_assert(ccl::SocketType::LINK_INCOMING == CCL_SOCKET_LINK_INCOMING);
        static_assert(ccl::SocketType::LINK_NORMAL == CCL_SOCKET_LINK_NORMAL);
        static_assert(ccl::SocketType::LINK_POSITION == CCL_SOCKET_LINK_POSITION);
        static_assert(ccl::SocketType::LINK_TANGENT == CCL_SOCKET_LINK_TANGENT);
        static_assert(ccl::SocketType::LINK_OSL_INITIALIZER == CCL_SOCKET_LINK_OSL_INITIALIZER);
        std::printf("SOCKET %d %s | %s | %d %u %s %d %llu %u\n",
                    direction,
                    s.name.c_str(),
                    s.ui_name.empty() ? "" : s.ui_name.c_str(),
                    api_type(s.type),
                    unsigned(s.flags),
                    s.node_type ? s.node_type->name.c_str() : "",
                    s.default_value && s.type != ccl::SocketType::CLOSURE,
                    (unsigned long long)defaults(s, false),
                    unsigned(enums.size()));
        defaults(s, true);
        for (const auto &entry : enums)
          std::printf("ENUM %s %d\n", entry.first.c_str(), entry.second);
      }
    }
  }
}
