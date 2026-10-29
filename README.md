# Ambient Sound Machine

Firmware for the **Waveshare ESP32-S3-AUDIO-Board** with Wi-Fi and BLE controls for:

- Ambient soundscapes ported from `/home/roh/code/amb-gen-esp32`, with Open Water, Moss Garden, and After Hours palettes.
- Density (note activity), space (reverb mix), and warmth (tone filtering) controls.
- White noise as an alternate sound mode, plus shared start/stop and volume.
- A single selectable colour across the seven surround RGB LEDs.
- Independent lighting on/off and brightness.
- Saved sound mode, palette, density, space, warmth, volume, colour, brightness, and lighting state.

Audio is generated on the ESP32 at 32 kHz; no SD card, audio downloads, cloud service, or Home Assistant is required. The web page and all its assets are embedded in the firmware. Sound always starts **stopped** after a power cycle. Volume transitions are smoothed, and output is attenuated in software. Ambient mode adds layered detuned notes, bass drones, slow envelopes, reverb, and peak limiting. These palettes are musical soundscapes, rather than recordings of water or gardens. Palette changes allow existing notes to finish while the bass drones crossfade.

## Build and flash

Connect a suitable speaker to the board's speaker connector, then connect the board to your computer with a USB data cable.

Use PlatformIO Core **6.1.19 or later**, or the PlatformIO extension in VS Code. This project pins pioarduino / Arduino-ESP32 3.3.11, Adafruit NeoPixel 1.15.2, and NimBLE-Arduino 2.5.2. Dependencies are downloaded on the first build.

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

