#pragma once
#include "DeviceControls.h"
#include <cstdint>

namespace device {
class DebouncedButton {
 public:
  // A button held during boot/recovery must be released before it can trigger.
  void reset() { initialized_ = false; }
  bool update(bool pressed, uint32_t now) {
    if (!initialized_) {
      initialized_ = true;
      raw_ = stable_ = pressed;
      changed_ = now;
      return false;
    }
    if (pressed != raw_) { raw_ = pressed; changed_ = now; }
    if (raw_ != stable_ && static_cast<uint32_t>(now - changed_) >= 40) {
      stable_ = raw_;
      return stable_;
    }
    return false;
  }
 private:
  bool initialized_ = false, raw_ = false, stable_ = false;
  uint32_t changed_ = 0;
};

inline bool toggleWhiteNoise(State& state, bool audioReady) {
  if (!audioReady) return false;
  const bool stop = state.playing && !state.ambientMode;
  state.ambientMode = false;
  state.playing = !stop;
  return true;
}
} // namespace device
