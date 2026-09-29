#pragma once
#include "auto_target.hpp"
#include "target_policy.hpp"

#include <set>

// Direct capture follows the foreground fullscreen window on the streamed monitor.
// Background games never qualify, regardless of installation path or window class.
class CeTargetSelector {
  std::vector<std::wstring> excluded_paths;
  HMONITOR monitor = nullptr;
  bool active = false;
  ULONGLONG next_scan = 0;
  direct_capture::automatic_games games;
  std::set<std::pair<DWORD, ULONGLONG>> attempted;

  HWND selected_window = nullptr;
  std::pair<DWORD, ULONGLONG> selected_identity {};

  bool focused_fullscreen(HWND window) const {
    if (!window || window != GetForegroundWindow() || !IsWindowVisible(window) || IsIconic(window) ||
        MonitorFromWindow(window, MONITOR_DEFAULTTONULL) != monitor) {
      return false;
    }
    // Use the client area: a maximized window's border must not qualify it as fullscreen.
    RECT client {};
    MONITORINFO info {sizeof(MONITORINFO)};
    POINT origin {};
    if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin) || !GetMonitorInfoW(monitor, &info)) {
      return false;
    }
    return direct_capture::covers_monitor(origin.x, origin.y, origin.x + client.right, origin.y + client.bottom,
                                         info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom);
  }

  static bool graphics_process(DWORD pid) {
    Handle snapshot {CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
    MODULEENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Module32FirstW(snapshot.v, &entry); ok; ok = Module32NextW(snapshot.v, &entry)) {
      auto name = direct_capture::normalized(entry.szModule);
      if (name == L"d3d11.dll" || name == L"d3d12.dll" || name == L"d3d9.dll" || name == L"opengl32.dll" || name == L"vulkan-1.dll") {
        return true;
      }
    }
    return false;
  }

public:
  explicit CeTargetSelector(bool enabled, HMONITOR capture_monitor, std::wstring /* legacy_extras */ = {}, std::wstring exclusions = {}):
      excluded_paths(direct_capture::parse_paths(exclusions)),
      monitor(capture_monitor),
      active(enabled) {}

  bool still_selected(DWORD pid) const {
    DWORD current_pid = 0;
    GetWindowThreadProcessId(selected_window, &current_pid);
    return current_pid == pid && focused_fullscreen(selected_window);
  }

  void finished(bool failed) {
    if (failed) {
      attempted.insert(selected_identity);
    }
    selected_window = nullptr;
    next_scan = 0;
  }

  DWORD next() {
    if (!active || GetTickCount64() < next_scan) {
      return 0;
    }
    next_scan = GetTickCount64() + 250;
    // One candidate only. Never walk the Z-order looking behind the foreground app.
    for (auto window : {GetForegroundWindow()}) {
      if (!focused_fullscreen(window)) {
        continue;
      }
      RECT rect {};
      if (!GetClientRect(window, &rect) || rect.right < 320 || rect.bottom < 200) {
        continue;
      }
      DWORD pid = 0;
      GetWindowThreadProcessId(window, &pid);
      if (!pid || pid == GetCurrentProcessId()) {
        continue;
      }
      Handle process {OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
      if (!process.v) {
        continue;
      }
      wchar_t actual[32768];
      DWORD length = std::size(actual);
      FILETIME creation, exit, kernel, user;
      if (!QueryFullProcessImageNameW(process.v, 0, actual, &length) || !GetProcessTimes(process.v, &creation, &exit, &kernel, &user)) {
        continue;
      }
      const auto identity = std::make_pair(pid, (ULONGLONG(creation.dwHighDateTime) << 32) | creation.dwLowDateTime);
      if (attempted.count(identity)) {
        continue;
      }
      const auto path = direct_capture::normalized(std::wstring(actual, length));
      const auto name = std::filesystem::path(path).filename().wstring();
      if (direct_capture::excluded_application(name)) {
        continue;
      }
      auto root = games.game_root(path);
      wchar_t windows_dir[MAX_PATH] {};
      GetWindowsDirectoryW(windows_dir, std::size(windows_dir));
      if (direct_capture::under(path, direct_capture::normalized(windows_dir))) {
        continue;
      }
      if (root.empty()) {
        auto directory = std::filesystem::path(path).parent_path();
        // Unreal stand-alone games often put the renderer two levels below
        // the install directory. Include that directory in protection checks.
        if (directory.filename() == L"win64" || directory.filename() == L"win32") {
          directory = directory.parent_path();
        }
        if (directory.filename() == L"binaries") {
          directory = directory.parent_path();
        }
        root = directory.wstring();
      }
      auto reason = std::wstring {};
      if (std::find(excluded_paths.begin(), excluded_paths.end(), path) != excluded_paths.end()) {
        reason = L"excluded in settings";
      } else if (direct_capture::known_incompatible_game(name)) {
        reason = L"known incompatible game";
      } else {
        reason = direct_capture::automatic_games::active_protection();
      }
      if (reason.empty()) {
        reason = direct_capture::automatic_games::directory_protection(std::filesystem::path(path).parent_path(), root);
      }
      if (!reason.empty()) {
        attempted.insert(identity);
        BOOST_LOG(warning) << "Direct capture skipped for PID " << pid << ": " << std::filesystem::path(reason).string() << "; using WGC";
        continue;
      }
      if (!graphics_process(pid)) {
        continue;
      }
      selected_window = window;
      selected_identity = identity;
      BOOST_LOG(info) << "Automatically selected direct capture game: " << std::filesystem::path(path).string() << ", PID " << pid;
      return pid;
    }
    return 0;
  }
};
