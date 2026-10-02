/*
 * BBQ Master — XIAO ESP32-S3 Sense Firmware
 *
 * Hardware: Seeed XIAO ESP32-S3 Sense (always-on, USB-C powered)
 *   - OV2640 camera (MJPEG stream + snapshots)
 *   - BLE 5.0 scanner for Combustion Inc. predictive thermometers
 *   - Optional I2C: BME280, BME680, or BME688 (auto-detected)
 *   - 8MB PSRAM for camera frame buffers
 *
 * Architecture:
 *   - WiFi stays connected permanently (wired USB-C power)
 *   - Camera serves an MJPEG stream on CAM_STREAM_PORT (/, /stream, /snapshot)
 *   - BLE scans continuously for Combustion probe advertisements
 *   - Every POST_INTERVAL_SEC: POST cached probe data to the server
 *   - Every AMBIENT_INTERVAL_SEC: read BME sensor, POST ambient data
 *   - Every OTA_CHECK_INTERVAL_SEC: check for firmware updates
 *
 * BLE Protocol: Combustion vendor ID 0x09C7, 8×13-bit thermistors
 * See: https://github.com/combustion-inc/combustion-documentation
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "combustion.h"
#include "bme_sensor.h"
#ifndef NO_CAMERA
#include "camera.h"
#endif
#include "network.h"
#include "ota.h"

// ─── Globals ─────────────────────────────────────────────

#define MAX_PROBES 8

static CombustionProbeData probeCache[MAX_PROBES];
static int probeCacheCount = 0;

static unsigned long lastProbePost   = 0;
static unsigned long lastAmbientPost = 0;
static unsigned long lastHeartbeat   = 0;
static unsigned long lastOtaCheck    = 0;

static BmeSensorType bmeType = BME_NONE;
#ifndef NO_CAMERA
static bool cameraAvailable  = false;
#endif
static bool wifiConnected    = false;

// ─── BLE Scan Callback ──────────────────────────────────

// Per-probe rate limiter for the reading log. Probes broadcast 4
// packets per second; we don't need to print every one. Each probe
// gets its own "last logged" timestamp so two probes don't contend
// for the same slot.
static unsigned long lastProbeLogMs[MAX_PROBES] = {0};
#define PROBE_LOG_INTERVAL_MS 3000

class CombustionScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        if (!device->haveManufacturerData()) return;

        const std::string& mfgData = device->getManufacturerData();
        if (mfgData.size() < 24) return;

        const uint8_t* data = (const uint8_t*)mfgData.data();

        // Vendor ID check (LE: 0xC7 0x09)
        uint16_t vendorId = data[0] | (data[1] << 8);
        if (vendorId != COMBUSTION_VENDOR_ID) return;

        CombustionProbeData probe = parseCombustionAdv(
            data, mfgData.size(), device->getRSSI()
        );
        if (!probe.valid) return;

        // Update or insert into cache
        int slot = -1;
        for (int i = 0; i < probeCacheCount; i++) {
            if (probeCache[i].serialNumber == probe.serialNumber) {
                probeCache[i] = probe;
                slot = i;
                break;
            }
        }
        if (slot < 0 && probeCacheCount < MAX_PROBES) {
            slot = probeCacheCount++;
            probeCache[slot] = probe;

            char serialStr[12];
            formatSerial(probe.serialNumber, serialStr, sizeof(serialStr));
            Serial.printf("[probe] New probe discovered: %s (slot %d)\n",
                          serialStr, slot);
        }
        if (slot < 0) return;  // cache full

        // Rate-limited per-probe reading log
        unsigned long now = millis();
        if (now - lastProbeLogMs[slot] < PROBE_LOG_INTERVAL_MS) return;
        lastProbeLogMs[slot] = now;

        char serialStr[12];
        formatSerial(probe.serialNumber, serialStr, sizeof(serialStr));

        Serial.printf(
            "[probe %s] ID%d %ddBm %s mode=%d | T1-T8°F: "
            "%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f | "
            "core=%.1f(T%d) surf=%.1f(T%d) amb=%.1f(T%d)%s\n",
            serialStr,
            probe.probeId + 1,
            probe.rssi,
            probe.batteryLow ? "LOW-BATT" : "OK",
            probe.mode,
            cToF(probe.temperatures[0]),
            cToF(probe.temperatures[1]),
            cToF(probe.temperatures[2]),
            cToF(probe.temperatures[3]),
            cToF(probe.temperatures[4]),
            cToF(probe.temperatures[5]),
            cToF(probe.temperatures[6]),
            cToF(probe.temperatures[7]),
            probe.core,      probe.coreIndex + 1,
            probe.surface,   probe.surfaceIndex + 4,
            probe.ambient,   probe.ambientIndex + 5,
            probe.overheating ? " OVERHEAT!" : "");
    }
};

static NimBLEScan* pBLEScan = nullptr;

// ─── BLE Setup ──────────────────────────────────────────

void bleSetup() {
    Serial.println("[ble] Initializing NimBLE...");
    NimBLEDevice::init(DEVICE_HOSTNAME);

    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(new CombustionScanCallbacks(), true);
    pBLEScan->setActiveScan(false);   // Passive: just listen
    pBLEScan->setInterval(100);       // 100ms scan interval
    pBLEScan->setWindow(99);          // 99ms window (nearly continuous)
    pBLEScan->setDuplicateFilter(false);

    // Start continuous scan
    pBLEScan->start(0, false);
    Serial.println("[ble] Continuous scan started — listening for Combustion probes");
}

// ─── Entry Point ────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("╔═══════════════════════════════════════════════╗");
    Serial.println("║  BBQ Master                                   ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.printf("  Device:   %s\n", DEVICE_ID);
    Serial.printf("  Location: %s\n", DEVICE_LOCATION);
    Serial.printf("  Firmware: v%s (build %d)\n", FW_VERSION, BUILD_NUMBER);
    Serial.printf("  PSRAM:    %d KB\n", ESP.getPsramSize() / 1024);
    Serial.println();

    // ── Connect WiFi first (must happen before camera on S3) ──
    Serial.println("[main] Connecting WiFi...");
    wifiConnected = networkConnect();

    // ── Initialize BME sensor (auto-detect 280/680/688) ──
    Serial.println("[main] Probing I2C for BME sensor...");
    bmeType = bmeInit();
    Serial.printf("[main] Ambient sensor: %s\n", bmeName());

#ifndef NO_CAMERA
    // ── Initialize camera (after WiFi to avoid GPIO conflicts on S3) ──
    Serial.println("[main] Initializing camera...");
    cameraAvailable = cameraInit();
    if (cameraAvailable) {
        Serial.println("[main] Camera ready");
    } else {
        Serial.println("[main] Camera not available — continuing without");
    }
#endif

    if (wifiConnected) {
#ifndef NO_CAMERA
        // Start camera stream server (needs WiFi for IP)
        if (cameraAvailable) {
            cameraStartServer();
            Serial.printf("[main] Camera stream: http://%s:%d/stream\n",
                          WiFi.localIP().toString().c_str(), CAM_STREAM_PORT);
            Serial.printf("[main] Camera snapshot: http://%s:%d/snapshot\n",
                          WiFi.localIP().toString().c_str(), CAM_STREAM_PORT);
        }
#endif

        // Check for OTA update on boot
        otaCheck();
    }

    // ── Initialize BLE scanner ──
    bleSetup();

    // ── Initialize timers ──
    lastProbePost   = millis();
    lastAmbientPost = millis();
    lastHeartbeat   = millis();
    lastOtaCheck    = millis();

    Serial.println();
    Serial.println("[main] All systems initialized — entering main loop");
    Serial.println();
}

void loop() {
    unsigned long now = millis();

#ifndef NO_CAMERA
    // ── Handle camera stream clients and sleep when idle ──
    if (cameraAvailable) {
        cameraHandleClients();
        // Deinit the camera driver if no stream/snapshot requests
        // have come in for CAMERA_SLEEP_TIMEOUT_MS. Saves ~70 mA
        // while nobody is watching. The first viewer request after
        // a sleep incurs a ~500 ms wake latency.
        cameraMaybeSleep();
    }
#endif

    // ── Ensure WiFi stays connected ──
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiConnected) {
            Serial.println("[main] WiFi dropped — reconnecting...");
            wifiConnected = false;
        }
        wifiConnected = networkConnect();
#ifndef NO_CAMERA
        if (wifiConnected && cameraAvailable) {
            cameraStartServer();
        }
#endif
        if (!wifiConnected) {
            delay(5000);
            return;
        }
    }

    // ── POST probe data ──
    if (now - lastProbePost >= POST_INTERVAL_SEC * 1000UL) {
        lastProbePost = now;
        if (probeCacheCount > 0) {
            Serial.printf("[main] Posting %d probe(s)\n", probeCacheCount);
            for (int i = 0; i < probeCacheCount; i++) {
                networkPostProbe(probeCache[i]);
            }
        }
    }

    // ── POST ambient data ──
    if (now - lastAmbientPost >= AMBIENT_INTERVAL_SEC * 1000UL) {
        lastAmbientPost = now;
        if (bmeType != BME_NONE) {
            BmeReading reading;
            if (bmeRead(&reading)) {
                Serial.printf("[main] %s: %.1f°F  %.1f%%  %.1f hPa",
                              bmeName(), reading.temperatureF,
                              reading.humidity, reading.pressureHpa);
                if (reading.hasGas) {
                    Serial.printf("  gas=%.0f Ohm", reading.gasResistance);
                }
                Serial.println();
                networkPostAmbient(reading);
            }
        }
    }

    // ── Heartbeat ──
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_SEC * 1000UL) {
        lastHeartbeat = now;
        int sensorCount = (bmeType != BME_NONE) ? 1 : 0;
#ifndef NO_CAMERA
        networkHeartbeat(probeCacheCount, sensorCount, cameraAvailable);
#else
        networkHeartbeat(probeCacheCount, sensorCount, false);
#endif
    }

    // ── OTA check ──
    if (now - lastOtaCheck >= OTA_CHECK_INTERVAL_SEC * 1000UL) {
        lastOtaCheck = now;
        otaCheck();
    }

    delay(10);
}
