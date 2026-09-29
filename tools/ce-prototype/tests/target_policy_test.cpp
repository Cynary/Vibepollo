#include "../target_policy.hpp"

#include <cassert>

int main() {
  using namespace direct_capture;
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
