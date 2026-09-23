#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace native_controller {
  using message = std::array<std::uint8_t, 80>;
  using feedback = std::function<void(const message &)>;

  struct session {
    virtual ~session() = default;
    virtual void receive(const message &) = 0;
  };

  bool enabled();
  std::unique_ptr<session> attach(feedback send);
}  // namespace native_controller
