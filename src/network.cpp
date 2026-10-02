#include "network.h"
#include "battery.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "esp_netif.h"

// ─── Helpers ──────────────────────────────────────────────

static void advancePos(int& pos, int written, int bufSize) {
    if (written > 0) {
        int remaining = bufSize - pos;
        pos += (written < remaining) ? written : remaining - 1;
    }
}

// ─── WiFi ─────────────────────────────────────────────────

bool networkConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    // Arduino setHostname often fails to propagate to DHCP on ESP32-S3/C6.
    // Use the ESP-IDF netif API directly to set the hostname before DHCP runs.
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_set_hostname(netif, DEVICE_HOSTNAME);
    }
    WiFi.setAutoReconnect(true);

    // Scan for networks first (debug: see what's visible)
    Serial.println("[network] Scanning WiFi networks...");
    int n = WiFi.scanNetworks();
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Serial.printf("[network]   %d: %-20s ch=%d rssi=%d %s\n",
                          i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i),
                          WiFi.RSSI(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
        }
    } else {
        Serial.println("[network]   No networks found!");
    }
    WiFi.scanDelete();

    Serial.printf("[network] Connecting to '%s'...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Single long wait — don't fight the WiFi stack with retries
    unsigned long start = millis();
    unsigned long timeout = WIFI_TIMEOUT_MS * WIFI_RETRIES;  // 45 seconds total
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout) break;
        if ((millis() - start) % 5000 < 250) {
            Serial.printf("[network] Waiting... status=%d (%lu ms)\n",
                          WiFi.status(), millis() - start);
        }
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[network] Connected — IP: %s, RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());

        // Start mDNS so the device is discoverable as DEVICE_HOSTNAME.local
        if (MDNS.begin(DEVICE_HOSTNAME)) {
            Serial.printf("[network] mDNS: %s.local\n", DEVICE_HOSTNAME);
#ifndef NO_CAMERA
            MDNS.addService("http", "tcp", CAM_STREAM_PORT);
#endif
        }

        return true;
    }

    Serial.printf("[network] WiFi failed (status=%d after %lu ms)\n",
                  WiFi.status(), millis() - start);

    Serial.println("[network] WiFi failed after all attempts");
    return false;
}

// ─── POST probe data ─────────────────────────────────────

bool networkPostProbe(const CombustionProbeData& probe) {
    char serialStr[12];
    formatSerial(probe.serialNumber, serialStr, sizeof(serialStr));

    char probeId[24];
    snprintf(probeId, sizeof(probeId), "combustion-%s", serialStr);

    // All 8 thermistor temps in °F
    float t[8];
    for (int i = 0; i < 8; i++) {
        t[i] = cToF(probe.temperatures[i]);
    }

    // Build camera URLs from our current IP so the dashboard can
    // embed the live stream. probeSlot is 1-8 (the user-assignable
    // probe ID from the Combustion app), making it easy for the UI
    // to show "Probe 2" instead of the serial hash.
    //
    // Omit the ":80" suffix when running on the default HTTP port —
    // both forms are valid but http://192.168.1.50/stream is
    // cleaner in the UI than http://192.168.1.50:80/stream.
    IPAddress ip = WiFi.localIP();
    char streamUrl[64];
    char snapshotUrl[64];
#if CAM_STREAM_PORT == 80
    snprintf(streamUrl, sizeof(streamUrl),
             "http://%u.%u.%u.%u/stream",
             ip[0], ip[1], ip[2], ip[3]);
    snprintf(snapshotUrl, sizeof(snapshotUrl),
             "http://%u.%u.%u.%u/snapshot",
             ip[0], ip[1], ip[2], ip[3]);
#else
    snprintf(streamUrl, sizeof(streamUrl),
             "http://%u.%u.%u.%u:%d/stream",
             ip[0], ip[1], ip[2], ip[3], CAM_STREAM_PORT);
    snprintf(snapshotUrl, sizeof(snapshotUrl),
             "http://%u.%u.%u.%u:%d/snapshot",
             ip[0], ip[1], ip[2], ip[3], CAM_STREAM_PORT);
#endif

    char payload[1024];
    snprintf(payload, sizeof(payload),
        "{"
        "\"probeId\":\"%s\","
        "\"probeSlot\":%d,"
        "\"core\":%.1f,"
        "\"surface\":%.1f,"
        "\"ambient\":%.1f,"
        "\"temperatures\":{"
            "\"t1\":%.1f,\"t2\":%.1f,\"t3\":%.1f,\"t4\":%.1f,"
            "\"t5\":%.1f,\"t6\":%.1f,\"t7\":%.1f,\"t8\":%.1f"
        "},"
        "\"batteryOk\":%s,"
        "\"color\":\"%s\","
        "\"streamUrl\":\"%s\","
        "\"snapshotUrl\":\"%s\""
        "}",
        probeId,
        probe.probeId + 1,              // 0-indexed internal → 1-8 for UI
        probe.core, probe.surface, probe.ambient,
        t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7],
        probe.batteryLow ? "false" : "true",
        probe.colorId == COLOR_YELLOW ? "yellow" : "grey",
        streamUrl,
        snapshotUrl
    );

