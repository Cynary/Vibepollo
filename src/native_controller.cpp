#include "native_controller.h"

#include "logging.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <moonlight-common-c/src/NativeController.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
  #include "platform/windows/native_controller_driver.h"

  #include <setupapi.h>
#endif
namespace native_controller {
  bool enabled() {
#ifdef _WIN32
    const char *opt = std::getenv("MOONMACHINE_NATIVE_CONTROLLER");
    return opt && std::strcmp(opt, "1") == 0;
#else
    return false;
#endif
  }
#ifdef _WIN32
  namespace {
    std::mutex owner_mutex;
    bool owned = false;

    HANDLE open_driver() {
      auto set = SetupDiGetClassDevsW(&steam_native::interface_id, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (set == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
      }
      SP_DEVICE_INTERFACE_DATA data {sizeof(data)};
      HANDLE result = INVALID_HANDLE_VALUE;
      if (SetupDiEnumDeviceInterfaces(set, nullptr, &steam_native::interface_id, 0, &data)) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &size, nullptr);
        if (size >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) && size < 65536) {
          auto memory = std::make_unique<std::uint8_t[]>(size);
          auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(memory.get());
          detail->cbSize = sizeof(*detail);
          if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, size, nullptr, nullptr)) {
            result = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
          }
        }
      }
      SetupDiDestroyDeviceInfoList(set);
      return result;
    }

    class bridge final: public session {
      std::atomic_uint64_t input_reports {0}, feature_requests {0}, feature_replies {0}, failed_inputs {0};
      HANDLE handle;
      feedback send;
      std::atomic_bool stopping {false};
      std::thread worker;
      std::mutex pending_mutex;

      struct request {
        steam_native::operation op;
        std::chrono::steady_clock::time_point deadline;
      };

      std::unordered_map<std::uint64_t, request> pending;

      bool call(DWORD code, void *in, DWORD n, void *out, DWORD m) {
        DWORD got = 0;
        return DeviceIoControl(handle, code, in, n, out, m, &got, nullptr) != 0;
      }

    public:
      bridge(HANDLE h, feedback f):
          handle(h),
          send(std::move(f)) {}

      bool start() {
        if (!call(steam_native::start, nullptr, 0, nullptr, 0)) {
          return false;
        }
        worker = std::thread([this] {
          while (!stopping) {
            steam_native::packet p {};
            if (!call(steam_native::poll, nullptr, 0, &p, sizeof(p))) {
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
              continue;
            }
            message out {};
            out[0] = 1;
            out[1] = LI_NATIVE_REQUEST;
            out[3] = static_cast<std::uint8_t>(p.op);
            out[4] = static_cast<std::uint8_t>(p.size);
            LiNativeSetRequestId(out.data(), p.id);
            if (p.size <= 64) {
              std::memcpy(out.data() + 16, p.data, p.size);
            }
            bool allowed = p.size <= 64 && LiNativeValidate(out.data(), out.size(), 1) != 0;
            {
              std::lock_guard lock(pending_mutex);
              auto now = std::chrono::steady_clock::now();
              for (auto it = pending.begin(); it != pending.end();) {
                if (it->second.deadline < now) {
                  it = pending.erase(it);
                } else {
                  ++it;
                }
              }
              if (allowed && pending.size() < 32) {
                pending.emplace(p.id, request {p.op, now + std::chrono::seconds(2)});
              } else {
                allowed = false;
              }
            }
            if (allowed) {
              feature_requests++;
              send(out);
            } else {
              steam_native::response r {};
              r.id = p.id;
              r.status = -1;
              call(steam_native::reply, &r, sizeof(r), nullptr, 0);
            }
          }
        });
        BOOST_LOG(info) << "Native Steam Controller attached to stream";
        return true;
      }

      void receive(const message &m) override {
        if (!LiNativeValidate(m.data(), m.size(), 0)) {
          return;
        }
        if (m[1] == LI_NATIVE_INPUT) {
          steam_native::packet p {};
          p.size = m[4];
          std::memcpy(p.data, m.data() + 16, p.size);
          if (call(steam_native::input, &p, sizeof(p), nullptr, 0)) {
            input_reports++;
          } else {
            failed_inputs++;
          }
        } else if (m[1] == LI_NATIVE_REPLY) {
          auto id = LiNativeRequestId(m.data());
          std::lock_guard lock(pending_mutex);
          auto it = pending.find(id);
          if (it == pending.end()) {
            return;
          }
          if (it->second.deadline < std::chrono::steady_clock::now()) {
            pending.erase(it);
            return;
          }
          if (!m[2] && m[4] != (it->second.op == steam_native::operation::get_feature ? 64 : 0)) {
            return;
          }
          steam_native::response r {};
          r.id = id;
          r.status = m[2] ? -1 : 0;
          r.size = m[4];
          std::memcpy(r.data, m.data() + 16, r.size);
          call(steam_native::reply, &r, sizeof(r), nullptr, 0);
          feature_replies++;
          pending.erase(it);
        }
      }

      ~bridge() override {
        stopping = true;
        if (worker.joinable()) {
          worker.join();
        }
        call(steam_native::stop, nullptr, 0, nullptr, 0);
        CloseHandle(handle);
        BOOST_LOG(info) << "Native Steam Controller detached: reports=" << input_reports << " rejected=" << failed_inputs << " requests=" << feature_requests << " replies=" << feature_replies;
        std::lock_guard lock(owner_mutex);
        owned = false;
      }
    };
  }  // namespace
#endif
  std::unique_ptr<session> attach(feedback send) {
#ifdef _WIN32
    if (!enabled()) {
      return {};
    }
    {
      std::lock_guard lock(owner_mutex);
      if (owned) {
        return {};
      }
      owned = true;
    }
    HANDLE handle = open_driver();
    if (handle == INVALID_HANDLE_VALUE) {
      std::lock_guard lock(owner_mutex);
      owned = false;
      return {};
    }
    auto result = std::make_unique<bridge>(handle, std::move(send));
    if (!result->start()) {
      return {};
    }
    return result;
#else
    (void) send;
    return {};
#endif
  }
}  // namespace native_controller
