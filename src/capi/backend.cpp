/* SPDX-License-Identifier: Apache-2.0 */
#include "dispatch_table.h"
#include <cstring>

/* The only backend export. This is a private exact-build protocol, not public ABI. */
extern "C" __declspec(dllexport) const ccl_backend_table *CCL_CALL
cycles_c_private_get_table(uint32_t major, uint32_t minor, uint32_t bytes, const char *signature)
{
  static const ccl_backend_table table = {
#include "dispatch_table.inc"
  };
  if (major != 1 || minor != 6 || bytes != sizeof(table) || !signature ||
      std::strcmp(signature, CCL_TABLE_SIGNATURE) != 0)
    return nullptr;
  return &table;
}
