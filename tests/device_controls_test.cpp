#include <DeviceControls.h>
#include <cassert>
#include <cstring>
#include <cstdio>

bool same(const device::State& a, const device::State& b) {
  return a.playing == b.playing && a.lights == b.lights && a.ambientMode == b.ambientMode &&
    a.brightness == b.brightness && a.colour == b.colour &&
    a.sound.volume == b.sound.volume && a.sound.palette == b.sound.palette &&
    a.sound.density == b.sound.density && a.sound.space == b.sound.space && a.sound.warmth == b.sound.warmth;
}
int main() {
  device::State state;
  for (const char* command : {"playing=true", "volume=30", "mode=white", "mode=ambient", "palette=moss",
                            "density=80", "space=0", "warmth=100", "lights=false", "brightness=0", "colour=#aBcD09"})
    assert(device::command(state, command, std::strlen(command)));
  assert(state.playing && !state.lights && state.ambientMode && state.sound.volume == 30);
  assert(state.sound.palette == 1 && state.sound.density == 80 && state.sound.space == 0 && state.sound.warmth == 100);
  assert(state.colour == 0xabcd09 && state.brightness == 0);
  for (const char* bad : {"", "volume", "=30", "volume=", "volume=1=2", "volume=-1", "volume=101",
                        "volume=3.5", "brightness=101", "brightness=-1", "colour=#12345x", "colour=123456",
                        "colour=#1234567", "lights=1", "playing=True", "mode=rain", "palette=rain",
                        "audioReady=true", "colour=#123456\n", " volume=3", "volume= 3", "density=20\r\n",
                        "volume=000000000000001", "volume=0000"}) {
    const auto before = state;
    assert(!device::command(state, bad, std::strlen(bad)));
    assert(same(before, state));
  }
  const char embeddedNull[] = "volume=3\0bad";
  assert(!device::command(state, embeddedNull, sizeof(embeddedNull) - 1));
  assert(!device::command(state, nullptr, 5));
  for (const char* name : {"volume", "density", "space", "warmth", "brightness"}) {
    assert(device::apply(state, name, "0"));
    assert(device::apply(state, name, "100"));
    const auto before = state;
    assert(!device::apply(state, name, "9999999999999999999999999999999999999999"));
    assert(same(before, state));
  }
  assert(device::apply(state, "colour", "#000000") && state.colour == 0);
  assert(device::apply(state, "colour", "#ffffff") && state.colour == 0xffffff);
  // Transactional callers validate a copy, applying none of a failed batch.
  const auto before = state;
  auto next = state;
  assert(device::apply(next, "volume", "20"));
  assert(!device::apply(next, "palette", "invalid"));
  assert(same(state, before));
  std::puts("BLE framing, sound and LED validation, boundaries, and rejection atomicity passed.");
}
