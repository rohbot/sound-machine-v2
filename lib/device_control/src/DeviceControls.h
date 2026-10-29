#pragma once
#include <Controls.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace device {
struct State {
  ambient::Settings sound;
  bool playing = false, lights = true, ambientMode = true;
  uint8_t brightness = 100;
  uint32_t colour = 0xff4d00;
};

// Shared by HTTP, BLE, and USB. Invalid fields never modify the supplied state.
inline bool apply(State& state, const char* name, const char* value) {
  if (!name || !value) return false;
  if ((!std::strcmp(name, "volume") || !std::strcmp(name, "density") || !std::strcmp(name, "space") ||
       !std::strcmp(name, "warmth") || !std::strcmp(name, "brightness")) && std::strlen(value) > 3) return false;
  if (!std::strcmp(name, "lights")) {
    if (!std::strcmp(value, "true")) state.lights = true;
    else if (!std::strcmp(value, "false")) state.lights = false;
    else return false;
    return true;
  }
  if (!std::strcmp(name, "mode")) {
    if (!std::strcmp(value, "ambient")) state.ambientMode = true;
    else if (!std::strcmp(value, "white")) state.ambientMode = false;
    else return false;
    return true;
  }
  if (!std::strcmp(name, "brightness")) {
    ambient::Settings parsed;
    if (!ambient::applyControl(parsed, state.playing, "volume", value)) return false;
    state.brightness = parsed.volume;
    return true;
  }
  if (!std::strcmp(name, "colour")) {
    if (std::strlen(value) != 7 || value[0] != '#') return false;
    uint32_t colour = 0;
    for (int i = 1; i < 7; ++i) {
      const char c = value[i];
      int digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else return false;
      colour = colour * 16 + digit;
    }
    state.colour = colour;
    return true;
  }
  return ambient::applyControl(state.sound, state.playing, name, value);
}

// One complete ASCII name=value command per GATT write; fits the minimum ATT MTU.
inline bool command(State& state, const char* bytes, size_t length) {
  if (!bytes || !length || length > 20) return false;
  char text[21];
  for (size_t i = 0; i < length; ++i) {
    if (bytes[i] < 33 || bytes[i] > 126) return false;
    text[i] = bytes[i];
  }
  text[length] = 0;
  char* equal = std::strchr(text, '=');
  if (!equal || equal == text || !equal[1] || std::strchr(equal + 1, '=')) return false;
  *equal = 0;
  return apply(state, text, equal + 1);
}
} // namespace device
