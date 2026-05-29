#ifndef LocalAPI_h
#define LocalAPI_h

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Update.h>

// Forward declarations - these are defined in main.cpp / SubGhz / WaveKai
extern float CC1101_MHZ;
extern int CC1101_MODULATION;
extern float CC1101_DRATE;
extern float CC1101_RX_BW;
extern int CC1101_PA;
extern int CC1101_SYNC_MODE;
extern int CC1101_PKT_FORMAT;
extern int sample[];
extern int samplecount;
extern uint8_t currentState;
extern volatile bool wifiGotIP;
extern char wifiLocalIP[];
extern SemaphoreHandle_t lvgl_mutex;

// State constants (from Event.h)
#define API_STATE_IDLE 0
#define API_STATE_CAPTURE 1
#define API_STATE_PLAYBACK 2
#define API_STATE_SCANNER 15

#include "Misc/Config.h"
#define _WS_STR(x) #x
#define WS_STR(x) _WS_STR(x)
#define WAVESENTINEL_FW_VERSION WS_STR(APP_VERSION_MAJOR) "." WS_STR(APP_VERSION_MINOR) "." WS_STR(APP_VERSION_PATCH)
#define API_PORT 80

class LocalAPIServer {
public:
    AsyncWebServer server;
    bool running = false;

    // Scanner data for FFT-like display
    static const int SCAN_BINS = 128;
    int8_t scanRSSI[SCAN_BINS];       // RSSI values per bin
    float scanFreqStart = 433.0;
    float scanFreqStop = 434.0;
    float scanPeakFreq = 0;
    int8_t scanPeakRSSI = -128;
    bool scanRunning = false;

    // Capture state
    bool captureReady = false;
    int capturedSamples = 0;
    float captureFreq = 433.92;

    LocalAPIServer() : server(API_PORT) {}

    void begin() {
        if (!wifiGotIP) return;

        // CORS headers for web app access
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

        // OPTIONS preflight handler
        server.onNotFound([](AsyncWebServerRequest *request) {
            if (request->method() == HTTP_OPTIONS) {
                request->send(200);
            } else {
                request->send(404, "application/json", "{\"error\":\"not found\"}");
            }
        });

        setupRoutes();
        server.begin();
        running = true;
        Serial.printf("[API] Local API started on http://%s:%d\n", wifiLocalIP, API_PORT);
    }

    void setupRoutes() {
        // ============================================================
        // GET /api/status - Device info
        // ============================================================
        server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["device"] = "WaveSentinel";
            doc["version"] = WAVESENTINEL_FW_VERSION;
            doc["uptime_ms"] = millis();
            doc["free_heap"] = ESP.getFreeHeap();
            doc["free_psram"] = ESP.getFreePsram();
            doc["wifi_rssi"] = WiFi.RSSI();
            doc["ip"] = wifiLocalIP;
            doc["mac"] = WiFi.macAddress();
            doc["state"] = currentState;

            // CC1101 config
            JsonObject rf = doc["rf"].to<JsonObject>();
            rf["frequency"] = CC1101_MHZ;
            rf["modulation"] = CC1101_MODULATION;
            rf["data_rate"] = CC1101_DRATE;
            rf["rx_bw"] = CC1101_RX_BW;
            rf["tx_power"] = CC1101_PA;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // POST /api/config - Set CC1101 parameters
        // ============================================================
        server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["frequency"] = CC1101_MHZ;
            doc["modulation"] = CC1101_MODULATION;
            doc["data_rate"] = CC1101_DRATE;
            doc["rx_bw"] = CC1101_RX_BW;
            doc["tx_power"] = CC1101_PA;
            doc["sync_mode"] = CC1101_SYNC_MODE;
            doc["pkt_format"] = CC1101_PKT_FORMAT;
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        server.addHandler(new AsyncCallbackJsonWebHandler("/api/config",
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject obj = json.as<JsonObject>();
                if (obj["frequency"].is<float>()) CC1101_MHZ = obj["frequency"].as<float>();
                if (obj["modulation"].is<int>()) CC1101_MODULATION = obj["modulation"].as<int>();
                if (obj["data_rate"].is<float>()) CC1101_DRATE = obj["data_rate"].as<float>();
                if (obj["rx_bw"].is<float>()) CC1101_RX_BW = obj["rx_bw"].as<float>();
                if (obj["tx_power"].is<int>()) CC1101_PA = obj["tx_power"].as<int>();

                request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
        ));

        // ============================================================
        // POST /api/tx - Transmit a signal
        // Body: { "protocol": "princeton", "key": "0xDEADBE", "frequency": 433.92,
        //         "te": 400, "repeat": 5 }
        // OR raw: { "raw": true, "samples": [400, -800, ...], "frequency": 433.92 }
        // ============================================================
        server.addHandler(new AsyncCallbackJsonWebHandler("/api/tx",
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject obj = json.as<JsonObject>();

                float freq = obj["frequency"] | 433.92f;
                int repeat = obj["repeat"] | 3;

                if (obj["raw"].is<bool>() && obj["raw"].as<bool>()) {
                    // Raw sample transmission
                    JsonArray arr = obj["samples"].as<JsonArray>();
                    if (arr.isNull() || arr.size() < 2) {
                        request->send(400, "application/json", "{\"error\":\"samples array required\"}");
                        return;
                    }
                    // Copy samples to the global buffer
                    samplecount = min((int)arr.size(), 4096);
                    for (int i = 0; i < samplecount; i++) {
                        sample[i] = arr[i].as<int>();
                    }
                    CC1101_MHZ = freq;
                    // Trigger playback via state machine
                    currentState = API_STATE_PLAYBACK;

                    JsonDocument resp;
                    resp["status"] = "transmitting";
                    resp["samples"] = samplecount;
                    resp["frequency"] = freq;
                    String response;
                    serializeJson(resp, response);
                    request->send(200, "application/json", response);
                } else {
                    // Protocol-based transmission
                    String protocol = obj["protocol"] | "princeton";
                    uint32_t key = strtoul(obj["key"] | "0", NULL, 0);
                    int te = obj["te"] | 400;
                    int bits = obj["bits"] | 24;

                    // Generate Princeton-style signal in sample buffer
                    samplecount = 0;
                    for (int r = 0; r < repeat; r++) {
                        for (int i = bits - 1; i >= 0; i--) {
                            if ((key >> i) & 1) {
                                // Bit 1: long HIGH + short LOW
                                sample[samplecount++] = te * 3;
                                sample[samplecount++] = te;
                            } else {
                                // Bit 0: short HIGH + long LOW
                                sample[samplecount++] = te;
                                sample[samplecount++] = te * 3;
                            }
                            if (samplecount >= 4090) break;
                        }
                        // Sync gap
                        sample[samplecount++] = te;
                        sample[samplecount++] = te * 31;
                        if (samplecount >= 4090) break;
                    }

                    CC1101_MHZ = freq;
                    currentState = API_STATE_PLAYBACK;

                    JsonDocument resp;
                    resp["status"] = "transmitting";
                    resp["protocol"] = protocol;
                    resp["key"] = key;
                    resp["bits"] = bits;
                    resp["frequency"] = freq;
                    String response;
                    serializeJson(resp, response);
                    request->send(200, "application/json", response);
                }
            }
        ));

