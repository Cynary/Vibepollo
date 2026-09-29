#include "../auto_target_policy.hpp"
#include "../target_policy.hpp"

#include <cassert>

int main() {
  assert(direct_capture::covers_monitor(0, 0, 3840, 2160, 0, 0, 3840, 2160));
  assert(direct_capture::covers_monitor(-3840, 0, 0, 2160, -3840, 0, 0, 2160));
  assert(direct_capture::covers_monitor(1, 1, 3839, 2159, 0, 0, 3840, 2160));
  assert(!direct_capture::covers_monitor(0, 30, 3840, 2120, 0, 0, 3840, 2160));
  assert(!direct_capture::covers_monitor(0, 0, 7680, 2160, 0, 0, 3840, 2160));
  assert(!direct_capture::covers_monitor(0, 0, 1920, 1080, 0, 0, 3840, 2160));

  using namespace direct_capture;
  assert(conventional_game_root(L"e:\\steamapps\\common\\new game\\bin\\game.exe") == L"e:\\steamapps\\common\\new game");
  assert(conventional_game_root(L"c:\\xboxgames\\another game\\content\\game.exe") == L"c:\\xboxgames\\another game");
  assert(conventional_game_root(L"c:\\windows\\explorer.exe").empty());
  assert(under(L"e:\\games\\game\\bin\\game.exe", L"e:\\games\\game"));
  assert(!under(L"e:\\games\\game2\\game.exe", L"e:\\games\\game"));
  assert(excluded_application(L"epicgameslauncher.exe"));
  assert(excluded_application(L"upc.exe"));
  assert(excluded_application(L"steamwebhelper.exe"));
  assert(excluded_application(L"chrome.exe"));
  assert(!excluded_application(L"never-listed-before.exe"));
  assert(known_incompatible_game(L"cs2.exe"));
  assert(anti_cheat_marker(L"easyanticheat_eos.sys"));
  assert(anti_cheat_marker(L"battleye"));
  assert(anti_cheat_marker(L"vgk"));
  assert(!anti_cheat_marker(L"nvgcolor.dll"));
  assert(game_window_class(L"unitywndclass"));
  assert(!game_window_class(L"chrome_widgetwin_1"));
  assert(parse_paths(L"").empty());
  const auto paths = parse_paths(L" \"C:/Games/A.exe\" ; D:\\Games\\B.EXE ; c:\\games\\a.exe ");
  assert(paths.size() == 2 && paths[0] == L"c:\\games\\a.exe");
  for (const auto *text : {L"game.exe", L"C:game.exe", L"C:\\Games\\*.exe", L"C:\\Games\\game.dll", L"\\\\?\\C:\\game.exe", L"C:\\a.exe\nb.exe"}) {
    bool rejected = false;
    try {
      parse_paths(text);
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  bool oversized = false;
  try {
    parse_paths(std::wstring(4096, L'a'));
  } catch (...) {
    oversized = true;
  }
  assert(oversized);
  std::wstring many;
  for (int i = 0; i < 17; ++i) {
    many += L"C:\\Game" + std::to_wstring(i) + L".exe;";
  }
  bool tooMany = false;
  try {
    parse_paths(many);
  } catch (...) {
    tooMany = true;
  }
  assert(tooMany);
  assert(parse_paths(L"\\\\server\\games\\a.exe").size() == 1);
  assert(!stalled(0, 29999, 0, 0));
  assert(stalled(0, 30000, 0, 0));
  assert(!stalled(1, 4999, 0, 0));
  assert(stalled(1, 5000, 0, 0));
  assert(!stalled(2, 9000, 0, 8000));
}
