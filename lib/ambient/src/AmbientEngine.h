#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ambient {
struct Settings {
  int density = 35;
  int space = 65;
  int warmth = 65;
  int volume = 55;
  int palette = 0;
};

// Single owner: call all methods from the audio task (or a host renderer).
class Engine {
 public:
  static constexpr int sampleRate = 32000;
  explicit Engine(uint32_t seed = 1);
  void reseed(uint32_t seed) { random_ = seed ? seed : 1; }
  void configure(Settings settings);
  void start();
  void stop();
  void render(int16_t* mono, size_t frames);
  bool playing() const { return playing_; }
  size_t activeVoices() const;
  static const char* paletteName(int index);

 private:
  struct Voice {
    float phase[2]{}, step[2]{};
    float gain = 0, peak = 0, attackStep = 0, release = 0;
    uint32_t age = 0, attack = 0, duration = 0;
  };
  struct Delay {
    std::array<float, 2048> data{};
    size_t position = 0, length = 1;
    float damp = 0, feedback = 0;
  };
  Settings settings_{};
  std::array<float, 2049> sine_{};
  std::array<Voice, 32> voices_{};
  std::array<Delay, 6> delays_{};
  float dronePhase_[6]{}, droneStep_[6]{}, droneGain_[6]{};
  uint64_t clock_ = 0, nextNote_[3]{};
  uint32_t random_, stopFrames_ = 0;
  bool playing_ = false, fading_ = false;
  float master_ = 0, dry_ = 0.5075f, wet_ = 0.6075f, cutoff_ = 2725;
  float z1_ = 0, z2_ = 0, limiter_ = 1;
  float random(float low, float high);
  float oscillator(float& phase, float step) const;
  void note(int layer);
  void clear();
  float reverb(float input);
};
}  // namespace ambient
