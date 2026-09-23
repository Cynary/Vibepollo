// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <windows.h>
#include <winioctl.h>
namespace steam_native {
inline constexpr GUID interface_id{
    0xa41e06ba,
    0xc6b7,
    0x48e1,
    {0xbc, 0x94, 0x0f, 0x90, 0x4d, 0x63, 0x0b, 0x4a}};
inline constexpr wchar_t root_id[] = L"ROOT\\MOONMACHINESTEAMHID";
constexpr DWORD ctl(unsigned n) {
  return CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900 + n, METHOD_BUFFERED,
                  FILE_READ_DATA | FILE_WRITE_DATA);
}
inline constexpr DWORD start = ctl(0), stop = ctl(1), input = ctl(2),
                       poll = ctl(3), reply = ctl(4);
enum class operation : std::uint32_t {
  get_feature = 1,
  set_feature = 2,
  write = 3,
  blocked_feature = 4
};
// Never marshal Windows handles. IDs are monotonic across device lifetimes.
struct packet {
  std::uint64_t id;
  operation op;
  std::uint32_t size;
  std::uint8_t data[64];
};
struct response {
  std::uint64_t id;
  std::int32_t status;
  std::uint32_t size;
  std::uint8_t data[64];
};
static_assert(sizeof(packet) == 80 && sizeof(response) == 80);
constexpr unsigned input_size(unsigned id) {
  switch (id) {
  case 0x42:
    return 54;
  case 0x43:
    return 15;
  case 0x44:
    return 6;
  case 0x45:
    return 46;
  case 0x79:
    return 2;
  case 0x7b:
    return 13;
  default:
    return 0;
  }
}
constexpr bool safe_feature(unsigned id, unsigned cmd, unsigned length) {
  // Steam's CGetTritonDonglePairingBondWorkItem sends report 2/A3/0.
  if (id == 2)
    return ((cmd == 0xa3 || cmd == 0xb4 || cmd == 0x83) && length == 0) || (cmd == 0xf3 && length <= 1);
  if (id != 1 || length > 61)
    return false;
  switch (cmd) {
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x87:
  case 0x89:
  case 0x8a:
  case 0x8b:
  case 0x8c:
  case 0x8f:
  case 0xa1:
  case 0xaa:
  case 0xab:
  case 0xae:
  case 0xba:
  case 0xc4:
  case 0xed: // Read named setting (including calibration data).
  case 0xf2: // Read system information.
  case 0xc1: // Runtime audio mapping.
  case 0xe2: // Runtime trackpad side.
    return true;
  default:
    return false;
  }
}
constexpr unsigned output_size(unsigned id) {
  switch (id) {
  case 0x80:
  case 0x83:
    return 10;
  case 0x81:
    return 8;
  case 0x82:
  case 0x85:
    return 4;
  case 0x84:
    return 9;
  default:
    return 0;
  }
}
} // namespace steam_native
