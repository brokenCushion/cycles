/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sstream>
#include <set>
#include <string>

namespace ccl::deep {
/* OSO metadata, captured from the very bytes loaded into OSL. Public group
 * queries do not expose trace operations or bound the GPU closure arena.
 * Count every possible allocation; reject closure-building loops rather than
 * assuming their iteration count. Strings/comments are never instructions. */
struct OSLFeatures {
  std::string unsupported;
  size_t components = 0, muls = 0, adds = 0;
  bool loop = false;
  void merge(const OSLFeatures &other)
  {
    if (!other.unsupported.empty()) unsupported = other.unsupported;
    components += other.components;
    muls += other.muls;
    adds += other.adds;
    loop |= other.loop;
  }
};
inline OSLFeatures osl_features(const std::string &bytecode)
{
  OSLFeatures result;
  std::set<std::string> closures;
  std::istringstream lines(bytecode);
  bool code = false;
  for (std::string line; std::getline(lines, line);) {
    std::istringstream tokens(line);
    std::string op, type, name;
    tokens >> op;
    if (op == "code") { code = true; continue; }
    if (!code) {
      tokens >> type;
      if (type == "closure") {
        tokens >> type >> name; /* closure color NAME */
        closures.insert(name);
      }
      continue;
    }
    if (op == "trace" || op == "getmessage" || op == "setmessage" || op == "pointcloud_write")
      result.unsupported = op;
    /* functioncall marks an INLINED region (e.g. stdosl clamp); it does not
     * repeat it. Repetition is represented by the loop operations below. */
    if (op == "for" || op == "while" || op == "dowhile") result.loop = true;
    tokens >> name;
    if (op == "closure") ++result.components;
    if (closures.count(name)) {
      if (op == "mul") ++result.muls;
      if (op == "add") ++result.adds;
      if (op != "closure" && op != "mul" && op != "add" && op != "assign" &&
          op != "aassign" && op != "aref")
        result.unsupported = "unbounded closure operation " + op;
    }
  }
  return result;
}
}
