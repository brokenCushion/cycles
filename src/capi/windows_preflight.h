/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "dependency_policy.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <windows.h>

namespace loader {
#include "dependency_versions.inc"

inline std::string expected_version(const std::string &name)
{
  if (microsoft_runtime(name)) return microsoft_runtime_minimum;
  // UCRT is serviced with Windows and uses OS (10.x), not MSVC (14.x), versions.
  if (_stricmp(name.c_str(), "ucrtbase.dll") == 0 ||
      _strnicmp(name.c_str(), "api-ms-win-crt-", 15) == 0) return {};
  for (const auto &entry : build_versions)
    if (_stricmp(entry.name, name.c_str()) == 0)
      return entry.version;
  return {};
}
/* Read PE data without executing TLS callbacks, static initializers or DllMain. */
class Image {
  std::vector<char> data;
  const IMAGE_NT_HEADERS64 *nt;
  const IMAGE_SECTION_HEADER *sections;

 public:
  explicit Image(const std::filesystem::path &path)
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || uint64_t(file.tellg()) > UINT32_MAX)
      throw std::runtime_error("Cannot read backend dependency image");
    data.resize(size_t(file.tellg()));
    file.seekg(0);
    if (!file.read(data.data(), data.size()))
      throw std::runtime_error("Short PE read");
    auto dos = at<IMAGE_DOS_HEADER>(0);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
      throw std::runtime_error("Invalid DOS header");
    nt = at<IMAGE_NT_HEADERS64>(dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
      throw std::runtime_error("Unsupported PE header");
    sections = at<IMAGE_SECTION_HEADER>(uint64_t(dos->e_lfanew) + sizeof(*nt),
                                        nt->FileHeader.NumberOfSections);
  }
  template<class T> const T *at(uint64_t offset, uint64_t count = 1) const
  {
    if (offset > data.size() || count > (data.size() - offset) / sizeof(T))
      throw std::runtime_error("PE offset outside image");
    return reinterpret_cast<const T *>(data.data() + offset);
  }
  uint64_t offset(uint64_t rva, uint64_t bytes) const
  {
    if (rva < nt->OptionalHeader.SizeOfHeaders && bytes <= nt->OptionalHeader.SizeOfHeaders - rva)
      return rva;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
      const auto &s = sections[i];
      if (rva >= s.VirtualAddress && rva - s.VirtualAddress <= s.SizeOfRawData &&
          bytes <= s.SizeOfRawData - (rva - s.VirtualAddress))
        return uint64_t(s.PointerToRawData) + rva - s.VirtualAddress;
    }
    throw std::runtime_error("PE RVA outside file-backed section");
  }
  template<class T> const T *rva(uint64_t address) const
  {
    return at<T>(offset(address, sizeof(T)));
  }
  std::string string(uint64_t address) const
  {
    std::string value;
    for (size_t i = 0; i < 32768; ++i) {
      char c = *rva<char>(address + i);
      if (!c)
        return value;
      value += c;
    }
    throw std::runtime_error("Unterminated PE string");
  }
  template<class F> void imports(F visit) const
  {
    // No delayed imports are emitted by our backend build; reject rather than omit them.
    if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size)
      throw std::runtime_error("Delay import preflight is not implemented");
    const auto &d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!d.Size)
      return;
    for (uint64_t i = 0; i + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= d.Size;
         i += sizeof(IMAGE_IMPORT_DESCRIPTOR))
    {
      const auto *entry = rva<IMAGE_IMPORT_DESCRIPTOR>(uint64_t(d.VirtualAddress) + i);
      if (!entry->Name)
        return;
      if (!entry->OriginalFirstThunk)
        throw std::runtime_error("Missing PE import names");
      std::vector<std::string> names;
      std::vector<WORD> ordinals;
      for (uint64_t j = 0; j < data.size() / sizeof(uint64_t); ++j) {
        uint64_t thunk = *rva<uint64_t>(uint64_t(entry->OriginalFirstThunk) +
                                        j * sizeof(uint64_t));
        if (!thunk) {
          visit(string(entry->Name), names, ordinals);
          break;
        }
        if (IMAGE_SNAP_BY_ORDINAL64(thunk))
          ordinals.push_back(WORD(IMAGE_ORDINAL64(thunk)));
        else
          names.push_back(string(thunk + sizeof(WORD)));
        if (j + 1 == data.size() / sizeof(uint64_t))
          throw std::runtime_error("Unterminated import table");
      }
    }
    throw std::runtime_error("Unterminated import descriptors");
  }
};

inline std::string version(const std::filesystem::path &path)
{
  DWORD unused = 0, bytes = GetFileVersionInfoSizeW(path.c_str(), &unused);
  if (!bytes)
    return {};
  std::vector<char> data(bytes);
  if (!GetFileVersionInfoW(path.c_str(), 0, bytes, data.data()))
    throw std::runtime_error("Cannot read dependency version resource");
  VS_FIXEDFILEINFO *info = nullptr;
  UINT size = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &size) ||
      size < sizeof(*info) || info->dwSignature != 0xfeef04bd)
    throw std::runtime_error("Invalid dependency version resource");
  return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
         std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
         std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
         std::to_string(LOWORD(info->dwFileVersionLS));
}

inline std::filesystem::path module_path(HMODULE module)
{
  wchar_t path[32768];
  DWORD n = GetModuleFileNameW(module, path, 32768);
  if (!n || n == 32768)
    throw std::runtime_error("Cannot resolve module path");
  return path;
}

inline std::string preflight(const std::filesystem::path &backend,
                             const std::filesystem::path &dependencies)
{
  std::set<std::filesystem::path> seen;
  std::vector<std::filesystem::path> pending{backend};
  struct Check {
    std::string expected, actual;
    std::set<std::string> missing;
  };
  std::map<std::string, Check> checks;
  while (!pending.empty()) {
    auto path = pending.back();
    pending.pop_back();
    if (!seen.insert(path).second)
      continue;
    Image(path).imports([&](const std::string &name, const auto &names, const auto &ordinals) {
      auto expected_path = backend.parent_path() / name;
      if (!std::filesystem::is_regular_file(expected_path))
        expected_path = dependencies / name;
      bool bundled = std::filesystem::is_regular_file(expected_path);
      if (bundled)
        pending.push_back(expected_path);
      HMODULE loaded = GetModuleHandleA(name.c_str());
      if (!loaded)
        return;
      auto &check = checks[name];
      check.expected = expected_version(name);
      check.actual = check.expected.empty() ? std::string() : version(module_path(loaded));
      for (const auto &symbol : names)
        if (!GetProcAddress(loaded, symbol.c_str()))
          check.missing.insert(symbol);
      for (WORD ordinal : ordinals)
        if (!GetProcAddress(loaded, MAKEINTRESOURCEA(ordinal)))
          check.missing.insert("ordinal#" + std::to_string(ordinal));
    });
  }
  std::ostringstream errors;
  for (const auto &[name, check] : checks) {
    std::string missing, message;
    for (const auto &symbol : check.missing) missing += symbol + " ";
    if (dependency_policy(name, check.expected, check.actual, missing, message) != CCL_SUCCESS)
      errors << message << "\n";
  }
  return errors.str();
}
}  // namespace loader
