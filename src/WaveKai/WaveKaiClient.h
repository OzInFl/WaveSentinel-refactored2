/*
 * WaveKai API Client for WaveSentinel
 * Sends captured signals to the WaveKai backend for KeeLoq cracking
 * and rolling code generation.
 */
#ifndef WAVEKAI_CLIENT_H
#define WAVEKAI_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <Update.h>

#include "Misc/Config.h"
#define _WK_STR(x) #x
#define WK_STR(x) _WK_STR(x)
#define WAVEKAI_FW_VERSION WK_STR(APP_VERSION_MAJOR) "." WK_STR(APP_VERSION_MINOR) "." WK_STR(APP_VERSION_PATCH)
// OTA checks version and downloads from our HTTP server (no SSL needed for ESP32)
#define WAVEKAI_FW_VERSION_URL "http://3.224.236.50/ota/version.json"
#define WAVEKAI_FW_OTA_URL "http://3.224.236.50/ota/firmware.bin"

// Default WaveKai server (AWS public IP for crm.southeastdatacom.net)
#ifndef WAVEKAI_SERVER
#define WAVEKAI_SERVER "http://3.224.236.50"
#endif

#define WAVEKAI_API_PREFIX "/api/v1"

// Simple ring-buffer log for debug web page
#define WKLOG_MAX 50
static String wkLogLines[WKLOG_MAX];
static int wkLogHead = 0;
static int wkLogCount = 0;

static void wkLog(const String& msg) {
    String ts = String(millis() / 1000) + "s";
    wkLogLines[wkLogHead] = "[" + ts + "] " + msg;
    wkLogHead = (wkLogHead + 1) % WKLOG_MAX;
    if (wkLogCount < WKLOG_MAX) wkLogCount++;
    Serial.println(msg);
}

class WaveKaiClient {
public:
    // Server configuration
    String serverUrl;
    bool connected;
    String lastError;

    // Auth
    String apiToken;
    String username;
    String loginEmail;
    String loginPass;
    int tokenBalance;
    bool isAuthenticated;
    String deviceMac;

    // Debug web server
    WebServer *debugServer = nullptr;

    WaveKaiClient() : serverUrl(WAVEKAI_SERVER), connected(false),
                       tokenBalance(0), isAuthenticated(false) {}

    // Start debug web server on port 8080
    void startDebugServer() {
        if (debugServer) return;
        debugServer = new WebServer(8080);
        setupDebugRoutes();
        debugServer->begin();
        wkLog("Debug server started on port 8080");
    }

    void handleDebugServer() {
        if (debugServer) debugServer->handleClient();
    }

    void setupDebugRoutes() {
        debugServer->on("/", [this]() { handleDebugPage(); });
        debugServer->on("/test", [this]() { handleTestConnection(); });
        debugServer->on("/preset", [this]() { handleSetPreset(); });
        debugServer->on("/freq", [this]() { handleSetFreq(); });
    }

    void handleSetPreset() {
        String preset = debugServer->arg("p");
        extern SubGhz SUBGHZ;
        if (preset == "am650") { SUBGHZ.setPreset(AM650); wkLog("Remote: AM650"); }
        else if (preset == "am270") { SUBGHZ.setPreset(AM270); wkLog("Remote: AM270"); }
        else if (preset == "fm238") { SUBGHZ.setPreset(FM238); wkLog("Remote: FM238"); }
        else if (preset == "fm476") { SUBGHZ.setPreset(FM476); wkLog("Remote: FM476"); }
        debugServer->send(200, "text/plain", "Preset set to " + preset);
    }

    void handleSetFreq() {
        String freq = debugServer->arg("f");
        float f = freq.toFloat();
        if (f >= 300 && f <= 928) {
            extern float CC1101_MHZ;
            CC1101_MHZ = f;
            wkLog("Remote freq: " + String(f) + " MHz");
            debugServer->send(200, "text/plain", "Freq set to " + String(f));
        } else {
            debugServer->send(400, "text/plain", "Invalid freq");
        }
    }

