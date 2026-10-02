#include "bme_sensor.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>

static BmeSensorType detectedType = BME_NONE;
static uint8_t detectedAddr = 0;

// One of these will be active based on detected chip
static Adafruit_BME280 bme280;
static Adafruit_BME680 bme680;

// ─── Chip detection ──────────────────────────────────────

static uint8_t readChipId(uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(BME_REG_CHIP_ID);
    if (Wire.endTransmission() != 0) return 0;

    Wire.requestFrom(addr, (uint8_t)1);
    if (Wire.available()) return Wire.read();
    return 0;
}

// ─── Public API ──────────────────────────────────────────

BmeSensorType bmeInit() {
    Wire.begin(I2C_SDA, I2C_SCL);

    // Try both possible addresses
    for (uint8_t addr : {BME_ADDR_PRIMARY, BME_ADDR_SECONDARY}) {
        uint8_t chipId = readChipId(addr);

        if (chipId == BME280_CHIP_ID) {
            if (bme280.begin(addr, &Wire)) {
                // Configure for weather monitoring (low power, high accuracy)
                bme280.setSampling(
                    Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temp
                    Adafruit_BME280::SAMPLING_X1,   // pressure
                    Adafruit_BME280::SAMPLING_X1,   // humidity
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_1000
                );
                detectedType = BME_280;
                detectedAddr = addr;
                Serial.printf("[bme] BME280 found at 0x%02X\n", addr);
                return detectedType;
            }
        } else if (chipId == BME680_CHIP_ID) {
            if (bme680.begin(addr)) {
                // Configure gas heater for VOC measurement
                bme680.setTemperatureOversampling(BME680_OS_8X);
                bme680.setHumidityOversampling(BME680_OS_2X);
                bme680.setPressureOversampling(BME680_OS_4X);
                bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
                bme680.setGasHeater(320, 150); // 320°C for 150ms
                detectedType = BME_680;
                detectedAddr = addr;
                Serial.printf("[bme] BME680/688 found at 0x%02X\n", addr);
                return detectedType;
            }
        } else if (chipId != 0) {
            Serial.printf("[bme] Unknown chip ID 0x%02X at 0x%02X\n", chipId, addr);
        }
    }

    Serial.println("[bme] No BME sensor found — ambient readings disabled");
    return BME_NONE;
}

bool bmeRead(BmeReading* reading) {
    if (detectedType == BME_NONE || !reading) return false;

    if (detectedType == BME_280) {
        // Force a measurement
        bme280.takeForcedMeasurement();

        reading->temperatureF = bme280.readTemperature() * 9.0f / 5.0f + 32.0f;
        reading->humidity     = bme280.readHumidity();
        reading->pressureHpa  = bme280.readPressure() / 100.0f;
        reading->gasResistance = 0;
        reading->hasGas = false;
        return true;
    }

    if (detectedType == BME_680) {
        if (!bme680.performReading()) {
            Serial.println("[bme] BME680 read failed");
            return false;
        }

        reading->temperatureF  = bme680.temperature * 9.0f / 5.0f + 32.0f;
        reading->humidity      = bme680.humidity;
        reading->pressureHpa   = bme680.pressure / 100.0f;
        reading->gasResistance = bme680.gas_resistance;
        reading->hasGas = true;
        return true;
    }

    return false;
}

const char* bmeName() {
    switch (detectedType) {
        case BME_280: return "BME280";
        case BME_680: return "BME680/688";
        default:      return "none";
    }
}
