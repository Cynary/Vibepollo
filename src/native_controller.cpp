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
      HANDLE poll_timer = nullptr;
      feedback send;
      std::atomic_bool stopping {false};
      std::thread worker;
      std::mutex pending_mutex;

      struct request {
        steam_native::operation op;
        std::chrono::steady_clock::time_point deadline;
        bool completed_on_enqueue;
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
        // Sleep(1) may wait an entire 15.6 ms Windows timer tick. Use a
        // high-resolution local wait without changing system timer resolution.
        poll_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
        if (!poll_timer) {
          BOOST_LOG(error) << "Native controller high-resolution wait creation failed: " << GetLastError();
          return false;
        }
        if (!call(steam_native::start, nullptr, 0, nullptr, 0)) {
          return false;
        }
        worker = std::thread([this] {
          while (!stopping) {
            {
              std::lock_guard lock(pending_mutex);
              auto now = std::chrono::steady_clock::now();
              for (auto it = pending.begin(); it != pending.end();) {
                if (it->second.deadline < now) {
                  if (!it->second.completed_on_enqueue) {
                    steam_native::response r {};
                    r.id = it->first;
                    r.status = -1;
                    call(steam_native::reply, &r, sizeof(r), nullptr, 0);
                  }
                  it = pending.erase(it);
                } else {
                  ++it;
                }
              }
            }
            steam_native::packet p {};
            if (!call(steam_native::poll, nullptr, 0, &p, sizeof(p))) {
              LARGE_INTEGER due;
              due.QuadPart = -10000; // one millisecond, in 100 ns units
              if (!SetWaitableTimer(poll_timer, &due, 0, nullptr, nullptr, FALSE) ||
                  WaitForSingleObject(poll_timer, 1000) != WAIT_OBJECT_0) {
                BOOST_LOG(error) << "Native controller driver wait failed";
                break;
              }
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
            if (allowed && p.op == steam_native::operation::set_feature) {
              allowed = steam_native::safe_feature(p.data[0], p.data[1], p.data[2]);
            } else if (allowed && p.op == steam_native::operation::write) {
              const auto size = steam_native::output_size(p.data[0]);
              allowed = size && (p.size == size || p.size == 64);
            }
            const bool asynchronous = p.op != steam_native::operation::get_feature;
            {
              std::lock_guard lock(pending_mutex);
              auto now = std::chrono::steady_clock::now();
              if (allowed && pending.size() < 32) {
                // Complete validated writes on enqueue. Feature reads remain
                // pending until their ordered physical reply arrives.
                pending.emplace(p.id, request {p.op, now + std::chrono::seconds(2), asynchronous});
              } else {
                allowed = false;
              }
            }
            if (allowed) {
              feature_requests++;
              send(out);
              if (asynchronous) {
                steam_native::response r {};
                r.id = p.id;
                call(steam_native::reply, &r, sizeof(r), nullptr, 0);
              }
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
          if (!it->second.completed_on_enqueue) {
            call(steam_native::reply, &r, sizeof(r), nullptr, 0);
          } else if (r.status) {
            BOOST_LOG(warning) << "Native Steam Controller asynchronous command delivery failed for request " << id;
          }
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
        if (poll_timer) CloseHandle(poll_timer);
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