    void handleTestConnection() {
        wkLog("Manual test triggered via web");
        HTTPClient http;
        String url = serverUrl + "/health";
        wkLog("Testing: " + url);
        http.begin(url);
        http.setTimeout(10000);
        int code = http.GET();
        String body = (code > 0) ? http.getString() : http.errorToString(code);
        http.end();
        wkLog("Result: HTTP " + String(code) + " - " + body);
        debugServer->send(200, "text/plain", "HTTP " + String(code) + "\n" + body);
    }

    void handleDebugPage() {
        String html = "<!DOCTYPE html><html><head><title>WaveSentinel Debug</title>";
        html += "<meta http-equiv='refresh' content='5'>";
        html += "<style>body{background:#0a0a0f;color:#e2e8f0;font-family:monospace;padding:20px;}";
        html += "h1{color:#00ff88;}table{border-collapse:collapse;width:100%;}";
        html += "td{padding:4px 12px;border-bottom:1px solid #1a1a2e;}";
        html += ".ok{color:#00ff88;}.err{color:#ff4444;}.log{color:#94a3b8;font-size:13px;}";
        html += "a{color:#6366f1;}</style></head><body>";
        html += "<h1>WaveSentinel Debug</h1>";

        // Status table
        html += "<table>";
        html += "<tr><td>WiFi</td><td class='" + String(WiFi.status() == WL_CONNECTED ? "ok" : "err") + "'>";
        if (WiFi.status() == WL_CONNECTED) {
            html += WiFi.SSID() + " — " + WiFi.localIP().toString();
            html += " (DNS: " + WiFi.dnsIP().toString() + ")";
        } else {
            html += "Not connected (status=" + String(WiFi.status()) + ")";
        }
        html += "</td></tr>";
        html += "<tr><td>Server URL</td><td>" + serverUrl + "</td></tr>";
        html += "<tr><td>Server connected</td><td class='" + String(connected ? "ok" : "err") + "'>" + String(connected ? "Yes" : "No") + "</td></tr>";
        html += "<tr><td>Last error</td><td class='err'>" + lastError + "</td></tr>";
        html += "<tr><td>Authenticated</td><td>" + String(isAuthenticated ? "Yes (" + username + ")" : "No") + "</td></tr>";
        html += "<tr><td>Token balance</td><td>" + String(tokenBalance) + "</td></tr>";
        html += "<tr><td>Device MAC</td><td>" + deviceMac + "</td></tr>";
        html += "<tr><td>API token</td><td>" + (apiToken.length() > 0 ? apiToken.substring(0, 8) + "..." : "none") + "</td></tr>";
        html += "<tr><td>Free heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
        html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " seconds</td></tr>";
        html += "</table>";

        // Action buttons
        html += "<p><a href='/test'>[Test Server]</a> ";
        html += "<a href='/preset?p=am650'>[AM650]</a> ";
        html += "<a href='/preset?p=am270'>[AM270]</a> ";
        html += "<a href='/preset?p=fm238'>[FM238]</a> ";
        html += "<a href='/preset?p=fm476'>[FM476]</a></p>";
        html += "<p><a href='/freq?f=433.92'>[433.92]</a> ";
        html += "<a href='/freq?f=433.42'>[433.42]</a> ";
        html += "<a href='/freq?f=315.0'>[315.0]</a> ";
        html += "<a href='/freq?f=868.35'>[868.35]</a> ";
        html += "<a href='/freq?f=300.0'>[300.0]</a></p>";

        // Log
        html += "<h2>Log</h2><div class='log'>";
        int start = (wkLogCount < WKLOG_MAX) ? 0 : wkLogHead;
        for (int i = wkLogCount - 1; i >= 0; i--) {
            int idx = (start + i) % WKLOG_MAX;
            html += wkLogLines[idx] + "<br>";
        }
        html += "</div></body></html>";
        debugServer->send(200, "text/html", html);
    }

