/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <filesystem>
namespace ccl::deep {
/* A sibling private directory reserves a unique name on the destination
 * filesystem. The final name is replaced only after successful close. */
class AtomicOutput {
 public:
  explicit AtomicOutput(const std::filesystem::path &destination);
  ~AtomicOutput();
  AtomicOutput(const AtomicOutput &) = delete;
  AtomicOutput &operator=(const AtomicOutput &) = delete;
  const std::filesystem::path &temporary() const
  {
    return temporary_;
  }
  void publish();

 private:
  std::filesystem::path destination_, directory_, temporary_;
};
}  // namespace ccl::deep
