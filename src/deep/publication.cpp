/* SPDX-License-Identifier: Apache-2.0 */
#include "deep/publication.h"
#include <random>
#include <stdexcept>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
namespace ccl::deep {
AtomicOutput::AtomicOutput(const std::filesystem::path &destination) : destination_(destination)
{
  std::random_device random;
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto candidate = destination;
    candidate += ".partial-" + std::to_string(random()) + "-" + std::to_string(random());
    if (std::filesystem::create_directory(candidate)) {
      directory_ = candidate;
      temporary_ = directory_ / "output";
      return;
    }
  }
  throw std::runtime_error("Cannot reserve temporary deep output");
}
AtomicOutput::~AtomicOutput()
{
  std::error_code ignored;
  std::filesystem::remove(temporary_, ignored);
  std::filesystem::remove(directory_, ignored);
}
void AtomicOutput::publish()
{
#ifdef _WIN32
  if (!MoveFileExW(temporary_.c_str(),
                   destination_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::system_error(GetLastError(), std::system_category(), "Deep publication failed");
#else
  std::filesystem::rename(temporary_, destination_);
#endif
}
}  // namespace ccl::deep
