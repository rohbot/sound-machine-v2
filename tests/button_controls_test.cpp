#include <ButtonControls.h>
#include <cassert>
#include <cstdio>

int main() {
  device::DebouncedButton button;
  assert(!button.update(false, 0));
  assert(!button.update(true, 10));
  assert(!button.update(false, 20)); // Contact bounce restarts qualification.
  assert(!button.update(true, 30));
  assert(!button.update(true, 69));
  assert(button.update(true, 70));
  assert(!button.update(true, 10000)); // Holding never repeats.
  assert(!button.update(false, 10010));
  assert(!button.update(true, 10020)); // Release bounce does not re-arm.
  assert(!button.update(true, 10080));
  assert(!button.update(false, 10100));
  assert(!button.update(false, 10140));
  assert(!button.update(true, 10150));
  assert(button.update(true, 10190));
  button.reset();
  assert(!button.update(true, 20000));
  assert(!button.update(true, 21000)); // Held on boot/bus recovery is ignored.
  assert(!button.update(false, 21010));
  assert(!button.update(false, 21050));
  assert(!button.update(true, 21060));
  assert(button.update(true, 21100));
  button.reset();
  assert(!button.update(false, UINT32_MAX - 30));
  assert(!button.update(true, UINT32_MAX - 20));
  assert(!button.update(true, 18));
  assert(button.update(true, 19)); // Debounce also works across millis rollover.

  device::State state;
  const auto original = state;
  assert(!device::toggleWhiteNoise(state, false));
  assert(state.ambientMode == original.ambientMode && state.playing == original.playing);
  assert(device::toggleWhiteNoise(state, true));
  assert(state.playing && !state.ambientMode);
  assert(state.lights == original.lights && state.colour == original.colour && state.brightness == original.brightness);
  assert(device::toggleWhiteNoise(state, true));
  assert(!state.playing && !state.ambientMode);
  state.playing = true;
  state.ambientMode = true;
  assert(device::toggleWhiteNoise(state, true));
  assert(state.playing && !state.ambientMode); // Active ambient changes to white noise.
  assert(state.sound.volume == original.sound.volume && state.sound.palette == original.sound.palette);
  std::puts("Button debounce, hold/release, startup, recovery, rollover, and white-noise toggles passed.");
}
