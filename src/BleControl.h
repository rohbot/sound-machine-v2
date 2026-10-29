#pragma once
#include <Arduino.h>
namespace ble {
inline constexpr char SERVICE[] = "d973f2e0-b19e-4b9e-a316-4bb1c0c28f70";
inline constexpr char COMMAND[] = "d973f2e1-b19e-4b9e-a316-4bb1c0c28f70";
inline constexpr char STATE[] = "d973f2e2-b19e-4b9e-a316-4bb1c0c28f70";
inline constexpr char EVENT[] = "d973f2e3-b19e-4b9e-a316-4bb1c0c28f70";
struct Request { char text[21]; uint8_t length; uint16_t connection; };
bool begin(const char* initialState);
bool next(Request& request);
void stateChanged(const char* json);
void reply(const Request& request, const char* error = nullptr);
} // namespace ble
