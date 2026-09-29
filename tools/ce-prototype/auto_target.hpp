#pragma once
#include "auto_target_policy.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <winsvc.h>

namespace direct_capture {
  // Passive checks only: no remote threads or target memory reads here. A missing
  // marker is not a guarantee of anti-cheat compatibility.
  class automatic_games {
    std::vector<std::wstring> roots;
    ULONGLONG refresh_at = 0;

    static std::wstring reg_string(HKEY key, const wchar_t *name) {
      DWORD bytes = 0;
      if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes > 65536) {
        return {};
      }
      std::wstring value(bytes / sizeof(wchar_t), L'\0');
      if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
      }
      while (!value.empty() && !value.back()) {
        value.pop_back();
      }
      return value;
    }

    void discover_roots() {
      if (GetTickCount64() < refresh_at) {
        return;
      }
      refresh_at = GetTickCount64() + 30000;
      roots.clear();
      wchar_t program_data[32768] {};
      auto n = GetEnvironmentVariableW(L"PROGRAMDATA", program_data, std::size(program_data));
      if (n && n < std::size(program_data)) {
        std::error_code ec;
        const auto dir = std::filesystem::path(program_data) / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
          if (it->path().extension() != L".item") {
            continue;
          }
          try {
            std::ifstream file(it->path());
            auto json = nlohmann::json::parse(file);
            auto install = json.value("InstallLocation", std::string {});
            if (!install.empty()) {
              roots.push_back(normalized(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(install.data()), install.size())).wstring()));
            }
          } catch (const std::exception &) { /* Incomplete launcher metadata is not a target. */
          }
        }
      }
      // Ubisoft records installed games here, including custom library drives.
      for (auto view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        HKEY parent = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Ubisoft\\Launcher\\Installs", 0, KEY_READ | view, &parent) != ERROR_SUCCESS) {
          continue;
        }
        for (DWORD index = 0;; ++index) {
          wchar_t name[256];
          DWORD length = std::size(name);
          if (RegEnumKeyExW(parent, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
          }
          HKEY game = nullptr;
          if (RegOpenKeyExW(parent, name, 0, KEY_READ | view, &game) == ERROR_SUCCESS) {
            auto path = reg_string(game, L"InstallDir");
            if (!path.empty()) {
              roots.push_back(normalized(path));
            }
            RegCloseKey(game);
          }
        }
        RegCloseKey(parent);
      }
      for (auto &root : roots) {
        while (root.size() > 3 && root.back() == L'\\') {
          root.pop_back();
        }
      }
    }

  public:
    std::wstring game_root(const std::wstring &path) {
      if (auto root = conventional_game_root(path); !root.empty()) {
        return root;
      }
      discover_roots();
      for (const auto &root : roots) {
        if (!root.empty() && under(path, root)) {
          return root;
        }
      }
      return {};
    }

    static std::wstring active_protection() {
      SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
      if (!manager) {
        return L"cannot inspect active services";
      }
      DWORD bytes = 0, count = 0;
      EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_ACTIVE, nullptr, 0, &bytes, &count, nullptr, nullptr);
      if (GetLastError() != ERROR_MORE_DATA) {
        CloseServiceHandle(manager);
        return L"cannot enumerate active services";
      }
      std::vector<BYTE> data(bytes);
      if (!EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_ACTIVE, data.data(), bytes, &bytes, &count, nullptr, nullptr)) {
        CloseServiceHandle(manager);
        return L"cannot enumerate active services";
      }
      CloseServiceHandle(manager);
      auto entries = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(data.data());
      for (DWORD i = 0; i < count; ++i) {
        auto name = normalized(entries[i].lpServiceName);
        if (anti_cheat_marker(name)) {
          return name;
        }
      }
      return {};
    }

    static std::wstring directory_protection(std::filesystem::path directory, const std::wstring &root) {
      // Inspect each directory between the executable and its install root, not
      // arbitrary siblings elsewhere in the game library or the whole drive.
      for (int depth = 0; depth < 12 && !directory.empty(); ++depth) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
          auto name = normalized(it->path().filename().wstring());
          if (anti_cheat_marker(name)) {
            return name;
          }
        }
        if (ec) {
          return L"cannot inspect game directory";
        }
        if (normalized(directory.wstring()) == root) {
          break;
        }
        const auto parent = directory.parent_path();
        if (parent == directory || root.empty()) {
          break;
        }
        directory = parent;
      }
      return {};
    }
  };
}  // namespace direct_capture
