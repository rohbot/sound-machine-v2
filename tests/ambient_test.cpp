#include <AmbientEngine.h>
#include <Controls.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  ambient::Settings settings;
  bool playing = false;
  assert(ambient::applyControl(settings, playing, "playing", "true") && playing);
  assert(ambient::applyControl(settings, playing, "palette", "moss") && settings.palette == 1);
  for (const char* field : {"density", "space", "warmth", "volume"}) {
    assert(ambient::applyControl(settings, playing, field, "0"));
    assert(ambient::applyControl(settings, playing, field, "100"));
    for (const char* bad : {"", "101", "-1", "1.5", "20oops", " 30", "99999999999999999999999"})
      assert(!ambient::applyControl(settings, playing, field, bad));
  }
  assert(!ambient::applyControl(settings, playing, "palette", "invalid") && settings.palette == 1);
  assert(!ambient::applyControl(settings, playing, "playing", "1") && playing);
  assert(!ambient::applyControl(settings, playing, "unknown", "20"));
  assert(settings.density == 100 && settings.volume == 100);

  ambient::Engine engine(1234), same(1234);
  std::array<int16_t, 256> samples{}, reference{};
  engine.render(samples.data(), samples.size());
  assert(std::all_of(samples.begin(), samples.end(), [](int16_t s) { return s == 0; }));
  engine.start(); same.start();
  for (int i = 0; i < 100; ++i) {
    engine.render(samples.data(), samples.size());
    same.render(reference.data(), reference.size());
    assert(samples == reference);
  }
  for (int palette = 0; palette < 3; ++palette) {
    settings.palette = palette;
    settings.volume = settings.density = settings.space = 100;
    settings.warmth = palette == 1 ? 100 : 0;
    engine.configure(settings);
    double energy = 0;
    int peak = 0;
    for (int block = 0; block < 7500; ++block) { // 60 seconds per palette at maximum density.
      engine.render(samples.data(), samples.size());
      assert(engine.activeVoices() <= 32);
      for (int s : samples) { energy += static_cast<double>(s) * s; peak = std::max(peak, std::abs(s)); }
    }
    assert(energy > 1000000 && peak < 32767);
    std::printf("Palette %d: peak=%d RMS=%.1f\n", palette, peak, std::sqrt(energy / (7500 * samples.size())));
  }
  engine.stop();
  for (int i = 0; i < 126; ++i) engine.render(samples.data(), samples.size());
  assert(!engine.playing() && engine.activeVoices() == 0);
  assert(std::all_of(samples.begin(), samples.end(), [](int16_t s) { return s == 0; }));
  engine.start();
  bool audible = false;
  for (int i = 0; i < 500; ++i) {
    engine.render(samples.data(), samples.size());
    for (int s : samples) audible |= s != 0;
  }
  assert(audible && engine.playing());
  std::puts("Control validation, determinism, 180-second DSP stress, stop and restart passed.");
}
