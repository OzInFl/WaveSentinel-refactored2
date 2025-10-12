Wave Sentinel (Refactored)

ESP32-S3 + CC1101 handheld for Sub-GHz exploration, Flipper-style .sub playback, scanning, and field-friendly RF workflows.

✨ Highlights

Sub-GHz tools (CC1101): quick band presets (315/390/433/868/915 MHz), RSSI meter, RX scanning, OOK/2-FSK profiles, GDO event handling.

.sub playback: load Flipper-style .sub files from SD and transmit (where legal).

Wi-Fi/BLE utilities: basic scanners and device info (model dependent).

Touch UI (LVGL): glove-friendly screens, status bar, dialogs.

Logging & profiles: SD logs; save/recall radio profiles and UI prefs.

Settings persistence: NVS/JSON.

⚠️ Operate only within the laws and spectrum rules for your jurisdiction.

🧩 Hardware

MCU/Display: WT32-SC01 / WT32-SC01-PLUS (ESP32-S3) touchscreen

RF Front-End: TI CC1101 via SPI (CS, GDO0, GDO2)

Storage: microSD (logs, .sub, profiles)

I/O: USB-C power/program; optional speaker/buzzer (I2S)

Pin mappings and board options live in include/config.h (or similar).

🗂️ Project Layout
/src            # app entry, init, loop, UI dispatch
/include        # headers, config, helpers
/lcd            # LVGL / SquareLine UI assets
/DocsAndImages  # diagrams, photos
/platformio.ini # PIO envs & board config

⚙️ Build (PlatformIO)

Install VS Code + PlatformIO.

Open the folder; PIO will parse platformio.ini.

Select the WT32-SC01-PLUS / ESP32-S3 environment.

Connect via USB-C → Upload.

Typical deps: Arduino-ESP32 core, LVGL, SPI, SD, CC1101 helper libs.

💾 SD Card Layout
/sub/        # Flipper-style .sub files
/logs/       # CSV/JSON logs
/profiles/   # optional radio profiles/presets

🔧 Configuration

Pins & board: SPI (MOSI/MISO/SCK/CS), GDO0/GDO2, backlight, touch IRQ → config.h.

Region/band limits: set legal bands, max deviation/data-rate, duty-cycle → config.h / profiles/.

UI theme: fonts, colors, and screens → lcd/.

📖 Function Reference (from src/main.cpp)

Replace this section with your actual functions using the auto-index snippet below. The list below shows the expected categories you’ll see:

App lifecycle

setup() – initialize logging/NVS, display & LVGL, touch, SPI/SD, CC1101; load settings & profiles; build initial UI.

loop() – LVGL timers, input dispatch, scheduled radio/UI tasks, periodic RSSI/status updates.

Radio / CC1101

radioInit() – SPI begin, CC1101 reset, default register set, verify chip.

setFrequency(uint32_t hz) – tune carrier; update synth/channel spacing.

setModulationOok() / setModulation2FSK() – apply modulation profiles.

setDataRate(float bps), setDeviation(float khz), setBandwidth(float khz) – PHY helpers.

enterRx() / exitRx() – RX state machine.

rssiRead() – read/convert RSSI (dBm).

txBuffer(const uint8_t* buf, size_t len) / txPacket(...) – transmit path & FIFO handling.

Scanning & meters

startRssiScan(uint32_t startHz, uint32_t stopHz, uint32_t stepHz) – sweep planner.

scanTick() – per-step tune→settle→RSSI→log→advance.

stopScan() – finalize; write summary.

.sub playback

loadSubFile(const char* path) – parse .sub headers/frames.

playSub() / stopSub() – playback lifecycle (freq/phy per frame).

playSubFrame(const SubFrame& f) – set radio state and timings for each frame/edge.

Storage / settings / logging

loadSettings() / saveSettings() – NVS or JSON.

logEvent(const char* msg) / logRssiSample(uint32_t hz, int dbm) – append logs under /logs/.

Wi-Fi utilities

wifiScanStart() / wifiScanCollect() – passive 2.4 GHz scan; populate UI list.

Audio (optional)

audioInitI2S() – set sample rate/format.

audioPlayWav(const char* path) – simple WAV playback from SD.

UI (LVGL)

buildHomeScreen() / buildRadioScreen() / buildPlaybackScreen() / buildScanScreen() – screen constructors.

updateStatusBar(int rssi, uint32_t hz) – status indicators.

onButtonPlay(), onButtonStop(), onBandPresetX() – LVGL event callbacks.

toast(const char* msg) – user feedback.

Utilities

formatFrequency(uint32_t hz) → "433.920 MHz".

formatRssi(int dbm) → "-62 dBm".

millisElapsed(uint32_t since) – timing helper.

🧪 Auto-generate the Function Index (copy-paste)

Run one of these from the repo root to extract a tidy list of function signatures from src/main.cpp and drop it into this README:

Using ripgrep + sed (Linux/macOS):

rg -n '^[[:space:]]*(?:static[[:space:]]+)?(?:inline[[:space:]]+)?[A-Za-z_][A-Za-z0-9_:<>*&[:space:]]+[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\\([^;]*\\)[[:space:]]*\\{' src/main.cpp \
| sed -E 's/^([^:]+):[0-9]+:/- `\1:` /; s/[[:space:]]*\\{$/`/; s/src\\/main.cpp://'


ctags (portable):

ctags -x --c-kinds=f src/main.cpp | awk '{print "- `" $1 "` (" $5 ":" $3 ")"}'


VS Code task: install “Symbol Outline”, open src/main.cpp, copy the Functions list and paste here.

Tip: keep the Function Reference short—group helpers under the feature they support (Radio, Scan, Playback, UI), not alphabetically.

🧭 Roadmap

Dual CC1101 (diversity / TX-select)

More protocol presets & analyzers

Favorites & fuzzy search for .sub

OTA updates (Wi-Fi)

⚖️ Legal

This project is for learning and lawful RF testing. Respect band limits, modulation, duty-cycle and EIRP rules. Transmit only where permitted.

🙏 Credits

LVGL / SquareLine Studio

TI CC1101 community

Contributors & testers

h_Rat
