#ifndef THERMAL_H
#define THERMAL_H

#include <stdint.h>

// ─── CPU thermal protection ──────────────────────────────
//
// Watches the chip's built-in die temperature sensor from its own
// FreeRTOS task, so it keeps running while the main loop is blocked
// serving a camera stream, waiting on an HTTP request or reconnecting
// WiFi.
//
//   below THERMAL_THROTTLE_C   normal operation
//   at    THERMAL_THROTTLE_C   throttled: camera off and refused, CPU
//                              clock lowered. Probe and ambient
//                              reporting carry on. Clears once the die
//                              cools to THERMAL_THROTTLE_CLEAR_C.
//   at    THERMAL_SHUTDOWN_C   deep sleep for THERMAL_COOLDOWN_SEC,
//                              then reboot. The device stays asleep,
//                              one cooldown period at a time, until
//                              the die is below THERMAL_RESUME_C.
//
// Thresholds are in config.h.

enum ThermalState : uint8_t {
    THERMAL_OK        = 0,
    THERMAL_THROTTLED = 1,
};

// Call first thing in setup(), before WiFi, BLE or the camera start.
// Does not return if the chip is still too hot after a thermal
// shutdown: it goes straight back to deep sleep.
void thermalInit();

ThermalState thermalState();
const char*  thermalStateName();

// False while throttled. The camera must stay off and refuse requests.
bool thermalCameraAllowed();

// Last die temperature in °C, or NAN if the sensor can't be read.
float thermalCpuTempC();

// Thermal shutdowns since the device was last powered on or reset.
uint32_t thermalShutdownCount();

#endif
