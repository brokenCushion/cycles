/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cycles_c.h"
#include <array>
#include <cctype>
#include <sstream>
#include <string>

namespace loader {
inline bool microsoft_runtime(std::string name)
{
  for (char &c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
  return name == "concrt140.dll" || name == "vcomp140.dll" ||
         (name.size() >= 4 && name.substr(name.size()-4) == ".dll" &&
          (name.rfind("vcruntime140", 0) == 0 || name.rfind("msvcp140", 0) == 0));
}

inline bool parse_version(const std::string &text, std::array<unsigned, 4> &v)
{
  std::istringstream input(text);
  for (size_t i = 0; i < v.size(); ++i) {
    if (!std::isdigit(input.peek()) || !(input >> v[i])) return false;
    if (i != 3 && input.get() != '.') return false;
  }
  return input.peek() == std::char_traits<char>::eof();
}

/* Pure policy shared by preflight and tests; missing imports always fail. */
inline ccl_status dependency_policy(const std::string &name,
                                    const std::string &expected,
                                    const std::string &actual,
                                    const std::string &missing,
                                    std::string &message)
{
  bool matches = expected.empty() || expected == actual;
  if (microsoft_runtime(name)) {
    std::array<unsigned, 4> minimum{}, loaded{};
    matches = parse_version(expected, minimum) && parse_version(actual, loaded) &&
              minimum[0] == 14 && loaded[0] == 14 && loaded >= minimum;
  }
  message.clear();
  if (matches && missing.empty()) return CCL_SUCCESS;
  message = name + ": expected " + (microsoft_runtime(name) ?
      "Microsoft runtime major 14, minimum version " : "version ") +
      (expected.empty() ? "no version resource (import ABI)" : expected) +
      ", loaded " + (actual.empty() ? "unavailable" : actual) +
      "; missing symbols: " + (missing.empty() ? "none" : missing);
  return CCL_ERROR_DEPENDENCY_MISMATCH;
}
} // namespace loader
