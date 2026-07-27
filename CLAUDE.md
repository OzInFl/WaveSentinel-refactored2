# WaveSentinel — Claude Code project guide

ESP32-S3 (UM **ProS3**) RF/security multitool on a **WT32-SC01-PLUS** (ST7796 320×480 LCD,
FT5x06 touch) + **CC1101** sub-GHz module. Arduino framework via PlatformIO, LVGL 8.3 UI on
the LovyanGFX driver. Features: SubGHz capture/replay/scan/analyze, KeeLoq, Tesla, Flipper
`.sub`, Universal Remote (RF+IR), TouchTunes, WiFi Marauder, BLE Marauder, WaveKai, and the
**POCSAG/FLEX Pager** (added on branch `pager-v2`).

## ⚠️ CRITICAL: toolchain is pinned to core 2.x — DO NOT upgrade it
- `platformio.ini` pins **`platform = espressif32@6.4.0`** = arduino-esp32 **2.0.11 / IDF 4.4**.
- Do **NOT** switch to unpinned `espressif32` or the pioarduino fork (core 3.x). Core 3.x breaks:
  - **LovyanGFX 1.1.12** — uses removed IDF4 symbols (`i2c_signal_conn_t::module`,
    `gpio_hal_iomux_func_sel`, `lcd_periph_signals`) → won't compile.
  - **ELECHOUSE/SmartRC CC1101 driver** — its per-op `SPI.begin()/end()` + `digitalWrite()` on
    SPI-owned pins + `while(digitalRead(MISO))` chip-ready spins HANG at boot on core 3.x
    (peripheral-manager conflict) → `SUBGHZ.init()` never returns → blank screen, backlight on.
- Staying on 6.4.0 avoids all of it. (If ever forced to core 3.x: upgrade LovyanGFX to `^1.2.0`
  via lib_deps; patch the CC1101 driver — begin SPI once, no per-op begin/end, `SPI.begin(...,-1)`
  for manual CS, `gpio_get_level()` + timeout for the MISO waits; set `LV_USE_SPINBOX 1`; add
  missing `return` in SubGhz.cpp and `#include <esp_wifi_types.h>` in Display/Utils.h.)

## Repo & branches
- GitHub: `OzInFl/WaveSentinel-refactored2`. `main` = **v2.0.52** ("full feature drop", has
  WaveKai + Marauder). The **pager lives on branch `pager-v2`** (v2.0.52 + pager).
- An old local folder `WaveSentinel-refactored` (v1.0.99) exists on one machine — **obsolete**,
  it predates WaveKai/Marauder. Ignore it; `main`/`pager-v2` here are the source of truth.

## Build & flash
- Build: `pio run -e WaveSentinel`
- **Flashing gotcha (Windows):** CLI `pio run -t upload` reliably writes the small sections then
  **fails on the 3 MB app** over the S3's USB-Serial-JTAG ("Cannot configure port" / "Write
  timeout") — a Windows usbser driver bug on sustained transfer, NOT a code problem.
  - **Reliable path = WebSerial**: the project web flasher (gh-pages) or Espressif esptool-js
    (https://espressif.github.io/esptool-js/). Build a combined image and flash it at `0x0`:
    `esptool merge_bin -o combined.bin --flash_mode keep --flash_freq keep --flash_size 16MB \
      0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin`
    (artifacts in `.pio/build/WaveSentinel/`; boot_app0 in the framework's `tools/partitions/`).
  - Download mode (native-USB S3, no DTR/RTS auto-reset): **hold BOOT while plugging in USB**.
  - Keep `upload_speed = 115200` for native-USB reliability. **Keep CDC-on-boot OFF**
    (`-UARDUINO_USB_CDC_ON_BOOT`) in production — enabling it makes an interrupted flash
    unrecoverable without the BOOT button.

## Pager (POCSAG/FLEX) — `pager-v2`
- **Launch:** CC1101 tools → **PAGER** tab → **OPEN PAGER**. Auto-starts on the seeded system
  **460.6125 MHz / POCSAG Auto** (the user's bench test transmitter).
- **Files:** `src/Pager/{PagerTypes,PocsagDecoder,FlexDecoder,PagerTones,Pager}.h` +
  `src/Display/PagerScreen.h`. Header-only, all pulled in once via `PagerScreen.h` in `main.cpp`
  (same single-TU pattern as `RemoteScreen.h`).
- **Wiring:** `STATE_PAGER` in `Display/Event.h`; `loop()` runs `pager_poll()` +
  `pager_screen_sync()` (under `lvgl_mutex`); `setup()` calls
  `pager_init()/pager_screen_init()/pager_add_launch_tab()`; the PAGER tab is appended to
  `ui_tabCC1101Stuff`.
- **RX path:** CC1101 2-FSK async serial (`setPktFormat(3)`), GDO0 edge-timing ISR → ring buffer
  → software NRZ bit recovery → decoders. No hardware timer.
- **POCSAG:** full — 512/1200/2400 + auto, BCH(31,21) error correction, numeric + alphanumeric.
  Algorithm validated with a Python encode/decode round-trip (incl. 1-bit-error correction).
- **FLEX:** EXPERIMENTAL. Only sync + mode + FIW (cycle/frame). The common 3200/6400 **4-level**
  FLEX cannot be decoded from the CC1101's 1-bit hard-sliced GDO0 output — hardware limit.
- **Alerts:** use v2's **ToneService** (`src/Audio/ToneService.h`) — 5 built-in `ToneSequence`s in
  `PagerTones.h`. (v2.0.52 removed the ESP32-audioI2S `Audio` object; ToneService owns I2S.)
- **Persistence:** NVS namespace `"pager"` — systems[], RIC watchlist (per-RIC alert tone 1–5,
  enable), monitor mode (ALL vs SELECTED), volume 0–100, logging toggle. Log →
  `/pager/logs/pager.log` on SD.

## Pager status / next steps
- Builds clean on core 2.x; flashed & boots on device with WaveKai/Marauder intact.
- **Not yet confirmed decoding a live POCSAG page** on 460.6125. If no pages appear:
  1. First confirm CC1101 RX works at all (Scanner tab picks up signals).
  2. In Pager → SETTINGS → SYSTEMS, set the system's **format to the transmitter's real baud**
     (512/1200/2400) instead of Auto; SAVE.
  3. If the page counter climbs but text is garbled, toggle **invert** on the system.

## Hardware pins (`src/Misc/Config.h`)
- CC1101: MISO 11, MOSI 10, SCLK 14, CS 12, GDO0 13 (default/FSPI SPI)
- SD: MISO 38, MOSI 40, SCLK 39, CS 41 (dedicated HSPI, `SPIClass sdSPI`)
- I2S: BCLK 36, LRC 35, DOUT 37. Display 8-bit parallel, RST 4, BL 45; touch SDA 6, SCL 5.

## Conventions
- Screens are **hand-built in code** (`src/Display/*.h`), not SquareLine — follow
  `RemoteScreen.h` / `PagerScreen.h`.
- LVGL refresh runs on **Core 0** (`Task_Refresh_Screen`); main logic on **Core 1** `loop()`.
  **Every** LVGL call must be wrapped with `lvgl_mutex`.
- The `loop()` state machine dispatches on `currentState` (`Display/Event.h` `WaveSentinelState`).
