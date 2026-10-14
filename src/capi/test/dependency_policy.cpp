/* SPDX-License-Identifier: Apache-2.0 */
#include "../dependency_policy.h"
#include <cstdlib>
#include <iostream>
int main()
{
  std::string message;
  auto check = [&](const char *name, const char *actual, const char *missing, bool pass) {
    const auto result = loader::dependency_policy(name, "14.44.35211.0", actual, missing, message);
    if (result != (pass ? CCL_SUCCESS : CCL_ERROR_DEPENDENCY_MISMATCH) ||
        (!pass && (message.find(name) == std::string::npos ||
                   message.find("14.44.35211.0") == std::string::npos ||
                   message.find(actual) == std::string::npos ||
                   message.find(missing) == std::string::npos))) std::abort();
  };
  for (auto name : {"vcruntime140.dll", "VCRUNTIME140_1.dll", "msvcp140.dll",
                    "msvcp140_atomic_wait.dll", "concrt140.dll", "vcomp140.dll"}) {
    check(name, "14.51.36247.0", "", true);
    check(name, "14.44.35211.0", "", true);
    check(name, "14.40.33816.0", "", false);
    check(name, "15.0.0.0", "", false);
    check(name, "14.51.36247.0", "missing_function", false);
    check(name, "unavailable", "", false);
  }
  check("OpenImageIO.dll", "14.51.36247.0", "", false);
  check("OpenImageIO.dll", "14.44.35211.0", "", true);
  check("tbb12.dll", "14.51.36247.0", "", false);
  if (loader::microsoft_runtime("ucrtbase.dll")) std::abort();
  if (loader::dependency_policy("ucrtbase.dll", "", "10.0.26100.0", "missing_function", message) !=
      CCL_ERROR_DEPENDENCY_MISMATCH) std::abort();
  std::cout << "PASS runtime minimum/major, exact renderer versions and required symbols\n";
}
