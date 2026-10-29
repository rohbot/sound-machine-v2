#include "AmbientEngine.h"
#include <algorithm>
#include <cmath>

namespace ambient {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int roots[] = {48, 45, 46};
constexpr int notes[][6] = {{0, 4, 7, 11, 14, 19}, {0, 3, 7, 10, 14, 17}, {0, 5, 7, 10, 14, 17}};
constexpr float intervals[] = {1, 1.2f, 1.4f};
float frequency(int midi) { return 440 * std::pow(2.0f, (midi - 69) / 12.0f); }
float coefficient(float seconds) { return 1 - std::exp(-1.0f / (Engine::sampleRate * seconds)); }
int percent(int value) { return std::max(0, std::min(100, value)); }
}

Engine::Engine(uint32_t seed) : random_(seed ? seed : 1) {
  for (size_t i = 0; i < sine_.size(); ++i) sine_[i] = std::sin(2 * pi * i / 2048);
  // Four damped parallel combs and two serial allpasses. About 49 KiB total engine state.
  const size_t lengths[] = {1117, 1381, 1613, 1789, 337, 113};
  for (size_t i = 0; i < delays_.size(); ++i) {
    delays_[i].length = lengths[i];
    delays_[i].feedback = std::exp(-static_cast<float>(lengths[i]) / (sampleRate * 1.3f));
  }
  for (int p = 0; p < 3; ++p) for (int n = 0; n < 2; ++n) {
    droneStep_[p * 2 + n] = frequency(roots[p] - 12 + n * 7) * std::pow(2.0f, random(-3, 3) / 1200) / sampleRate;
  }
}

const char* Engine::paletteName(int index) {
  static const char* names[] = {"Open Water", "Moss Garden", "After Hours"};
  return index >= 0 && index < 3 ? names[index] : "Unknown";
}

void Engine::configure(Settings settings) {
  settings.density = percent(settings.density);
  settings.space = percent(settings.space);
  settings.warmth = percent(settings.warmth);
  settings.volume = percent(settings.volume);
  settings.palette = std::max(0, std::min(2, settings.palette));
  settings_ = settings;
}

void Engine::clear() {
  voices_ = {};
  for (auto& d : delays_) { d.data.fill(0); d.position = 0; d.damp = 0; }
  for (float& g : droneGain_) g = 0;
  master_ = z1_ = z2_ = 0;
  limiter_ = 1;
  clock_ = 0;
}

void Engine::start() {
  if (playing_) return;
  clear();
  playing_ = true;
  fading_ = false;
  nextNote_[0] = 0;
  nextNote_[1] = static_cast<uint64_t>(1.7f * sampleRate);
  nextNote_[2] = static_cast<uint64_t>(4.2f * sampleRate);
}

void Engine::stop() { if (playing_) { playing_ = false; fading_ = true; stopFrames_ = sampleRate; } }

float Engine::random(float low, float high) {
  random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
  return low + (high - low) * ((random_ >> 8) * (1.0f / 16777216));
}

float Engine::oscillator(float& phase, float step) const {
  float position = phase * 2048;
  const auto i = static_cast<size_t>(position);
  float result = sine_[i] + (sine_[i + 1] - sine_[i]) * (position - i);
  phase += step;
  if (phase >= 1) phase -= 1;
  return result;
}

void Engine::note(int layer) {
  for (auto& v : voices_) {
    if (v.duration) continue;
    v = {};
    int p = settings_.palette;
    int midi = roots[p] + notes[p][static_cast<int>(random(0, 6))] + (layer == 2 ? 12 : 0);
    for (int h = 0; h < 2; ++h) v.step[h] = frequency(midi) * (h + 1) * std::pow(2.0f, random(-4, 4) / 1200) / sampleRate;
    v.duration = static_cast<uint32_t>(random(6, 12) * sampleRate);
    v.attack = static_cast<uint32_t>(random(1, 2.5f) * sampleRate);
    v.peak = random(0.035f, 0.075f);
    v.attackStep = v.peak / v.attack;
    v.release = std::pow(0.0001f / v.peak, 1.0f / (v.duration - v.attack));
    return;
  }
}