        // ============================================================
        // POST /api/rx/start - Start capture
        // Body: { "frequency": 433.92, "duration_ms": 5000 }
        // ============================================================
        server.addHandler(new AsyncCallbackJsonWebHandler("/api/rx/start",
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject obj = json.as<JsonObject>();
                captureFreq = obj["frequency"] | 433.92f;
                CC1101_MHZ = captureFreq;
                captureReady = false;
                capturedSamples = 0;

                // Trigger capture via state machine
                currentState = API_STATE_CAPTURE;

                JsonDocument resp;
                resp["status"] = "capturing";
                resp["frequency"] = captureFreq;
                String response;
                serializeJson(resp, response);
                request->send(200, "application/json", response);
            }
        ));

        // ============================================================
        // GET /api/rx/stop - Stop capture and return samples
        // ============================================================
        server.on("/api/rx/stop", HTTP_GET, [this](AsyncWebServerRequest *request) {
            currentState = API_STATE_IDLE;

            JsonDocument doc;
            doc["status"] = "stopped";
            doc["sample_count"] = samplecount;
            doc["frequency"] = captureFreq;

            // Include samples (limit to prevent OOM)
            int count = min(samplecount, 2048);
            JsonArray arr = doc["samples"].to<JsonArray>();
            for (int i = 0; i < count; i++) {
                arr.add(sample[i]);
            }

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // GET /api/rx/samples - Get last captured samples without stopping
        // ============================================================
        server.on("/api/rx/samples", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["sample_count"] = samplecount;
            doc["frequency"] = captureFreq;
            doc["state"] = currentState;

            int count = min(samplecount, 2048);
            JsonArray arr = doc["samples"].to<JsonArray>();
            for (int i = 0; i < count; i++) {
                arr.add(sample[i]);
            }

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // POST /api/replay - Replay last captured signal
        // ============================================================
        server.on("/api/replay", HTTP_POST, [this](AsyncWebServerRequest *request) {
            if (samplecount < 2) {
                request->send(400, "application/json", "{\"error\":\"no capture data\"}");
                return;
            }
            currentState = API_STATE_PLAYBACK;

            JsonDocument doc;
            doc["status"] = "replaying";
            doc["samples"] = samplecount;
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // POST /api/scan/start - Start frequency scanner
        // Body: { "start": 433.0, "stop": 434.0 }
        // ============================================================
        server.addHandler(new AsyncCallbackJsonWebHandler("/api/scan/start",
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject obj = json.as<JsonObject>();
                scanFreqStart = obj["start"] | 433.0f;
                scanFreqStop = obj["stop"] | 434.0f;
                scanRunning = true;
                scanPeakFreq = 0;
                scanPeakRSSI = -128;
                memset(scanRSSI, -128, sizeof(scanRSSI));

                request->send(200, "application/json", "{\"status\":\"scanning\"}");
            }
        ));

        // ============================================================
        // GET /api/scan/results - Get scanner FFT data
        // ============================================================
        server.on("/api/scan/results", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["start"] = scanFreqStart;
            doc["stop"] = scanFreqStop;
            doc["running"] = scanRunning;
            doc["peak_freq"] = scanPeakFreq;
            doc["peak_rssi"] = scanPeakRSSI;
            doc["bins"] = SCAN_BINS;

            JsonArray arr = doc["rssi"].to<JsonArray>();
            for (int i = 0; i < SCAN_BINS; i++) {
                arr.add(scanRSSI[i]);
            }

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // GET /api/scan/stop - Stop scanner
        // ============================================================
        server.on("/api/scan/stop", HTTP_GET, [this](AsyncWebServerRequest *request) {
            scanRunning = false;
            request->send(200, "application/json", "{\"status\":\"stopped\"}");
        });

        // ============================================================
        // POST /api/reboot - Restart device
        // ============================================================
        server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
            request->send(200, "application/json", "{\"status\":\"rebooting\"}");
            delay(500);
            ESP.restart();
        });

        // ============================================================
        // GET /api/update/check - Check for firmware updates
        // ============================================================
        server.on("/api/update/check", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["current_version"] = WAVESENTINEL_FW_VERSION;
            doc["update_url"] = "http://3.224.236.50/ota/version.json";
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // ============================================================
        // POST /api/update/flash - OTA firmware upload
        // ============================================================
        server.on("/api/update/flash", HTTP_POST,
            [](AsyncWebServerRequest *request) {
                bool success = !Update.hasError();
                AsyncWebServerResponse *response = request->beginResponse(
                    success ? 200 : 500, "application/json",
                    success ? "{\"status\":\"ok\",\"message\":\"rebooting\"}"
                            : "{\"status\":\"error\",\"message\":\"update failed\"}"
                );
                request->send(response);
                if (success) {
                    delay(500);
                    ESP.restart();
                }
            },
            [](AsyncWebServerRequest *request, String filename, size_t index,
               uint8_t *data, size_t len, bool final) {
                if (!index) {
                    Serial.printf("[API] OTA update start: %s\n", filename.c_str());
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                        Update.printError(Serial);
                    }
                }
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
                if (final) {
                    if (Update.end(true)) {
                        Serial.printf("[API] OTA update success: %u bytes\n", index + len);
                    } else {
                        Update.printError(Serial);
                    }
                }
            }
        );

        // ============================================================
        // GET / - Simple web UI
        // ============================================================
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            String html = "<!DOCTYPE html><html><head><title>WaveSentinel API</title></head><body>";
            html += "<h1>WaveSentinel v" WAVESENTINEL_FW_VERSION "</h1>";
            html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
            html += "<h2>API Endpoints</h2><ul>";
            html += "<li>GET <a href='/api/status'>/api/status</a> - Device info</li>";
            html += "<li>GET/POST /api/config - CC1101 config</li>";
            html += "<li>POST /api/tx - Transmit signal</li>";
            html += "<li>POST /api/rx/start - Start capture</li>";
            html += "<li>GET <a href='/api/rx/stop'>/api/rx/stop</a> - Stop & get samples</li>";
            html += "<li>POST /api/replay - Replay last capture</li>";
            html += "<li>POST /api/scan/start - Start frequency scan</li>";
            html += "<li>GET <a href='/api/scan/results'>/api/scan/results</a> - FFT data</li>";
            html += "<li>POST /api/reboot - Restart device</li>";
            html += "<li>POST /api/update/flash - OTA firmware upload</li>";
            html += "</ul>";
            html += "<h2>Firmware Update</h2>";
            html += "<form method='POST' action='/api/update/flash' enctype='multipart/form-data'>";
            html += "<input type='file' name='firmware' accept='.bin'> ";
            html += "<input type='submit' value='Upload & Flash'>";
            html += "</form></body></html>";
            request->send(200, "text/html", html);
        });
    }

    // Fast frequency scanner - call from main loop when scanning
    void scannerTick() {
        if (!scanRunning) return;

        float range = scanFreqStop - scanFreqStart;
        float step = range / SCAN_BINS;

        // Do a full sweep each tick for speed
        scanPeakRSSI = -128;
        scanPeakFreq = 0;

        for (int i = 0; i < SCAN_BINS; i++) {
            float f = scanFreqStart + (i * step);
            ELECHOUSE_cc1101.setMHZ(f);
            ELECHOUSE_cc1101.SetRx(f);
            delayMicroseconds(500);  // Short settle time
            int rssi = ELECHOUSE_cc1101.getRssi();
            scanRSSI[i] = (int8_t)constrain(rssi, -128, 0);

            if (rssi > scanPeakRSSI) {
                scanPeakRSSI = rssi;
                scanPeakFreq = f;
            }
        }
    }
};

#endif
