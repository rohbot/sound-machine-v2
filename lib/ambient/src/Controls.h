#pragma once
#include "AmbientEngine.h"
#include <cstring>

namespace ambient {
// Shared strict validation for serial, HTTP and MQTT. Failure leaves state untouched.
inline bool applyControl(Settings& settings, bool& playing, const char* name, const char* value) {
  if (!name || !value) return false;
  if (!std::strcmp(name, "playing")) {
    if (!std::strcmp(value, "true")) playing = true;
    else if (!std::strcmp(value, "false")) playing = false;
    else return false;
    return true;
  }
  if (!std::strcmp(name, "palette")) {
    if (!std::strcmp(value, "water")) settings.palette = 0;
    else if (!std::strcmp(value, "moss")) settings.palette = 1;
    else if (!std::strcmp(value, "dusk")) settings.palette = 2;
    else return false;
    return true;
  }
  int* parameter = nullptr;
  if (!std::strcmp(name, "density")) parameter = &settings.density;
  if (!std::strcmp(name, "space")) parameter = &settings.space;
  if (!std::strcmp(name, "warmth")) parameter = &settings.warmth;
  if (!std::strcmp(name, "volume")) parameter = &settings.volume;
  if (!parameter || !*value) return false;
  int number = 0;
  for (const char* c = value; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    number = number * 10 + (*c - '0');
    if (number > 100) return false;
  }
  *parameter = number;
  return true;
}
}