On the development machine used for this project, `/usr/bin/pio` is an older installation. Use the current installation explicitly if needed:

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
~/.platformio/penv/bin/pio device monitor --baud 115200
```

If automatic port detection fails, list ports with `pio device list`, then add `--upload-port /dev/ttyACM0` to the upload command (substitute your actual port). If the board does not enter download mode, hold **BOOT**, press and release **RESET**, then release **BOOT** and retry upload. Press RESET after flashing if necessary.

Uploading replaces the factory application. The firmware, partition table, and bootloader are uploaded by PlatformIO; no separate filesystem upload is needed.

## Connect directly

Without configured Wi-Fi credentials, the board creates a Wi-Fi access point:

- **Network:** `SoundMachine-XXXX` (unique suffix)
- **Password:** `quietnight`
- **Page:** <http://192.168.4.1/>

Connect your phone or computer to this network and enter that address in a browser. If your phone reports that the network has no internet, choose to stay connected. Start at a low volume and adjust it with the web controls.

## Choose a sound

On the Wi-Fi page, select **Ambient soundscape** under Sound, choose a palette, and press **Start**. The engine fades in over a few seconds:

| Control | Effect |
| --- | --- |
| Open Water | Major harmony with spacious extended notes |
| Moss Garden | Minor harmony with a slower note pattern |
| After Hours | Suspended harmony with the slowest note pattern |
| Density | Higher values generate notes more frequently |
| Space | Higher values increase reverb and reduce the dry mix |
| Warmth | Higher values soften the sound by lowering the filter cutoff |
| Volume | Overall sound level |

Select **White noise** for the original noise generator. The ambient controls hide in this mode; their settings remain saved. LED colour, brightness, and on/off remain independent of the selected sound. New installations default to ambient mode; playback always starts stopped.

## Use your home Wi-Fi

Copy `include/secrets.example.h` to `include/secrets.h`, enter your 2.4 GHz Wi-Fi name and password, then build and flash again. `secrets.h` is ignored by Git. You can also change the direct-connect password there; it must be 8–63 characters.

On a successful connection, open <http://sound-machine.local/> or use the IP address printed in the USB serial monitor. mDNS support varies by browser and network; the IP address is the reliable fallback.

If the configured network is unavailable during boot, the board opens its direct-connect access point after about 15 seconds. The web interface has no login and is intended for a trusted local network. Wi-Fi credentials are configured in the build, not through the web page.

## Bluetooth LE controls

The board advertises as **SoundMachine** and accepts sound and LED settings over BLE while Wi-Fi is running. BLE works without joining a Wi-Fi network. Playback stays on the board's speaker; BLE carries control commands and state.

### From a phone

Use a GATT client such as [nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile). Connect to **SoundMachine** inside the app, expand the service below, enable notifications on **Command results / changes**, then write a UTF-8/text command to **Command: name=value** using **Write with response**. Send one command per write, with no newline.

Examples:

```text
playing=true
volume=30
mode=ambient
palette=moss
density=70
space=80
warmth=60
lights=true
colour=#ff8800
brightness=25
playing=false
```

`mode=white` selects white noise. Sound and lighting settings apply immediately, update the Wi-Fi page on its next poll, and save automatically after two seconds without another change. Playback always starts stopped after a reboot. Read **Current settings (JSON)** to retrieve all settings, including changes made over Wi-Fi or USB.

BLE access currently requires no pairing PIN or authentication; nearby devices can control the machine. Advertising resumes when a client disconnects.

### From a computer

The included Python controller uses [Bleak](https://bleak.readthedocs.io/en/latest/). Create an environment and install its dependency:

```sh
python3 -m venv .venv-ble
.venv-ble/bin/pip install -r tools/requirements-ble.txt
.venv-ble/bin/python tools/ble_control.py scan
.venv-ble/bin/python tools/ble_control.py status
.venv-ble/bin/python tools/ble_control.py set mode=ambient palette=water volume=30 playing=true
.venv-ble/bin/python tools/ble_control.py set lights=true 'colour=#3399ff' brightness=40
.venv-ble/bin/python tools/ble_control.py set playing=false
```

If multiple boards are nearby, specify the address reported by scan: `python tools/ble_control.py --address XX:XX:XX:XX:XX:XX status`. Commands are applied one at a time, with an application acknowledgement before the next write. Multiple commands in one CLI invocation are sequential updates, not an atomic batch. Disconnect phone BLE apps before using the computer controller.

### GATT protocol

| Characteristic | UUID | Properties |
| --- | --- | --- |
| Service | `d973f2e0-b19e-4b9e-a316-4bb1c0c28f70` | Custom service |
| Command | `d973f2e1-b19e-4b9e-a316-4bb1c0c28f70` | Write with response |
| State | `d973f2e2-b19e-4b9e-a316-4bb1c0c28f70` | Read |
| Event | `d973f2e3-b19e-4b9e-a316-4bb1c0c28f70` | Read, Notify |

Command payloads are `name=value`, 1–20 ASCII bytes per GATT write. All valid commands fit the minimum 23-byte ATT MTU. The supported names and values match the HTTP API, including `colour=#RRGGBB`. Blank, malformed, unknown, out-of-range, and embedded-NUL commands leave settings unchanged.

The ATT write response confirms receipt, not application. Subscribe to Event before writing and wait for `ok:<command>` after the main loop applies the setting. Errors are `error:invalid`, `error:audio` (playback unavailable), `error:size`, or `error:busy` (queue full; retry). State is updated before success is reported. A `changed` event means the state changed through BLE, Wi-Fi, USB, or audio readiness; read State again. State JSON is a long GATT read, rather than a potentially truncated notification. Event messages are at most 20 bytes.

BLE callbacks only enqueue commands; the main loop validates and applies them through the same control path as HTTP and USB. The audio task receives complete settings snapshots and continues independently of BLE connections.

## Side buttons

- **KEY1** toggles the surround LEDs on/off, preserving the selected colour and brightness.
- **KEY2** toggles white noise. If sound is stopped, it selects white noise and starts playback. If ambient sound is playing, it switches to white noise. If white noise is already playing, it stops playback.

