#include "combustion.h"
#include <Arduino.h>
#include <stdio.h>

// ─── Bit extraction helpers ──────────────────────────────

// Extract a 13-bit temperature value starting at the given bit offset
// from a packed byte array (LSB first).
static uint16_t extract13Bits(const uint8_t* data, int bitOffset) {
    // Calculate which byte and bit within that byte
    int byteIdx = bitOffset / 8;
    int bitIdx  = bitOffset % 8;

    // Read 3 bytes to cover the 13-bit span (may cross byte boundaries)
    uint32_t raw = (uint32_t)data[byteIdx]
                 | ((uint32_t)data[byteIdx + 1] << 8);
    if (bitIdx + 13 > 16) {
        raw |= ((uint32_t)data[byteIdx + 2] << 16);
    }

    return (raw >> bitIdx) & 0x1FFF;
}

// ─── Public API ──────────────────────────────────────────

CombustionProbeData parseCombustionAdv(const uint8_t* data, size_t len, int rssi) {
    CombustionProbeData result = {};
    result.valid = false;

    // NimBLE on Arduino-ESP32 returns manufacturer data with the 2-byte
    // vendor ID prefixed. The Combustion spec defines the advertisement
    // as 25 bytes total (2 vendor ID + 23 payload), but current probe
    // firmware observed in the field sends 24 bytes (drops the final
    // "Thermometer Preferences" byte). Accept either length as long as
    // the vendor ID is present, and skip to the payload at offset 2.
    //
    // Observed layout (offsets into `data`, vendor ID included):
    //   0-1   vendor ID 0x09C7
    //   2     product type
    //   3-6   serial number (little-endian)
    //   7-19  13 bytes of packed 8×13-bit temperature values
    //   20    mode/ID
    //   21    battery status + virtual sensor indexes
    //   22    network info (unused by probe, don't care)
    //   23    overheating bitmask
    //   24    thermometer preferences (optional — absent in current firmware)

    if (len < 24) return result;
    if (data[0] != 0xC7 || data[1] != 0x09) return result;

    const uint8_t* payload = data + 2;

    // Byte 0 (of payload): Product type.
    //
    // The earlier PRODUCT_PROBE = 0 constant came from a stale version
    // of the spec. Current probe firmware observed in the field sends
    // product type = 0x01. We no longer gate parsing on the value;
    // only the Node (relay) format requires a different parser, and
    // the Node packet has a distinctly different payload layout that
    // we don't handle here. Record the byte and continue.
    result.productType = payload[0];

    // Bytes 1-4: Serial number (little-endian)
    result.serialNumber = (uint32_t)payload[1]
                        | ((uint32_t)payload[2] << 8)
                        | ((uint32_t)payload[3] << 16)
                        | ((uint32_t)payload[4] << 24);

    // Bytes 5-17: Raw temperature data (13 bytes = 104 bits = 8 × 13-bit values)
    const uint8_t* tempData = payload + 5;
    for (int i = 0; i < 8; i++) {
        uint16_t raw = extract13Bits(tempData, i * 13);
        // Convert: temp_C = raw * 0.05 - 20
        result.temperatures[i] = (raw * 0.05f) - 20.0f;
    }

    // Byte 18: Mode/ID packed field
    //   Bits 0-1: Mode (2 bits)
    //   Bits 2-4: Color ID (3 bits)
    //   Bits 5-7: Probe ID (3 bits)
    uint8_t modeId = payload[18];
    result.mode    = modeId & 0x03;
    result.colorId = (modeId >> 2) & 0x07;
    result.probeId = (modeId >> 5) & 0x07;

    // Byte 19: Battery status & virtual sensor mapping
    //   Bit 0: Battery (0=OK, 1=Low)
    //   Bits 1-3: Core sensor index (0-5 → T1-T6)
    //   Bits 4-5: Surface sensor index (0-3 → T4-T7, offset +3)
    //   Bits 6-7: Ambient sensor index (0-3 → T5-T8, offset +4)
    uint8_t batVirt = payload[19];
    result.batteryLow   = (batVirt & 0x01) != 0;
    result.coreIndex    = (batVirt >> 1) & 0x07;
    result.surfaceIndex = (batVirt >> 4) & 0x03;
    result.ambientIndex = (batVirt >> 6) & 0x03;

    // Byte 21: Overheating sensor bitmask
    result.overheating = payload[21];

    // Calculate virtual sensors in Fahrenheit
    // Core: index maps directly to T1-T6 (0-5)
    if (result.coreIndex < 6) {
        result.core = cToF(result.temperatures[result.coreIndex]);
    } else {
        result.core = cToF(result.temperatures[0]); // Fallback to T1
    }

    // Surface: index 0-3 maps to T4-T7
    result.surface = cToF(result.temperatures[result.surfaceIndex + 3]);

    // Ambient: index 0-3 maps to T5-T8
    result.ambient = cToF(result.temperatures[result.ambientIndex + 4]);

    result.rssi = rssi;
    result.valid = true;

    return result;
}

void formatSerial(uint32_t serial, char* buf, size_t bufLen) {
    snprintf(buf, bufLen, "%08X", serial);
}
