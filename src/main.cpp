#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2s_std.h>
#include <esp_random.h>
#include <atomic>
#include <AmbientEngine.h>
#include <DeviceControls.h>
#include "BleControl.h"
#include "config.h"
#include "es8311.h"
#include "web_ui.h"

namespace {
constexpr uint32_t SAMPLE_RATE = ambient::Engine::sampleRate;
constexpr uint8_t EXPANDER = 0x20;
WebServer server(80);
Preferences prefs;
Adafruit_NeoPixel ring(7, 38, RING_PIXEL_ORDER + NEO_KHZ800);
i2s_chan_handle_t tx = nullptr;
ambient::Engine ambientEngine;
ambient::Settings soundSettings;
struct AudioCommand { ambient::Settings settings; bool playing; bool ambientMode; };
QueueHandle_t audioCommands = nullptr;
bool ambientMode = true;
const char* const PALETTES[] = {"water", "moss", "dusk"};
std::atomic<uint32_t> maxRenderMicros{0};
std::atomic<uint32_t> renderOverruns{0};

std::atomic<bool> audioReady{false};
std::atomic<uint32_t> audioBlocks{0};
bool playing = false, lights = true, dirty = false, prefsReady = false;
uint8_t brightness = 100;
uint32_t colour = 0xff4d00, changedAt = 0;
String apName;

void publishAudio() {
  if (audioCommands) {
    const AudioCommand command{soundSettings, playing, ambientMode};
    xQueueOverwrite(audioCommands, &command);
  }
}


void printAudioDiagnostics() {
  Serial.printf("Audio: ready=%u playing=%u volume=%d blocks=%lu\n", audioReady.load(), playing,
                soundSettings.volume, static_cast<unsigned long>(audioBlocks.load()));
  Serial.printf("Sound: mode=%s palette=%s density=%d space=%d warmth=%d renderMax=%lu us overruns=%lu\n",
                ambientMode ? "ambient" : "white", PALETTES[soundSettings.palette], soundSettings.density,
                soundSettings.space, soundSettings.warmth, static_cast<unsigned long>(maxRenderMicros.load()),
                static_cast<unsigned long>(renderOverruns.load()));
  for (uint8_t address : {uint8_t(0x18), EXPANDER}) {
    Serial.printf("I2C 0x%02x:", address);
    for (uint8_t reg : {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x09, 0x0d, 0x0e, 0x12, 0x13, 0x14, 0x31, 0x32, 0x37}) {
      if (address == EXPANDER && reg > 7) continue;
      Wire.beginTransmission(address);
      Wire.write(reg);
      if (Wire.endTransmission(false) || Wire.requestFrom(address, uint8_t(1)) != 1) {
        Serial.printf(" %02x=ERR", reg);
      } else Serial.printf(" %02x=%02x", reg, Wire.read());
    }
    Serial.println();
  }
  Serial.printf("Network: %s\n", WiFi.localIP().toString().c_str());
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(EXPANDER);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool amplifier(bool enabled) {
  // TCA9555 port 1 bit 0 = EXIO8 = PA_CTRL. Keep all other pins as inputs.
  return writeReg(3, enabled ? 1 : 0) && writeReg(7, 0xfe);
}

void updateLights() {
  // Scale from the original colour every time, avoiding cumulative rounding.
  const unsigned scale = lights ? brightness : 0;
  const uint8_t r = ((colour >> 16) & 255) * scale / 100;
  const uint8_t g = ((colour >> 8) & 255) * scale / 100;
  const uint8_t b = (colour & 255) * scale / 100;
  ring.fill(ring.Color(r, g, b));
  ring.show();
}

void audioTask(void *) {
  int16_t frames[256 * 2], mono[256];
  AudioCommand current{};
  uint32_t randomState = esp_random();
  if (!randomState) randomState = 1;
  float gain = 0;
  while (true) {
    const uint32_t started = micros();
    AudioCommand next;
    if (xQueueReceive(audioCommands, &next, 0) == pdTRUE) {
      current = next;
      ambientEngine.configure(current.settings);
      if (current.playing && current.ambientMode) ambientEngine.start();
      else ambientEngine.stop();
    }
    ambientEngine.render(mono, 256);
    const float level = current.settings.volume / 100.0f;
    const float target = current.playing && !current.ambientMode ? level * level * 0.35f : 0;
    for (size_t i = 0; i < 256; ++i) {
      randomState ^= randomState << 13;
      randomState ^= randomState >> 17;
      randomState ^= randomState << 5;
      // About 20 ms of smoothing prevents abrupt start/stop/volume edges.
      gain += (target - gain) * (1.0f / (SAMPLE_RATE * 0.02f));
      const int32_t noise = static_cast<int32_t>(randomState >> 16) - 32768;
      const float mix = mono[i] + noise * gain;
      const int16_t sample = static_cast<int16_t>(constrain(mix, -32767.0f, 32767.0f));
      frames[i * 2] = sample;
      frames[i * 2 + 1] = sample;  // Same signal in both slots for the mono DAC.
    }
    const uint32_t elapsed = micros() - started;
    if (elapsed > maxRenderMicros.load()) maxRenderMicros.store(elapsed);
    if (elapsed > 256 * 1000000UL / SAMPLE_RATE) renderOverruns.fetch_add(1);
    size_t offset = 0;
    while (offset < sizeof(frames)) {
      size_t written = 0;
      const esp_err_t err = i2s_channel_write(tx, reinterpret_cast<uint8_t *>(frames) + offset,
                                             sizeof(frames) - offset, &written, 1000);
      if (err != ESP_OK || written == 0) {
        audioReady.store(false);
        Serial.printf("Audio output failed: %s\n", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
      }
      offset += written;
    }
    audioBlocks.fetch_add(1, std::memory_order_relaxed);
  }
}

bool initAudio() {
  if (!amplifier(false)) return false;
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 8;
  channel.dma_frame_num = 256;
  if (i2s_new_channel(&channel, &tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = GPIO_NUM_12;
  config.gpio_cfg.bclk = GPIO_NUM_13;
  config.gpio_cfg.ws = GPIO_NUM_14;
  config.gpio_cfg.dout = GPIO_NUM_16;
  config.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(tx, &config) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) return false;
  const auto codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (!codec) return false;
  const es8311_clock_config_t clock = {false, false, true, SAMPLE_RATE * 256, SAMPLE_RATE};
  if (es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_voice_volume_set(codec, 75, nullptr) != ESP_OK ||
      // The hardware resets with DAC mute bits set; init() does not clear them.
      es8311_voice_mute(codec, false) != ESP_OK) return false;
  ambientEngine.reseed(esp_random());
  audioCommands = xQueueCreate(1, sizeof(AudioCommand));
  if (!audioCommands) return false;
  publishAudio();
  audioReady.store(true);
  if (xTaskCreatePinnedToCore(audioTask, "sound-generator", 6144, nullptr, 4, nullptr, 1) != pdPASS) {
    audioReady.store(false);
    return false;
  }
  delay(30); // Start with silence in the DMA buffers before enabling the speaker.
  if (!amplifier(true)) { audioReady.store(false); return false; }
  return true;
}

String stateJson() {
  char data[384];
  snprintf(data, sizeof(data),
    "{\"playing\":%s,\"volume\":%d,\"mode\":\"%s\",\"palette\":\"%s\","
    "\"density\":%d,\"space\":%d,\"warmth\":%d,\"lights\":%s,"
    "\"brightness\":%u,\"colour\":\"#%06lx\",\"audioReady\":%s}",
    playing ? "true" : "false", soundSettings.volume, ambientMode ? "ambient" : "white",
    PALETTES[soundSettings.palette], soundSettings.density, soundSettings.space, soundSettings.warmth,
    lights ? "true" : "false", brightness, static_cast<unsigned long>(colour),
    audioReady.load() ? "true" : "false");
  return String(data);
}

void sendState() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", stateJson());
}

device::State currentState() {
  device::State state;
  state.sound = soundSettings;
  state.playing = playing;
  state.lights = lights;
  state.ambientMode = ambientMode;
  state.brightness = brightness;
  state.colour = colour;
  return state;
}

void commitState(const device::State& state) {
  soundSettings = state.sound;
  playing = state.playing;
  lights = state.lights;
  ambientMode = state.ambientMode;
  brightness = state.brightness;
  colour = state.colour;
  publishAudio();
  updateLights();
  dirty = true;
  changedAt = millis();
  ble::stateChanged(stateJson().c_str());
}

void changeState() {
  auto next = currentState();
  // Validate the entire request before applying any fields to the device.
  for (int i = 0; i < server.args(); ++i) {
    if (!device::apply(next, server.argName(i).c_str(), server.arg(i).c_str())) {
      server.send(400, "text/plain", "Invalid control value");
      return;
    }
  }
  if (next.playing && !audioReady.load()) {
    server.send(503, "text/plain", "Audio unavailable; check serial log and restart");
    return;
  }
  commitState(next);
  sendState();
}

void serviceBle() {
  ble::Request request;
  // Limit work per iteration so a fast BLE client cannot starve Wi-Fi and USB.
  for (unsigned i = 0; i < 4 && ble::next(request); ++i) {
    auto next = currentState();
    if (!device::command(next, request.text, request.length)) ble::reply(request, "error:invalid");
    else if (next.playing && !audioReady.load()) ble::reply(request, "error:audio");
    else {
      commitState(next);
      ble::reply(request);
    }
  }
}

void serialCommand(char* line) {
  char* name = strtok(line, " \t");
  char* value = strtok(nullptr, " \t");
  char* extra = strtok(nullptr, " \t");
  if (!name) return;
  if ((!strcmp(name, "d") || !strcmp(name, "status")) && !value) {
    printAudioDiagnostics();
    return;
  }
  auto next = currentState();
  bool valid = false;
  if (!extra) {
    if (!strcmp(name, "play") && !value) valid = device::apply(next, "playing", "true");
    else if (!strcmp(name, "stop") && !value) valid = device::apply(next, "playing", "false");
    else valid = device::apply(next, name, value);
  }
  if (!valid || (next.playing && !audioReady.load())) {
    Serial.println("Rejected. Use play, stop, status, or a setting name and value.");
    return;
  }
  commitState(next);
  printAudioDiagnostics();
}

void readSerial() {
  static char line[96];
  static size_t length = 0;
  static bool overflow = false;
  for (unsigned i = 0; i < 80 && Serial.available(); ++i) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (overflow) Serial.println("Serial command too long");
      else { line[length] = 0; serialCommand(line); }
      length = 0;
      overflow = false;
    } else if (length < sizeof(line) - 1) line[length++] = c;
    else overflow = true;
  }
}

void startNetwork() {
  WiFi.setHostname("sound-machine");
  if (strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Open http://%s/ or http://sound-machine.local/\n", WiFi.localIP().toString().c_str());
  } else {
    // AP+STA allows configured Wi-Fi to reconnect later while keeping fallback access.
    WiFi.mode(strlen(WIFI_SSID) ? WIFI_AP_STA : WIFI_AP);
    apName = "SoundMachine-" + WiFi.macAddress().substring(12);
    apName.replace(":", "");
    if (!WiFi.softAP(apName.c_str(), AP_PASSWORD)) Serial.println("Wi-Fi AP failed: check AP_PASSWORD (8-63 characters)");
    Serial.printf("Join %s; open http://%s/\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
  }
  if (MDNS.begin("sound-machine")) MDNS.addService("http", "tcp", 80);
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  prefsReady = prefs.begin("sound-machine", false);
  if (prefsReady) {
    soundSettings.volume = min<unsigned>(prefs.getUChar("volume", 20), 100);
    soundSettings.density = min<unsigned>(prefs.getUChar("density", 35), 100);
    soundSettings.space = min<unsigned>(prefs.getUChar("space", 65), 100);
    soundSettings.warmth = min<unsigned>(prefs.getUChar("warmth", 65), 100);
    soundSettings.palette = min<unsigned>(prefs.getUChar("palette", 0), 2);
    ambientMode = prefs.getBool("ambient", true);
    brightness = min<unsigned>(prefs.getUChar("brightness", 100), 100);
    colour = prefs.getUInt("colour", 0xff4d00) & 0xffffff;
    lights = prefs.getBool("lights", true);
  }
  ring.begin();
  updateLights();
  if (!Wire.begin(11, 10, 400000) || !initAudio()) {
    audioReady.store(false);
    amplifier(false);
    Serial.println("Audio initialisation failed. LED and web controls remain available.");
  }
  if (ble::begin(stateJson().c_str())) Serial.println("BLE advertising as SoundMachine");
  else Serial.println("BLE initialization failed; Wi-Fi and USB remain available.");
  startNetwork();
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_UI); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/state", HTTP_POST, changeState);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println("USB serial commands (end with newline): play, stop, status, mode ambient|white, palette water|moss|dusk, volume|density|space|warmth 0..100.");
  printAudioDiagnostics();
}

void loop() {
  server.handleClient();
  readSerial();
  serviceBle();
  if (playing && !audioReady.load()) {
    playing = false;
    publishAudio();
    amplifier(false);
    ble::stateChanged(stateJson().c_str());
  }
  static bool previousReady = audioReady.load();
  const bool ready = audioReady.load();
  if (ready != previousReady) {
    previousReady = ready;
    ble::stateChanged(stateJson().c_str());
  }
  // Debounce flash writes while dragging sliders. Playback always starts stopped after reboot.
  if (dirty && millis() - changedAt > 2000) {
    if (prefsReady) {
      prefs.putUChar("volume", soundSettings.volume);
      prefs.putUChar("density", soundSettings.density);
      prefs.putUChar("space", soundSettings.space);
      prefs.putUChar("warmth", soundSettings.warmth);
      prefs.putUChar("palette", soundSettings.palette);
      prefs.putBool("ambient", ambientMode);
      prefs.putUChar("brightness", brightness);
      prefs.putUInt("colour", colour);
      prefs.putBool("lights", lights);
    }
    dirty = false;
  }
  delay(2);
}
