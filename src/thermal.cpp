#include "thermal.h"
#include "config.h"

#include <Arduino.h>
#include <atomic>
#include <math.h>
#include <esp_sleep.h>

// Kept in RTC memory so they survive the cooldown deep sleep. Both go
// back to their initial values on a power cycle or ordinary reset.
RTC_DATA_ATTR static uint32_t rtcShutdownCount = 0;
RTC_DATA_ATTR static bool     rtcCoolingDown   = false;

static std::atomic<float>   lastTempC{NAN};
static std::atomic<uint8_t> state{THERMAL_OK};
static uint32_t normalCpuMhz = 0;

// ─── Sensor ──────────────────────────────────────────────

// temperatureRead() is the Arduino core's wrapper around the on-chip
// sensor. It returns NAN when the read fails.
static bool readTemp(float* out) {
    float c = temperatureRead();
    if (isnan(c)) return false;
    *out = c;
    return true;
}

// ─── Shutdown ────────────────────────────────────────────

[[noreturn]] static void sleepForCooldown() {
    rtcCoolingDown = true;
    // Give the serial line a moment, but don't flush: with no USB host
    // attached a flush can block, and this path must not hang.
    delay(100);
    esp_sleep_enable_timer_wakeup((uint64_t)THERMAL_COOLDOWN_SEC * 1000000ULL);
    esp_deep_sleep_start();
    for (;;) {}
}

// ─── Throttling ──────────────────────────────────────────

static void enterThrottled(float c) {
    state = THERMAL_THROTTLED;
    setCpuFrequencyMhz(THERMAL_THROTTLED_CPU_MHZ);
    Serial.printf("[thermal] CPU %.1f°C >= %d°C — throttling: camera off, CPU at %d MHz\n",
                  c, THERMAL_THROTTLE_C, THERMAL_THROTTLED_CPU_MHZ);
}

static void leaveThrottled(float c) {
    setCpuFrequencyMhz(normalCpuMhz);
    state = THERMAL_OK;
    Serial.printf("[thermal] CPU %.1f°C <= %d°C — back to normal\n",
                  c, THERMAL_THROTTLE_CLEAR_C);
}

// ─── Guard task ──────────────────────────────────────────

static void guardTask(void*) {
    int hotSamples  = 0;
    int failedReads = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(THERMAL_CHECK_INTERVAL_MS));

        float c;
        if (!readTemp(&c)) {
            // Without a reading there is nothing to act on. Report it
            // (the heartbeat drops cpuTempC) rather than guess.
            if (++failedReads == THERMAL_SENSOR_FAIL_LIMIT) {
                lastTempC = NAN;
                Serial.println("[thermal] WARNING: CPU temperature sensor not responding — protection inactive");
            }
            continue;
        }
        failedReads = 0;
        lastTempC = c;

        // Shutdown needs several readings in a row so that one bad
        // sample can't put the device to sleep mid-cook.
        if (c >= THERMAL_SHUTDOWN_C) {
            if (++hotSamples >= THERMAL_SHUTDOWN_SAMPLES) {
                rtcShutdownCount++;
                Serial.printf("[thermal] CPU %.1f°C >= %d°C — shutting down for %d s to cool\n",
                              c, THERMAL_SHUTDOWN_C, THERMAL_COOLDOWN_SEC);
                sleepForCooldown();
            }
        } else {
            hotSamples = 0;
        }

        if (state == THERMAL_OK && c >= THERMAL_THROTTLE_C) {
            enterThrottled(c);
        } else if (state == THERMAL_THROTTLED && c <= THERMAL_THROTTLE_CLEAR_C) {
            leaveThrottled(c);
        }
    }
}

// ─── Public API ──────────────────────────────────────────

void thermalInit() {
    normalCpuMhz = getCpuFrequencyMhz();

    float c;
    bool ok = readTemp(&c);
    if (!ok) {
        // The first read after power-up can fail while the sensor starts.
        delay(20);
        ok = readTemp(&c);
    }

    if (ok) {
        lastTempC = c;
        Serial.printf("[thermal] CPU temperature at boot: %.1f°C\n", c);

        // Coming back from a thermal shutdown the die has to be properly
        // cool, not just under the trip point, or it would trip again
        // as soon as the radios and camera are back on.
        int limit = rtcCoolingDown ? THERMAL_RESUME_C : THERMAL_SHUTDOWN_C;
        if (c >= limit) {
            if (!rtcCoolingDown) rtcShutdownCount++;
            Serial.printf("[thermal] Still %.1f°C (limit %d°C) — sleeping another %d s\n",
                          c, limit, THERMAL_COOLDOWN_SEC);
            sleepForCooldown();
        }
        if (c >= THERMAL_THROTTLE_C) enterThrottled(c);
    } else {
        Serial.println("[thermal] WARNING: CPU temperature sensor not responding — protection inactive");
    }
    rtcCoolingDown = false;

    if (rtcShutdownCount > 0) {
        Serial.printf("[thermal] Thermal shutdowns since power-on: %lu\n",
                      (unsigned long)rtcShutdownCount);
    }

    xTaskCreate(guardTask, "thermal", 4096, nullptr, 5, nullptr);
}

ThermalState thermalState() {
    return (ThermalState)state.load();
}

const char* thermalStateName() {
    return state.load() == THERMAL_THROTTLED ? "throttled" : "ok";
}

bool thermalCameraAllowed() {
    return state.load() == THERMAL_OK;
}

float thermalCpuTempC() {
    return lastTempC.load();
}

uint32_t thermalShutdownCount() {
    return rtcShutdownCount;
}
