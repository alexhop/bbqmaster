#include "ota.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// Minimal JSON value extractor
static String jsonValue(const String& json, const char* key) {
    String needle = String("\"") + key + "\":";
    int idx = json.indexOf(needle);
    if (idx < 0) return "";

    idx += needle.length();
    while (idx < (int)json.length() && json[idx] == ' ') idx++;

    if (json[idx] == '"') {
        int start = idx + 1;
        int end = json.indexOf('"', start);
        if (end < 0) return "";
        return json.substring(start, end);
    }

    int end = idx;
    while (end < (int)json.length() && json[end] != ',' && json[end] != '}') end++;
    return json.substring(idx, end);
}

void otaCheck() {
    Serial.printf("[ota] Checking for updates (current: v%s build %d)...\n",
                  FW_VERSION, BUILD_NUMBER);

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/api/firmware/check?deviceId=%s&currentBuild=%d",
        SERVER_HOST, SERVER_PORT, DEVICE_ID, BUILD_NUMBER);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[ota] Check failed (HTTP %d)\n", httpCode);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    String available = jsonValue(body, "updateAvailable");
    if (available != "true") {
        Serial.println("[ota] Firmware is up to date");
        return;
    }

    String downloadUrl = jsonValue(body, "url");
    String newBuild = jsonValue(body, "buildNumber");

    if (downloadUrl.length() == 0) {
        Serial.println("[ota] Server reported update but no download URL");
        return;
    }

    Serial.printf("[ota] Update available! Build %s — downloading from: %s\n",
                  newBuild.c_str(), downloadUrl.c_str());

    WiFiClient client;
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret = httpUpdate.update(client, downloadUrl);

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("[ota] Update flashed — rebooting...");
            Serial.flush();
            ESP.restart();
            break;
        case HTTP_UPDATE_FAILED:
            Serial.printf("[ota] Update FAILED: %s (error %d)\n",
                httpUpdate.getLastErrorString().c_str(),
                httpUpdate.getLastError());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[ota] Server said no update needed");
            break;
    }
}