    // Save server URL + auth to NVS
    void saveConfig() {
        Preferences prefs;
        prefs.begin("wavekai", false);
        prefs.putString("server", serverUrl);
        prefs.putString("apitoken", apiToken);
        prefs.putString("username", username);
        prefs.putString("loginemail", loginEmail);
        prefs.putString("loginpass", loginPass);
        prefs.putInt("balance", tokenBalance);
        prefs.putBool("autoconnect", true);
        prefs.end();
        Serial.printf("[WaveKai] Config saved: %s user=%s tokens=%d\n", serverUrl.c_str(), username.c_str(), tokenBalance);
    }

    // Load config from NVS
    void loadConfig() {
        Preferences prefs;
        prefs.begin("wavekai", false);  // read-write to fix stale URLs
        serverUrl = prefs.getString("server", WAVEKAI_SERVER);

        // One-time migration: fix old hardcoded LAN addresses
        int migVer = prefs.getInt("migver", 0);
        if (migVer < 2) {
            // v2 migration: force server to public IP
            wkLog("Migration v2: " + serverUrl + " -> " + String(WAVEKAI_SERVER));
            serverUrl = WAVEKAI_SERVER;
            prefs.putString("server", serverUrl);
            prefs.putInt("migver", 2);
        }

        apiToken = prefs.getString("apitoken", "");
        username = prefs.getString("username", "");
        loginEmail = prefs.getString("loginemail", "");
        loginPass = prefs.getString("loginpass", "");
        tokenBalance = prefs.getInt("balance", 0);
        prefs.end();

        isAuthenticated = (apiToken.length() == 64);
        wkLog("Config loaded: server=" + serverUrl);
        if (isAuthenticated) {
            wkLog("Restored session: " + username + " tokens=" + String(tokenBalance));
        }

        // MAC will be fetched when needed (after WiFi is connected)
        updateMac();
        wkLog("Device MAC: " + deviceMac);
    }

    // Add auth headers to HTTP request
    void addAuthHeaders(HTTPClient& http) {
        http.addHeader("Content-Type", "application/json");
        if (apiToken.length() > 0) {
            http.addHeader("Authorization", "Bearer " + apiToken);
        }
        if (deviceMac.length() > 0) {
            http.addHeader("X-Device-MAC", deviceMac);
        }
    }

    // Login with email/password
    bool login(const String& email, const String& password) {
        if (WiFi.status() != WL_CONNECTED) {
            lastError = "WiFi not connected";
            return false;
        }

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/auth/login";
        wkLog("Login URL: " + url);
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000);

        StaticJsonDocument<256> doc;
        doc["login"] = email;
        doc["password"] = password;
        String body;
        serializeJson(doc, body);