float Engine::reverb(float input) {
  float sum = 0;
  for (int i = 0; i < 4; ++i) {
    auto& d = delays_[i];
    float out = d.data[d.position];
    d.damp += 0.45f * (out - d.damp);
    d.data[d.position] = input + d.damp * d.feedback;
    if (++d.position == d.length) d.position = 0;
    sum += out * 0.25f;
  }
  for (int i = 4; i < 6; ++i) {
    auto& d = delays_[i];
    float delayed = d.data[d.position];
    float out = delayed - sum * 0.5f;
    d.data[d.position] = sum + out * 0.5f;
    if (++d.position == d.length) d.position = 0;
    sum = out;
  }
  return sum * 0.3f;
}

size_t Engine::activeVoices() const {
  size_t result = 0;
  for (const auto& v : voices_) if (v.duration) ++result;
  return result;
}

void Engine::render(int16_t* mono, size_t frames) {
  if (!playing_ && !fading_) { std::fill_n(mono, frames, 0); return; }
  // Filter coefficients move at block boundaries with a 0.5 s time constant.
  const float smooth = 1 - std::exp(-static_cast<float>(frames) / (sampleRate * 0.5f));
  cutoff_ += smooth * (800 + (100 - settings_.warmth) * 55 - cutoff_);
  dry_ += smooth * (0.8f - settings_.space * 0.0045f - dry_);
  wet_ += smooth * (0.12f + settings_.space * 0.0075f - wet_);
  float w = 2 * pi * cutoff_ / sampleRate, c = std::cos(w), alpha = std::sin(w) / (2 * 0.35f);
  float a0 = 1 + alpha, b0 = (1 - c) / (2 * a0), b1 = 2 * b0;
  float a1 = -2 * c / a0, a2 = (1 - alpha) / a0;
  const float masterRate = coefficient(playing_ ? 1.5f : 0.2f);
  const float droneRate = coefficient(3);
  const float releaseRate = coefficient(0.25f);
  for (size_t frame = 0; frame < frames; ++frame) {
    if (!playing_ && !fading_) { mono[frame] = 0; continue; }
    if (playing_) for (int layer = 0; layer < 3; ++layer) {
      if (clock_ >= nextNote_[layer]) {
        note(layer);
        nextNote_[layer] = clock_ + static_cast<uint64_t>(sampleRate * random(3, 7) * (1.65f - settings_.density * 0.012f) * intervals[settings_.palette]);
      }
    }
    float mix = 0;
    for (int d = 0; d < 6; ++d) {
      droneGain_[d] += droneRate * ((d / 2 == settings_.palette ? 0.055f : 0) - droneGain_[d]);
      mix += droneGain_[d] * oscillator(dronePhase_[d], droneStep_[d]);
    }
    for (auto& v : voices_) if (v.duration) {
      v.gain = v.age < v.attack ? v.gain + v.attackStep : v.gain * v.release;
      mix += v.gain * (oscillator(v.phase[0], v.step[0]) + 0.15f * oscillator(v.phase[1], v.step[1]));
      if (++v.age >= v.duration) v.duration = 0;
    }
    float filtered = b0 * mix + z1_;
    z1_ = b1 * mix - a1 * filtered + z2_;
    z2_ = b0 * mix - a2 * filtered;
    master_ += masterRate * ((playing_ ? settings_.volume * 0.0065f : 0) - master_);
    float out = (filtered * dry_ + reverb(filtered) * wet_) * master_;
    // Fast-attack peak limiter plus a final PCM clamp, without per-sample transcendentals.
    float peak = std::fabs(out);
    float target = peak > 0.25f ? 0.25f / peak : 1;
    limiter_ = target < limiter_ ? target : limiter_ + releaseRate * (target - limiter_);
    out = std::max(-0.98f, std::min(0.98f, out * limiter_));
    mono[frame] = static_cast<int16_t>(out * 32767);
    ++clock_;
    if (fading_ && --stopFrames_ == 0) { fading_ = false; clear(); }
  }
}
}  // namespace ambient
