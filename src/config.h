#ifndef CONFIG_H
#define CONFIG_H

// ─── Build ───────────────────────────────────────────────────
#define BUILD_NUMBER    5
#define FW_VERSION      "0.5.1"

// ─── Device Identity ───────────────────────────────────────
#ifndef DEVICE_ID
#define DEVICE_ID       "bbq-1"
#endif
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "bbqmaster"
#endif
#ifndef DEVICE_LOCATION
#define DEVICE_LOCATION "Grill"
#endif

// ─── WiFi + Server ─────────────────────────────────────────
// WIFI_SSID, WIFI_PASSWORD, SERVER_HOST and SERVER_PORT are site-specific and
// live in secrets.h, which is gitignored so credentials never get committed.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing src/secrets.h - copy src/secrets.example.h to src/secrets.h and fill in your WiFi and server settings"
#endif

#define WIFI_TIMEOUT_MS 15000
#define WIFI_RETRIES    3

#define SMOKER_API_PATH "/api/smoker/reading"
#define SENSOR_API_PATH "/api/sensors/batch"

// ─── Pins (XIAO ESP32-S3 Sense) ──────────────────────────
// I2C — directly on the expansion board side connector
#define I2C_SDA         D4    // GPIO5
#define I2C_SCL         D5    // GPIO6

// ─── I2C Addresses (BME family) ───────────────────────────
// BME280/680/688 default to 0x77 (SDO pulled high).
// With SDO to GND: 0x76.
#define BME_ADDR_PRIMARY    0x77
#define BME_ADDR_SECONDARY  0x76

// ─── BME Chip ID Register ─────────────────────────────────
// Read register 0xD0 to identify the chip:
//   0x60 = BME280
//   0x61 = BME680 / BME688
#define BME_REG_CHIP_ID     0xD0
#define BME280_CHIP_ID      0x60
#define BME680_CHIP_ID      0x61

// ─── BLE Scanning ─────────────────────────────────────────
#define COMBUSTION_VENDOR_ID   0x09C7

// ─── Camera (OV2640 on S3 Sense) ──────────────────────────
// Camera pin mapping for XIAO ESP32-S3 Sense board
#define CAM_PIN_PWDN    -1    // No power down pin
#define CAM_PIN_RESET   -1    // No reset pin
#define CAM_PIN_XCLK    10
#define CAM_PIN_SIOD    40    // SCCB SDA
#define CAM_PIN_SIOC    39    // SCCB SCL
#define CAM_PIN_D7      48
#define CAM_PIN_D6      11
#define CAM_PIN_D5      12
#define CAM_PIN_D4      14
#define CAM_PIN_D3      16
#define CAM_PIN_D2      18
#define CAM_PIN_D1      17
#define CAM_PIN_D0      15
#define CAM_PIN_VSYNC   38
#define CAM_PIN_HREF    47
#define CAM_PIN_PCLK    13

// Camera settings
#define CAM_FRAME_SIZE   FRAMESIZE_VGA    // 640x480
#define CAM_JPEG_QUALITY 12               // 0-63, lower = better quality
#define CAM_FB_COUNT     2                // Frame buffers (PSRAM)

// Camera HTTP server port. Port 80 is the standard HTTP port so
// users can type `http://bbqmaster.local` without a port suffix.
// Historically the ESP32-cam reference ran on :81 because it also
// had a control panel on :80 — we don't, so we use :80 directly.
#define CAM_STREAM_PORT  80

// Snapshot POST interval (seconds) — how often to push a JPEG to the server
#define CAM_SNAPSHOT_INTERVAL_SEC  30

// Camera sleep — deinit the OV2640 driver after this long with no
// /stream or /snapshot requests. Saves ~70 mA while idle. Wake
// latency on next request is ~500 ms (driver reinit + first-frame
// discard). The XIAO S3 Sense doesn't expose CAM_PWDN to a GPIO,
// so sleep/wake via esp_camera_deinit/init is the cheapest option.
#define CAMERA_SLEEP_TIMEOUT_MS    30000

// ─── Battery monitoring ──────────────────────────────────
// The XIAO ESP32-S3 base board does not expose battery voltage to
// a dedicated ADC pin out of the box. To enable on-device battery
// monitoring, wire a 100 kΩ / 100 kΩ voltage divider:
//
//     BAT+ ──[100k]──┬──[100k]── GND
//                    │
//                    └── GPIO1 (A0)
//
// That gives a 2:1 divider so a 4.2 V fully-charged LiPo reads
// 2.1 V at GPIO1 — well within the ESP32-S3 ADC range. If you use
// different resistors, adjust BATTERY_DIVIDER_RATIO accordingly.
#define BATTERY_ADC_PIN            1         // GPIO1 / A0
#define BATTERY_DIVIDER_RATIO      2.0f      // Vbat = Vpin * ratio
#define BATTERY_FULL_V             4.20f     // LiPo nominal full charge
#define BATTERY_EMPTY_V            3.30f     // BQ25100 cutoff is ~3.0V; use 3.3V as "empty" for UX

// ─── Timing ───────────────────────────────────────────────
#define POST_INTERVAL_SEC          3     // Probe data POST interval
#define AMBIENT_INTERVAL_SEC       30    // BME sensor read interval
#define HEARTBEAT_INTERVAL_SEC     30
#define OTA_CHECK_INTERVAL_SEC     60

// ─── HTTP ────────────────────────────────────────────────
#define HTTP_TIMEOUT_MS            10000

// ─── Logging ─────────────────────────────────────────────
#ifndef VERBOSE_LOG
#define VERBOSE_LOG                true
#endif

#endif