        int httpCode = http.POST(body);
        wkLog("Login response: HTTP " + String(httpCode));
        if (httpCode < 0) {
            wkLog("HTTP error detail: " + http.errorToString(httpCode));
        }
        if (httpCode == 200) {
            String response = http.getString();
            StaticJsonDocument<1024> respDoc;
            if (!deserializeJson(respDoc, response)) {
                apiToken = respDoc["user"]["api_token"].as<String>();
                username = respDoc["user"]["username"].as<String>();
                tokenBalance = respDoc["user"]["token_balance"] | 0;
                loginEmail = email;
                loginPass = password;
                isAuthenticated = true;
                saveConfig();
                Serial.printf("[WaveKai] Logged in as %s, tokens=%d\n", username.c_str(), tokenBalance);
                http.end();
                return true;
            }
        } else {
            String resp = http.getString();
            StaticJsonDocument<256> errDoc;
            if (!deserializeJson(errDoc, resp)) {
                lastError = errDoc["detail"].as<String>();
            } else {
                lastError = "HTTP " + String(httpCode);
            }
        }
        http.end();
        return false;
    }

    // Register device MAC with server
    bool registerDevice() {
        if (!isAuthenticated) {
            lastError = "Not logged in";
            Serial.println("[WaveKai] Register failed: not authenticated");
            return false;
        }
        if (WiFi.status() != WL_CONNECTED) {
            lastError = "WiFi not connected";
            Serial.println("[WaveKai] Register failed: no WiFi");
            return false;
        }

        // Refresh MAC address now that WiFi is connected
        updateMac();

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/devices/register";
        Serial.printf("[WaveKai] Registering device %s at %s\n", deviceMac.c_str(), url.c_str());
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(5000);

        StaticJsonDocument<256> doc;
        doc["mac_address"] = deviceMac;
        doc["device_name"] = "WaveSentinel";
        String body;
        serializeJson(doc, body);
        Serial.printf("[WaveKai] POST body: %s\n", body.c_str());

        int httpCode = http.POST(body);
        if (httpCode != 200) {
            String resp = http.getString();
            Serial.printf("[WaveKai] Device register failed: HTTP %d - %s\n", httpCode, resp.c_str());
            StaticJsonDocument<256> errDoc;
            if (!deserializeJson(errDoc, resp)) {
                lastError = errDoc["detail"].as<String>();
            } else {
                lastError = "HTTP " + String(httpCode);
            }
            http.end();
            return false;
        }
        Serial.println("[WaveKai] Device registered successfully!");
        http.end();
        return true;
    }

    // Refresh token balance from server
    bool refreshBalance() {
        if (!isAuthenticated || WiFi.status() != WL_CONNECTED) return false;

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/billing/balance";
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(3000);

        int httpCode = http.GET();
        if (httpCode == 200) {
            String response = http.getString();
            StaticJsonDocument<256> doc;
            if (!deserializeJson(doc, response)) {
                tokenBalance = doc["token_balance"] | 0;
                // Persist updated balance
                Preferences prefs;
                prefs.begin("wavekai", false);
                prefs.putInt("balance", tokenBalance);
                prefs.end();
            }
            http.end();
            return true;
        }
        http.end();
        return false;
    }

    // Logout — clear credentials
    void logout() {
        apiToken = "";
        username = "";
        tokenBalance = 0;
        isAuthenticated = false;
        Preferences prefs;
        prefs.begin("wavekai", false);
        prefs.remove("apitoken");
        prefs.remove("username");
        prefs.end();
        Serial.println("[WaveKai] Logged out");
    }

    // Get/refresh MAC address
    void updateMac() {
        // Try WiFi MAC first (reliable when WiFi is on)
        if (WiFi.status() == WL_CONNECTED || WiFi.getMode() != WIFI_OFF) {
            deviceMac = WiFi.macAddress();
        }
        // Fallback to eFuse MAC
        if (deviceMac.length() != 17 || deviceMac == "00:00:00:00:00:00") {
            uint8_t mac[6];
            esp_efuse_mac_get_default(mac);
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            deviceMac = String(macStr);
        }
    }

    void setServer(const String& url) {
        serverUrl = url;
        if (serverUrl.endsWith("/")) {
            serverUrl = serverUrl.substring(0, serverUrl.length() - 1);
        }
    }

    // Check if WaveKai server is reachable
    bool checkConnection() {
        if (WiFi.status() != WL_CONNECTED) {
            lastError = "WiFi not connected";
            connected = false;
            wkLog("checkConnection: WiFi not connected");
            return false;
        }

        HTTPClient http;
        String url = serverUrl + "/health";
        wkLog("checkConnection: " + url);
        wkLog("WiFi IP: " + WiFi.localIP().toString() + " DNS: " + WiFi.dnsIP().toString() + " GW: " + WiFi.gatewayIP().toString());
        http.begin(url);
        http.setTimeout(10000);
        int code = http.GET();
        if (code < 0) {
            wkLog("checkConnection error: " + http.errorToString(code));
        } else {
            wkLog("checkConnection: HTTP " + String(code));
        }
        http.end();

        connected = (code == 200);
        if (!connected) {
            lastError = "Server unreachable (HTTP " + String(code) + ")";
        }
        return connected;
    }

    // Upload captured raw timings and get crack results
    // sample[] = array of pulse durations in microseconds
    // samplecount = number of samples
    // frequency = capture frequency in MHz
    struct CrackResult {
        bool success;
        bool found;
        String manufacturer;
        String serial;
        int counter;
        int button;
        String derivedKey;
        String method;
        String rawHex;
        String error;
    };

    // ---------------------------------------------------------------
    // KeeLoq decoder — Flipper Zero algorithm (PWM state machine)
    // Returns 64-bit code word, or 0 if no valid frame found.
    //
    // KeeLoq PWM encoding:
    //   Bit 1 = short HIGH (~400us) + long LOW (~800us)
    //   Bit 0 = long  HIGH (~800us) + short LOW (~400us)
    //
    // Preamble: 3+ short pulses then ~4000us gap
    // Frame: 64-66 bits, MSB first
    // ---------------------------------------------------------------
    uint64_t decodeKeeLoq(int* samples, int count) {
        const int TE_SHORT = 400;
        const int TE_LONG  = 800;
        const int TE_DELTA = 180;  // Unleashed tolerance

        enum { RESET, CHECK_PREAMBLE, SAVE_DUR, CHECK_DUR };
        int step = RESET;
        int headerCount = 0;
        int teLast = 0;
        uint64_t decodeData = 0;
        int decodeBits = 0;

        // samples[] alternates: HIGH, LOW, HIGH, LOW...
        // sample[0] is unreliable, start at 1
        for (int i = 1; i < count; i++) {
            int duration = samples[i];
            bool isHigh = (i % 2 == 1);  // odd indices = HIGH

            switch (step) {
            case RESET:
                if (isHigh && abs(duration - TE_SHORT) < TE_DELTA) {
                    headerCount++;
                    step = CHECK_PREAMBLE;
                } else {
                    headerCount = 0;
                }
                break;

            case CHECK_PREAMBLE:
                if (!isHigh) {
                    if (abs(duration - TE_SHORT) < TE_DELTA) {
                        // Still preamble — go back for next HIGH
                        step = RESET;
                    } else if (headerCount > 2 &&
                               abs(duration - TE_SHORT * 10) < TE_DELTA * 10) {
                        // Preamble gap detected — start data capture
                        decodeData = 0;
                        decodeBits = 0;
                        step = SAVE_DUR;
                    } else {
                        headerCount = 0;
                        step = RESET;
                    }
                } else {
                    headerCount = 0;
                    step = RESET;
                }
                break;

            case SAVE_DUR:
                if (isHigh) {
                    teLast = duration;
                    step = CHECK_DUR;
                } else {
                    step = RESET;
                    headerCount = 0;
                }
                break;

            case CHECK_DUR:
                if (!isHigh) {
                    // End-of-frame gap?
                    if (duration >= TE_SHORT * 2 + TE_DELTA) {
                        if (decodeBits >= 64 && decodeBits <= 66) {
                            Serial.printf("[WaveKai] KeeLoq frame: %d bits, code=0x%016llX\n",
                                          decodeBits, decodeData);
                            return decodeData;
                        }
                        // Not enough bits — reset
                        headerCount = 0;
                        step = RESET;
                    }
                    // Bit 1: short HIGH + long LOW
                    else if (abs(teLast - TE_SHORT) < TE_DELTA &&
                             abs(duration - TE_LONG) < TE_DELTA * 2) {
                        if (decodeBits < 64) {
                            decodeData = (decodeData << 1) | 1;
                        }
                        decodeBits++;
                        step = SAVE_DUR;
                    }
                    // Bit 0: long HIGH + short LOW
                    else if (abs(teLast - TE_LONG) < TE_DELTA * 2 &&
                             abs(duration - TE_SHORT) < TE_DELTA) {
                        if (decodeBits < 64) {
                            decodeData = (decodeData << 1) | 0;
                        }
                        decodeBits++;
                        step = SAVE_DUR;
                    }
                    // Invalid timing
                    else {
                        headerCount = 0;
                        step = RESET;
                    }
                } else {
                    headerCount = 0;
                    step = RESET;
                }
                break;
            }
        }

        // Check if we ended with a valid frame (no trailing gap)
        if (decodeBits >= 64 && decodeBits <= 66) {
            Serial.printf("[WaveKai] KeeLoq frame (no gap): %d bits, code=0x%016llX\n",
                          decodeBits, decodeData);
            return decodeData;
        }

        return 0;
    }

    // Fallback: simple timing-to-hex for non-KeeLoq protocols
    String timingsToHex(int* samples, int count, int shortPulseUs = 400) {
        if (count < 4) return "";

        int minPulse = 999999;
        for (int i = 1; i < count; i++) {
            if (samples[i] > 50 && samples[i] < minPulse) {
                minPulse = samples[i];
            }
        }

        int threshold = minPulse * 2;
        String bits = "";

        for (int i = 1; i < count; i++) {
            if (samples[i] < 50 || samples[i] > minPulse * 6) continue;
            bits += (samples[i] < threshold) ? "0" : "1";
        }

        if (bits.length() < 8) return "";
        while (bits.length() % 4 != 0) bits += "0";

        String hex = "";
        for (int i = 0; i < (int)bits.length(); i += 4) {
            int nibble = 0;
            for (int j = 0; j < 4; j++) {
                nibble = (nibble << 1) | (bits[i + j] == '1' ? 1 : 0);
            }
            hex += String(nibble, HEX);
        }
        hex.toUpperCase();
        return hex;
    }

    // Send raw timing samples to WaveKai for multi-protocol analysis
    // Server runs all decoders (KeeLoq, Princeton, CAME, Security+, etc.)
    CrackResult crackSignal(int* samples, int sampleCount, float frequencyMhz = 433.92) {
        CrackResult result;
        result.success = false;
        result.found = false;

        if (WiFi.status() != WL_CONNECTED) {
            result.error = "WiFi not connected";
            return result;
        }

        wkLog("Sending " + String(sampleCount) + " samples to /analyze at " + String(frequencyMhz) + " MHz");

        // Log first few samples for debug
        String samplePreview = "Samples[0..9]: ";
        for (int i = 0; i < min(10, sampleCount); i++) {
            samplePreview += String(samples[i]) + " ";
        }
        wkLog(samplePreview);

        // Build JSON with raw timing samples
        // Use DynamicJsonDocument for large sample arrays
        size_t jsonSize = 128 + sampleCount * 8;  // ~8 chars per sample
        DynamicJsonDocument doc(jsonSize);
        JsonArray arr = doc.createNestedArray("samples");
        for (int i = 0; i < sampleCount; i++) {
            arr.add(samples[i]);
        }
        doc["frequency"] = frequencyMhz;
        doc["name"] = "WaveSentinel_Capture";
        doc["source_device"] = String("WaveSentinel/v") + WAVEKAI_FW_VERSION;

        String body;
        serializeJson(doc, body);
        wkLog("JSON payload size: " + String(body.length()) + " bytes");

        // Send to /analyze endpoint
        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/signals/analyze";
        wkLog("POST " + url);
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(15000);

        int httpCode = http.POST(body);
        wkLog("Response: HTTP " + String(httpCode));

        if (httpCode == 401) {
            result.error = "Not authenticated. Login in Account tab.";
            http.end();
            return result;
        }
        if (httpCode == 402) {
            result.error = "Insufficient tokens. Purchase more.";
            http.end();
            return result;
        }

        if (httpCode == 200) {
            String response = http.getString();
            wkLog("Response body (" + String(response.length()) + " chars): " + response.substring(0, 300));
            DynamicJsonDocument respDoc(4096);
            DeserializationError err = deserializeJson(respDoc, response);

            if (!err) {
                result.success = true;
                JsonArray results = respDoc["results"];
                wkLog("Results count: " + String(results.size()));
                if (results.size() > 0) {
                    JsonObject best = results[0];
                    result.found = best["found"] | false;
                    result.rawHex = best["raw_hex"].as<String>();
                    result.method = best["protocol"].as<String>();

                    wkLog("Best: protocol=" + result.method + " found=" + String(result.found) + " hex=" + result.rawHex);

                    if (result.found) {
                        result.manufacturer = best["manufacturer"].as<String>();
                        if (best.containsKey("serial") && !best["serial"].isNull()) {
                            result.serial = String(best["serial"].as<long>(), HEX);
                        }
                        result.counter = best["counter"] | 0;
                        result.button = best["button"] | 0;
                        result.derivedKey = best["key"].as<String>();
                    } else {
                        // Protocol identified but not cracked
                        result.manufacturer = best["protocol"].as<String>();
                        if (best.containsKey("serial") && !best["serial"].isNull()) {
                            result.serial = String(best["serial"].as<long>(), HEX);
                        }
                    }
                } else {
                    wkLog("No protocol match in results");
                }
            } else {
                wkLog("JSON parse error: " + String(err.c_str()));
                result.error = "JSON parse error";
            }
        } else {
            wkLog("API error: HTTP " + String(httpCode));
            if (httpCode > 0) {
                String errBody = http.getString();
                wkLog("Error body: " + errBody.substring(0, 200));
            }
            result.error = "HTTP " + String(httpCode);
        }

        http.end();
        return result;
    }

    // Upload signal for storage
    bool uploadSignal(int* samples, int sampleCount, float frequencyMhz,
                      const String& name, const String& protocol = "") {
        if (WiFi.status() != WL_CONNECTED) return false;

        String hex = timingsToHex(samples, sampleCount);
        if (hex.length() == 0) return false;

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/signals/upload";
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(5000);

        StaticJsonDocument<512> doc;
        doc["raw_hex"] = hex;
        doc["bit_length"] = hex.length() * 4;
        doc["frequency"] = frequencyMhz;
        doc["modulation"] = "ASK/OOK";
        doc["name"] = name;
        doc["source_device"] = String("WaveSentinel/v") + WAVEKAI_FW_VERSION;

        String body;
        serializeJson(doc, body);

        int httpCode = http.POST(body);
        http.end();

        return (httpCode == 200);
    }

    // Get list of manufacturer keys
    String getManufacturerKeys() {
        if (WiFi.status() != WL_CONNECTED) return "[]";

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/signals/keys";
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(5000);

        int httpCode = http.GET();
        String result = "[]";
        if (httpCode == 200) {
            result = http.getString();
        }
        http.end();
        return result;
    }

    // Generate rolling codes from cracked key
    String generateCodes(uint32_t serial, const String& keyHex,
                         int startCounter, int button = 1, int count = 5) {
        if (WiFi.status() != WL_CONNECTED) return "{}";

        HTTPClient http;
        String url = serverUrl + WAVEKAI_API_PREFIX + "/signals/generate";
        http.begin(url);
        addAuthHeaders(http);
        http.setTimeout(5000);

        StaticJsonDocument<256> doc;
        doc["serial"] = serial;
        doc["key_hex"] = keyHex;
        doc["start_counter"] = startCounter;
        doc["button"] = button;
        doc["count"] = count;

        String body;
        serializeJson(doc, body);

        int httpCode = http.POST(body);
        String result = "{}";
        if (httpCode == 200) {
            result = http.getString();
        }
        http.end();
        return result;
    }
    // ---------------------------------------------------------------
    // OTA Firmware Update
    // ---------------------------------------------------------------
    String latestVersion;
    String latestBinUrl;
    bool updateAvailable = false;

    bool checkForUpdate() {
        if (WiFi.status() != WL_CONNECTED) {
            lastError = "WiFi not connected";
            return false;
        }
        wkLog("Checking for updates at " + String(WAVEKAI_FW_VERSION_URL));
        HTTPClient http;
        http.begin(WAVEKAI_FW_VERSION_URL);
        http.setTimeout(10000);
        int code = http.GET();
        wkLog("Version check: HTTP " + String(code));
        if (code == 200) {
            String resp = http.getString();
            wkLog("Response: " + resp);
            StaticJsonDocument<256> doc;
            if (!deserializeJson(doc, resp)) {
                latestVersion = doc["version"].as<String>();
                latestBinUrl = WAVEKAI_FW_OTA_URL;
                updateAvailable = (latestVersion.length() > 0 && latestVersion != WAVEKAI_FW_VERSION);
                wkLog("Current: " + String(WAVEKAI_FW_VERSION) + " Latest: " + latestVersion +
                      (updateAvailable ? " UPDATE AVAILABLE!" : " (up to date)"));
                http.end();
                return updateAvailable;
            }
        }
        lastError = "Check failed: HTTP " + String(code);
        wkLog(lastError);
        http.end();
        return false;
    }

    // Progress callback for OTA — set by the UI before calling performOTA
    void (*otaProgressCallback)(int percent, size_t written, size_t total) = nullptr;

    bool performOTA() {
        if (latestBinUrl.length() == 0) {
            lastError = "No update URL";
            return false;
        }
        wkLog("Downloading firmware from " + latestBinUrl);

        HTTPClient http;
        http.begin(latestBinUrl);
        http.setTimeout(120000);
        int code = http.GET();
        if (code != 200) {
            lastError = "Download failed: HTTP " + String(code);
            wkLog(lastError);
            http.end();
            return false;
        }

        int contentLen = http.getSize();
        if (contentLen <= 0) {
            lastError = "Invalid firmware size";
            http.end();
            return false;
        }

        wkLog("Firmware size: " + String(contentLen) + " bytes");

        if (!Update.begin(contentLen)) {
            lastError = "OTA begin failed: " + String(Update.getError());
            wkLog(lastError);
            http.end();
            return false;
        }

        // Read in chunks with progress updates
        WiFiClient* stream = http.getStreamPtr();
        uint8_t buf[4096];
        size_t totalWritten = 0;
        int lastPct = -1;

        while (totalWritten < (size_t)contentLen) {
            size_t available = stream->available();
            if (available == 0) {
                // Wait for data
                int timeout = 0;
                while (!stream->available() && timeout < 30000) {
                    delay(10);
                    timeout += 10;
                }
                if (!stream->available()) {
                    lastError = "Download timeout";
                    Update.abort();
                    http.end();
                    return false;
                }
                continue;
            }

            size_t toRead = min(available, sizeof(buf));
            size_t bytesRead = stream->readBytes(buf, toRead);
            if (bytesRead == 0) break;

            size_t written = Update.write(buf, bytesRead);
            if (written != bytesRead) {
                lastError = "Write error at " + String(totalWritten);
                Update.abort();
                http.end();
                return false;
            }
            totalWritten += written;

            int pct = (int)(totalWritten * 100 / contentLen);
            if (pct != lastPct) {
                lastPct = pct;
                if (otaProgressCallback) {
                    otaProgressCallback(pct, totalWritten, contentLen);
                }
                if (pct % 10 == 0) {
                    wkLog("OTA: " + String(pct) + "% (" + String(totalWritten/1024) + "/" + String(contentLen/1024) + " KB)");
                }
            }
        }

        wkLog("Download complete: " + String(totalWritten) + " bytes");

        if (Update.end()) {
            if (Update.isFinished()) {
                wkLog("OTA SUCCESS! Rebooting...");
                http.end();
                return true;
            } else {
                lastError = "OTA not finished";
            }
        } else {
            lastError = "OTA error: " + String(Update.getError());
        }

        wkLog("OTA failed: " + lastError);
        http.end();
        return false;
    }
};

// Global instance
extern WaveKaiClient waveKai;

#endif // WAVEKAI_CLIENT_H
