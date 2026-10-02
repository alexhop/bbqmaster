#ifndef COMBUSTION_H
#define COMBUSTION_H

#include <stdint.h>
#include <stddef.h>

// ─── Combustion Probe BLE Advertisement Parser ────────────
//
// Parses manufacturer-specific data from Combustion Inc.
// Predictive Thermometer BLE advertising packets.
//
// Spec: https://github.com/combustion-inc/combustion-documentation
// Vendor ID: 0x09C7
// Manufacturer data: 25 bytes

// Product types
enum CombustionProduct : uint8_t {
    PRODUCT_PROBE    = 0,
    PRODUCT_NODE     = 1,
    PRODUCT_DISPLAY  = 2,
    PRODUCT_BOOSTER  = 3,
    PRODUCT_GAUGE    = 4,
};

// Probe mode
enum ProbeMode : uint8_t {
    MODE_NORMAL       = 0,
    MODE_INSTANT_READ = 1,
    MODE_RESERVED     = 2,
    MODE_ERROR        = 3,
};

// Probe color
enum ProbeColor : uint8_t {
    COLOR_YELLOW = 0,
    COLOR_GREY   = 1,
    // 2-7 TBD
};

struct CombustionProbeData {
    // Identity
    uint32_t serialNumber;
    uint8_t  productType;
    uint8_t  probeId;       // 0-7 (displayed as 1-8)
    uint8_t  colorId;
    uint8_t  mode;          // ProbeMode

    // 8 raw temperatures in °C
    float temperatures[8];  // T1 (tip/core) through T8 (handle/ambient)

    // Virtual sensor mappings (which T sensor is core/surface/ambient)
    uint8_t coreIndex;      // 0-5 → T1-T6
    uint8_t surfaceIndex;   // 0-3 → T4-T7 (offset by 3)
    uint8_t ambientIndex;   // 0-3 → T5-T8 (offset by 4)

    // Derived virtual sensors in °F
    float core;
    float surface;
    float ambient;

    // Status
    bool  batteryLow;
    uint8_t overheating;    // Bitmask: bit0=T1 ... bit7=T8

    // RSSI at time of scan
    int rssi;

    // Valid flag
    bool valid;
};

// Parse a Combustion BLE manufacturer data payload (25 bytes).
// Returns populated CombustionProbeData with valid=true on success.
CombustionProbeData parseCombustionAdv(const uint8_t* data, size_t len, int rssi);

// Convert Celsius to Fahrenheit
inline float cToF(float c) { return c * 9.0f / 5.0f + 32.0f; }

// Format a serial number as a hex string
void formatSerial(uint32_t serial, char* buf, size_t bufLen);

#endif