#if VERBOSE_LOG
    Serial.printf("[network] Probe payload: %s\n", payload);
#endif

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", SERVER_HOST, SERVER_PORT, SMOKER_API_PATH);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    if (httpCode == 201) {
        Serial.printf("[network] Probe POST OK: %s\n", response.c_str());
        return true;
    } else if (httpCode > 0) {
        Serial.printf("[network] Probe POST %d: %s\n", httpCode, response.c_str());
        return false;
    } else {
        Serial.printf("[network] Probe POST error: %s\n", http.errorToString(httpCode).c_str());
        return false;
    }
}

// ─── POST ambient sensors ────────────────────────────────

bool networkPostAmbient(const BmeReading& reading) {
    // Build JSON batch: temp, humidity, pressure, and optionally gas
    char payload[1024];
    int pos = 0;

    payload[pos++] = '[';

    // Temperature
    advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
        "{\"sensorId\":\"%s-air-temp\",\"type\":\"temperature\","
        "\"value\":%.1f,\"unit\":\"°F\",\"location\":\"%s\"}",
        DEVICE_ID, reading.temperatureF, DEVICE_LOCATION), sizeof(payload));

    // Humidity
    payload[pos++] = ',';
    advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
        "{\"sensorId\":\"%s-humidity\",\"type\":\"humidity\","
        "\"value\":%.1f,\"unit\":\"%%\",\"location\":\"%s\"}",
        DEVICE_ID, reading.humidity, DEVICE_LOCATION), sizeof(payload));

    // Pressure (reported as temperature type with hPa unit — server can handle)
    payload[pos++] = ',';
    advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
        "{\"sensorId\":\"%s-pressure\",\"type\":\"temperature\","
        "\"value\":%.1f,\"unit\":\"hPa\",\"location\":\"%s\"}",
        DEVICE_ID, reading.pressureHpa, DEVICE_LOCATION), sizeof(payload));

    // Gas resistance (BME680/688 only)
    if (reading.hasGas && reading.gasResistance > 0) {
        payload[pos++] = ',';
        advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
            "{\"sensorId\":\"%s-gas\",\"type\":\"co2\","
            "\"value\":%.0f,\"unit\":\"Ohm\",\"location\":\"%s\"}",
            DEVICE_ID, reading.gasResistance, DEVICE_LOCATION), sizeof(payload));
    }

    if (pos >= (int)sizeof(payload) - 1) pos = sizeof(payload) - 2;
    payload[pos++] = ']';
    payload[pos] = '\0';

#if VERBOSE_LOG
    Serial.printf("[network] Ambient payload: %s\n", payload);
#endif

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", SERVER_HOST, SERVER_PORT, SENSOR_API_PATH);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    if (httpCode == 201) {
        Serial.printf("[network] Ambient POST OK\n");
        return true;
    } else if (httpCode > 0) {
        Serial.printf("[network] Ambient POST %d: %s\n", httpCode, response.c_str());
        return false;
    } else {
        Serial.printf("[network] Ambient POST error: %s\n", http.errorToString(httpCode).c_str());
        return false;
    }
}

// ─── Heartbeat ───────────────────────────────────────────

bool networkHeartbeat(int probeCount, int sensorsOnline, bool hasCamera) {
    char payload[512];
    int pos = 0;

    advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
        "{\"deviceId\":\"%s\""
        ",\"firmwareVersion\":\"%s\""
        ",\"buildNumber\":%d"
        ",\"location\":\"%s\""
        ",\"ip\":\"%s\""
        ",\"rssi\":%d"
        ",\"uptimeSeconds\":%lu"
        ",\"sensorsOnline\":%d",
        DEVICE_ID, FW_VERSION, BUILD_NUMBER,
        DEVICE_LOCATION,
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        millis() / 1000,
        sensorsOnline + probeCount), sizeof(payload));

    // Battery voltage from the external 2:1 divider on GPIO1. If the
    // divider isn't wired (or the battery is fully depleted and the
    // device is on USB power), batteryRead() returns valid=false and
    // we just omit the fields from the payload — the server's
    // NetworkDevice type treats them as optional.
    BatteryReading battery = batteryRead();
    if (battery.valid) {
        advancePos(pos, snprintf(payload + pos, sizeof(payload) - pos,
            ",\"batteryVoltage\":%.2f"
            ",\"batteryPercent\":%d",
            battery.voltage, battery.percent), sizeof(payload));
    }

    if (pos >= (int)sizeof(payload) - 1) pos = sizeof(payload) - 2;
    payload[pos++] = '}';
    payload[pos] = '\0';

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/network/heartbeat",
             SERVER_HOST, SERVER_PORT);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.POST(payload);
    http.end();

    return httpCode == 200;
}

// ─── Disconnect ──────────────────────────────────────────

void networkDisconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[network] WiFi off");
}
