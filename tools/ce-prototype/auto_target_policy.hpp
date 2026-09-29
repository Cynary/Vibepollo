#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace direct_capture {
  inline std::wstring normalized(std::wstring value) {
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
      return std::towlower(c);
    });
    return value;
  }

  inline bool under(std::wstring_view path, std::wstring_view root) {
    return !root.empty() && path.size() > root.size() && path.substr(0, root.size()) == root &&
           (root.back() == L'\\' || path[root.size()] == L'\\');
  }

  inline bool anti_cheat_marker(std::wstring_view name) {
    if (name == L"vgk" || name == L"vgc" || name == L"vgk.sys" || name == L"vgc.exe") {
      return true;
    }
    for (auto token : {L"easyanticheat", L"easyanticheat_eos", L"battleye", L"beservice", L"bedaisy", L"faceit", L"equ8", L"xigncode", L"gameguard", L"nprotect", L"eaanticheat", L"eajavelin", L"ace-base"}) {
      if (name.find(token) != std::wstring_view::npos) {
        return true;
      }
    }
    return false;
  }

  inline bool excluded_application(std::wstring_view name) {
    for (auto item : {L"steam.exe", L"steamwebhelper.exe", L"epicgameslauncher.exe", L"epicwebhelper.exe", L"ubisoftconnect.exe", L"upc.exe", L"uplaywebcore.exe", L"eadesktop.exe", L"ealauncher.exe", L"origin.exe", L"battle.net.exe", L"agent.exe", L"galaxyclient.exe", L"galaxyclient helper.exe", L"playnite.desktopapp.exe", L"playnite.fullscreenapp.exe", L"launcher.exe", L"launch.exe", L"setup.exe", L"uninstall.exe", L"unins000.exe", L"crashreportclient.exe", L"crashpad_handler.exe", L"unrealcefsubprocess.exe", L"chrome.exe", L"msedge.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"discord.exe", L"explorer.exe", L"applicationframehost.exe", L"dwm.exe", L"sunshine.exe", L"sunshine_wgc_capture.exe", L"moondeckstream.exe", L"moondeckbuddy.exe", L"gamebar.exe", L"gamebarftserver.exe", L"nvidia overlay.exe", L"vrmonitor.exe", L"vrcompositor.exe"}) {
      if (name == item) {
        return true;
      }
    }
    return name.find(L"launcher") != std::wstring_view::npos || name.find(L"crash") != std::wstring_view::npos;
  }

  inline bool known_incompatible_game(std::wstring_view name) {
    // These restrictions apply even when no separately named anti-cheat service is visible.
    return name == L"cs2.exe" || name == L"destiny2.exe" || name == L"valorant-win64-shipping.exe";
  }

  inline bool game_window_class(std::wstring_view name) {
    return name == L"unitywndclass" || name == L"unrealwindow" || name == L"sdl_app" || name == L"valve001";
  }

  inline std::wstring conventional_game_root(const std::wstring &path) {
    for (auto marker : {L"\\steamapps\\common\\", L"\\xboxgames\\"}) {
      const auto pos = path.find(marker);
      if (pos == std::wstring::npos) {
        continue;
      }
      const auto begin = pos + std::wstring_view(marker).size();
      const auto end = path.find(L'\\', begin);
      if (end != std::wstring::npos && end > begin) {
        return path.substr(0, end);
      }
    }
    return {};
  }
}  // namespace direct_capture
