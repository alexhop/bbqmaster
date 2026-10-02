#ifndef BME_SENSOR_H
#define BME_SENSOR_H

#include <stdint.h>

// ─── Auto-detecting BME280 / BME680 / BME688 driver ──────
//
// Probes I2C for any BME sensor at 0x77 then 0x76.
// Reads chip ID register (0xD0) to determine variant:
//   0x60 = BME280   (temp, humidity, pressure)
//   0x61 = BME680/8 (temp, humidity, pressure, gas/VOC)
//
// All three chips are read through the Adafruit BME680 library
// when it's a 680/688, or the Adafruit BME280 library for 280.

enum BmeSensorType : uint8_t {
    BME_NONE = 0,
    BME_280  = 1,
    BME_680  = 2,   // Also covers BME688
};

struct BmeReading {
    float temperatureF;     // °F
    float humidity;         // % RH
    float pressureHpa;      // hPa (mbar)
    float gasResistance;    // Ohms (BME680/688 only, 0 for BME280)
    bool  hasGas;           // true if gas reading is available
};

// Initialize I2C and probe for BME sensor.
// Returns the detected sensor type (BME_NONE if nothing found).
BmeSensorType bmeInit();

// Read current values. Returns true on success.
bool bmeRead(BmeReading* reading);

// Get a human-readable name for the detected sensor.
const char* bmeName();

#endif
