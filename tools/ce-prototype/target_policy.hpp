#pragma once
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace direct_capture {
  inline std::vector<std::wstring> parse_paths(const std::wstring &value) {
    if (value.size() >= 4096) {
      throw std::runtime_error("game executable list exceeds 4095 characters");
    }
    std::vector<std::wstring> paths;
    size_t start = 0;
    while (start < value.size()) {
      auto end = value.find(L';', start);
      auto path = value.substr(start, end == std::wstring::npos ? end : end - start);
      const auto first = path.find_first_not_of(L" \t\r\n");
      if (first != std::wstring::npos) {
        path = path.substr(first, path.find_last_not_of(L" \t\r\n") - first + 1);
        if (path.size() > 1 && path.front() == L'"' && path.back() == L'"') {
          path = path.substr(1, path.size() - 2);
        }
        const bool drive = path.size() > 3 && std::iswalpha(path[0]) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
        const bool unc = path.size() > 4 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'?' && path[2] != L'.';
        if ((!drive && !unc) || path.find_first_of(L"*?\"\r\n") != std::wstring::npos) {
          throw std::runtime_error("use absolute executable paths without wildcards");
        }
        std::replace(path.begin(), path.end(), L'/', L'\\');
        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) {
          return std::towlower(c);
        });
        if (path.size() < 4 || path.substr(path.size() - 4) != L".exe") {
          throw std::runtime_error("game paths must end in .exe");
        }
        if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
          paths.push_back(path);
        }
        if (paths.size() > 16) {
          throw std::runtime_error("at most 16 game executables are supported");
        }
      }
      if (end == std::wstring::npos) {
        break;
      }
      start = end + 1;
    }
    return paths;
  }

  inline bool stalled(unsigned count, unsigned long long now, unsigned long long started, unsigned long long last_frame) {
    return count ? now - last_frame >= 5000 : now - started >= 30000;
  }
}  // namespace direct_capture