- **KEY3** switches between white noise and ambient audio, preserving the current playing/stopped state, volume, and ambient palette. During playback, the sound changes immediately; while stopped, it changes the selected mode.

The buttons are active-low inputs on the TCA9555: KEY1 = EXIO9 (port 1 bit 1), KEY2 = EXIO10 (port 1 bit 2), KEY3 = EXIO11 (port 1 bit 3). RESET and BOOT retain their hardware functions.

Inputs are polled every 10 ms and must remain stable for 40 ms. Each press toggles once; holding a button does not repeat. A button held during startup or I2C recovery must be released before another press can trigger it. Failed I2C reads are ignored.

Button changes use the same state and save path as Wi-Fi, BLE, and USB, so the web page and BLE state update together. White-noise playback still starts stopped after a reboot. The light button remains usable if the audio codec fails to initialize; the noise button reports audio unavailable in the USB log.

## Hardware mapping

These assignments come from the [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-AUDIO-Board/ESP32-S3-AUDIO-Board_1.1.pdf), [documentation](https://docs.waveshare.com/ESP32-S3-AUDIO-Board), and [factory demo](https://files.waveshare.com/wiki/ESP32-S3-AUDIO-Board/ESP32-S3-AUDIO-Board-Demo.zip).

| Function | Connection |
| --- | --- |
| I2C SDA / SCL | GPIO11 / GPIO10 |
| ES8311 codec | I2C address `0x18` |
| I2S MCLK / BCLK / LRCLK | GPIO12 / GPIO13 / GPIO14 |
| Speaker data output | GPIO16 |
| Amplifier enable | TCA9555 address `0x20`, EXIO8 (port 1 bit 0), active high |
| RGB ring | GPIO38, seven WS2812-compatible pixels, RGB order |
| KEY1 / KEY2 / KEY3 | TCA9555 EXIO9 / EXIO10 / EXIO11, active low |
| Flash / PSRAM | 16 MB / 8 MB octal |

The local `lib/es8311` driver is included from Waveshare's Arduino demo, with its original Espressif Apache-2.0 notices. Its I2C read helper has one fix to reject short reads.

If red and green appear swapped on your board revision, change `RING_PIXEL_ORDER` in `include/config.h` from `NEO_RGB` to `NEO_GRB`, then rebuild and upload.

The microphone, SD slot, RTC, and display are not used by this firmware.

## API

`GET /api/state` returns the current settings and audio availability:

```json
{"playing":false,"volume":20,"mode":"ambient","palette":"water","density":35,"space":65,"warmth":65,"lights":true,"brightness":100,"colour":"#ff4d00","audioReady":true}
```

`POST /api/state` accepts any subset of those settings as URL-encoded form fields, except the read-only `audioReady` field:

```sh
curl -X POST http://192.168.4.1/api/state \
  --data 'playing=true&mode=ambient&palette=moss&density=45&space=80&warmth=60&volume=20&lights=true&brightness=15' \
  --data-urlencode 'colour=#ffb46b'
```

Volume, density, space, warmth, and brightness must be integers from 0 to 100; `mode` must be `ambient` or `white`; `palette` must be `water`, `moss`, or `dusk`; booleans must be `true` or `false`; colour must be `#RRGGBB`. Invalid requests return HTTP 400 without changing settings. Starting playback with unavailable audio returns HTTP 503. Changes are persisted after two seconds without another update; playback state is never restored on reboot.

## Validation and hardware checks

```sh
pio run
node --test tests/web_ui.test.mjs
mkdir -p /tmp/sound-machine-tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ilib/ambient/src lib/ambient/src/AmbientEngine.cpp tests/ambient_test.cpp \
  -o /tmp/sound-machine-tests/ambient-test
ASAN_OPTIONS=detect_leaks=0 /tmp/sound-machine-tests/ambient-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ilib/ambient/src -Ilib/device_control/src tests/device_controls_test.cpp \
  -o /tmp/sound-machine-tests/device-controls-test
ASAN_OPTIONS=detect_leaks=0 /tmp/sound-machine-tests/device-controls-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ilib/ambient/src -Ilib/device_control/src tests/button_controls_test.cpp \
  -o /tmp/sound-machine-tests/button-controls-test
ASAN_OPTIONS=detect_leaks=0 /tmp/sound-machine-tests/button-controls-test
```

The firmware builds successfully. Seven tests exercise the embedded JavaScript with a simulated DOM and device API, including palette / texture updates, mode switching, independent lighting, serialized slider updates, and reconnect behavior. The portable engine tests cover strict parameter rejection, deterministic synthesis, 180 seconds of maximum-density output across all palettes, bounded output, stop, and restart under AddressSanitizer and UndefinedBehaviorSanitizer. The mobile page layout was also checked in headless Chrome with a simulated API.

The firmware was flashed to the connected board and exercised through USB serial: all three palettes at maximum density / space, white-noise switching, stop, and invalid parameter rejection. No render overruns were recorded; the maximum measured block-render time was 4,479 microseconds against an 8,000-microsecond budget. This verifies processing and I2S operation; listening quality still requires the connected speaker.

BLE was also tested over a live connection from the development computer: discovery, full JSON state reads, acknowledged writes for every sound and LED field, invalid command rejection (including embedded NUL), USB-to-BLE state synchronization, and disconnect / reconnect. Audio remained ready with zero render overruns during BLE control at maximum ambient density / space. Host tests separately cover shared command framing, value boundaries, and rejection without state mutation.

USB diagnostics on the connected board identified DAC mute register `0x31 = 0x60` despite successful initialization and ongoing I2S writes. Initialization now explicitly calls `es8311_voice_mute(codec, false)` before enabling the amplifier. Audible playback still needs confirmation through the speaker. After flashing, verify:

1. The direct-connect Wi-Fi network and page are accessible.
2. Ambient start/stop, all three palettes, density, space, warmth, volume, and switching to/from white noise work through the connected speaker, including while changing LED settings.
3. Red, green, and blue selections match the LEDs; brightness and lighting off work independently of sound.
4. After waiting at least two seconds, reset the board. Sound and lighting settings should return, with sound stopped.
5. If you configure home Wi-Fi, verify both normal connection and boot-time fallback with that network unavailable.
6. Press KEY1 twice to check LED on/off, and KEY2 twice to check white-noise start/stop. Press KEY3 during playback to check white-noise / ambient switching, and while stopped to check that it changes the mode without starting playback. Hold each button to confirm it triggers once, then verify the changes appear in the Wi-Fi page and BLE state.

If audio initialization fails, the page keeps its lighting controls available and displays an audio error. Check the USB serial log for the codec / I2C error and verify the exact board model. An error during ongoing I2S output stops playback and requires a restart.

Send `d` or `status`, followed by a newline, in the USB serial monitor to print playback state, the number of audio blocks written, and codec / expander registers. With audio initialized, codec register `31` should read `00` (unmuted), register `32` should read `bf` (0 dB DAC gain), and expander registers `03` / `07` should read `01` / `fe` (amplifier enabled). The audio block count advances even while stopped because the task streams silence when playback is off.


USB serial also accepts `play`, `stop`, `mode ambient|white`, `palette water|moss|dusk`, and `volume|density|space|warmth 0..100` (one command per line). These update the same saved settings as the Wi-Fi page. Diagnostics include maximum block-render time and render-overrun count; each 256-frame block has an 8 ms budget at 32 kHz.

The ambient synthesis engine is included in `lib/ambient`, copied from the referenced local project. This integration brings its sound palettes and synthesis controls into the Wi-Fi and BLE controls and LED page. MQTT is not configured in this firmware.

Button host tests cover contact bounce, held presses, release/re-press, startup and I2C recovery, millis rollover, and white-noise selection from stopped or ambient playback. Physical button actuation still needs checking on the speaker.
