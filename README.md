# BBQ Master

Firmware for a small, always-on cooking station monitor. A Seeed XIAO ESP32-S3
Sense listens for Combustion Inc. Predictive Thermometer probes over Bluetooth,
forwards every probe's temperatures to a server on your local network, and
serves a live camera view of the grill.

- **Firmware:** PlatformIO + Arduino framework, version 0.5.1 (build 5)
- **Power:** USB-C. The device stays awake and connected; it does not sleep.

This project is not affiliated with or endorsed by Combustion Inc. It only
listens to the Bluetooth advertisements their probes broadcast, using the
[BLE specification they publish](https://github.com/combustion-inc/combustion-documentation).

## Key Components

| Component | Role | Required |
|-----------|------|----------|
| [Seeed XIAO ESP32-S3 Sense](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html) | Microcontroller with WiFi, Bluetooth and the OV2640 camera | Yes |
| [Combustion Inc. Predictive Thermometer](https://combustion.inc/products/predictive-thermometer) | The wireless probe: eight temperature sensors along its length | Yes, one to eight probes |
| Bosch BME280, BME680 or BME688 breakout, such as the [Adafruit BME280](https://www.adafruit.com/product/2652) or [BME688](https://www.adafruit.com/product/5046) | Air temperature, humidity and pressure at the cooking station; gas resistance on the 680/688 | Optional |
| Two 100k resistors | Battery voltage divider, if you run it from a LiPo | Optional |

A [Seeed XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)
can be used in place of the S3 Sense when you do not want the camera.

Nothing needs to be paired. The probes broadcast their readings and the
firmware picks up every probe in range.

## Hardware

The XIAO ESP32-S3 Sense is used as it ships: the camera board plugs onto the
main board, and the WiFi/Bluetooth antenna must be attached to the u.FL
connector. Without the antenna the board will not find any WiFi network.

Optional wiring:

| XIAO pin | Connects to |
|----------|-------------|
| D4 | BME sensor SDA |
| D5 | BME sensor SCL |
| 3V3 / GND | BME sensor power |
| GPIO1 (A0) | Battery divider midpoint: 100k to BAT+, 100k to GND |

The BME sensor is detected at boot at I2C address `0x77` or `0x76`, and the
chip type is read from the sensor, so no build flag is needed. If no sensor
answers, ambient readings are skipped. The battery fields are likewise left
out when no divider is fitted.

## Quick Start

```bash
pip install platformio

# Site-specific settings (WiFi credentials, server address). Gitignored.
cp src/secrets.example.h src/secrets.h
$EDITOR src/secrets.h

# Build and flash
pio run -t upload            # XIAO ESP32-S3 Sense, with camera
pio run -e c6 -t upload      # XIAO ESP32-C6, no camera

pio device monitor
```

| Env | Board | Camera |
|-----|-------|--------|
| `s3-sense` (default) | Seeed XIAO ESP32-S3 Sense | Yes |
| `c6` | Seeed XIAO ESP32-C6 | No |

Both environments use the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform.
`python3` must be on the PATH; `get_network_flags.py` and `fix_wifi_deps.py`
work around an undeclared `WiFi` to `Network` library dependency in
Arduino-ESP32 3.x.

Once it is running, open `http://bbqmaster.local` (or the IP address printed
on the serial console) to see the camera.

## Configuration

Site-specific values live in `src/secrets.h` (copied from
`src/secrets.example.h`, never committed):

| Setting | Description |
|---------|-------------|
| `WIFI_SSID` / `WIFI_PASSWORD` | WiFi network to join |
| `SERVER_HOST` / `SERVER_PORT` | Address of the receiving server |

Everything else is in `src/config.h`. The identity settings can be overridden
with `build_flags` in `platformio.ini`; give each unit its own.

| Setting | Default | Description |
|---------|---------|-------------|
| `DEVICE_ID` | `"bbq-1"` | Unique device ID, prefix for ambient sensor IDs |
| `DEVICE_HOSTNAME` | `"bbqmaster"` | DHCP and mDNS hostname (`bbqmaster.local`) |
| `DEVICE_LOCATION` | `"Grill"` | Human-readable location sent with readings |
| `POST_INTERVAL_SEC` | `3` | How often probe readings are posted |
| `AMBIENT_INTERVAL_SEC` | `30` | How often the BME sensor is read and posted |
| `HEARTBEAT_INTERVAL_SEC` | `30` | Heartbeat interval |
| `OTA_CHECK_INTERVAL_SEC` | `60` | Firmware update check interval |
| `CAM_STREAM_PORT` | `80` | Port of the on-device camera web server |
| `CAMERA_SLEEP_TIMEOUT_MS` | `30000` | Camera is powered down after this long with no viewers |
| `CAM_FRAME_SIZE` / `CAM_JPEG_QUALITY` | VGA / `12` | Camera resolution and JPEG quality |
| `BATTERY_DIVIDER_RATIO` | `2.0f` | Battery divider ratio |
| `VERBOSE_LOG` | `true` | Print payloads on serial |

## How It Works

At boot the device joins WiFi, looks for a BME sensor, starts the camera and
its web server, checks for a firmware update, and starts a continuous passive
Bluetooth scan. From then on:

- Every advertisement from a Combustion probe is decoded and cached, keyed by
  the probe's serial number. Up to eight probes are tracked.
- Every 3 seconds, the latest reading of each cached probe is posted to the
  server.
- Every 30 seconds, the BME sensor is read and posted, and a heartbeat is sent.
- Every 60 seconds, the server is asked whether a newer firmware build exists.
- If WiFi drops, the device reconnects and carries on.

Each probe reports eight temperatures, T1 at the tip through T8 at the handle.
The probe also says which of them currently represent the core, surface and
ambient temperature; the firmware passes on both the raw eight and those
three.

### Camera

The S3 Sense build runs a small web server on port 80:

| Path | Returns |
|------|---------|
| `/` | A page that shows the live stream |
| `/stream` | MJPEG stream, up to about 15 frames per second at 640x480 |
| `/snapshot` | A single JPEG |

The camera is powered down after 30 seconds without a viewer and wakes on the
next request, which adds roughly half a second to the first frame.

## Server API

The firmware talks plain HTTP to `SERVER_HOST:SERVER_PORT`. No server is
included in this repository; anything that implements these endpoints will
work.

### `POST /api/smoker/reading`

One request per probe, every 3 seconds. The firmware expects `201`. All
temperatures are in degrees Fahrenheit.

```json
{
  "probeId": "combustion-1000A3F2",
  "probeSlot": 1,
  "core": 142.3,
  "surface": 188.6,
  "ambient": 251.4,
  "temperatures": {"t1": 142.3, "t2": 143.0, "t3": 147.9, "t4": 160.2, "t5": 188.6, "t6": 220.1, "t7": 244.8, "t8": 251.4},
  "batteryOk": true,
  "color": "yellow",
  "streamUrl": "http://192.168.1.50/stream",
  "snapshotUrl": "http://192.168.1.50/snapshot"
}
```

`probeId` is built from the probe's serial number. `probeSlot` is the probe
number (1 to 8) assigned in the Combustion app.

### `POST /api/sensors/batch`

Ambient readings from the BME sensor, every 30 seconds, as a JSON array. The
firmware expects `201`.

| Sensor ID | `type` | `unit` | Notes |
|-----------|--------|--------|-------|
| `<DEVICE_ID>-air-temp` | `temperature` | `°F` | |
| `<DEVICE_ID>-humidity` | `humidity` | `%` | |
| `<DEVICE_ID>-pressure` | `temperature` | `hPa` | Sent with type `temperature`; tell it apart by the unit |
| `<DEVICE_ID>-gas` | `co2` | `Ohm` | BME680/688 only. Raw gas resistance, sent with type `co2` |

Each object has `sensorId`, `type`, `value`, `unit` and `location`.

### `POST /api/network/heartbeat`

Every 30 seconds. The firmware expects `200`.

```json
{
  "deviceId": "bbq-1",
  "firmwareVersion": "0.5.1",
  "buildNumber": 5,
  "location": "Grill",
  "ip": "192.168.1.50",
  "rssi": -58,
  "uptimeSeconds": 3600,
  "sensorsOnline": 2,
  "batteryVoltage": 3.95,
  "batteryPercent": 72
}
```

`sensorsOnline` is the number of probes seen plus one if a BME sensor is
present. The battery fields are omitted when no divider is fitted.

### `GET /api/firmware/check?deviceId=<id>&currentBuild=<n>`

Called at boot and every 60 seconds. The firmware expects `200` and a compact
JSON body. If `updateAvailable` is `true` it downloads `url`, flashes it and
reboots.

```json
{"updateAvailable":true,"buildNumber":6,"url":"http://192.168.1.100:3001/firmware/bbqmaster.bin"}
```

The response parser is deliberately minimal: flat keys only, and no whitespace
between a key and its colon.

### Security

This design assumes a trusted home network. Requests to the server are
unauthenticated plain HTTP, the camera stream is open to anyone on the
network, and OTA images are neither signed nor fetched over TLS. Do not expose
the device or the server endpoints to the internet.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| No WiFi networks found | Antenna not attached | Attach the antenna to the u.FL connector on the XIAO. |
| WiFi will not connect | Wrong credentials | The serial console lists the networks it can see. Check `src/secrets.h`. |
| No probes discovered | Probe asleep or out of range | Take the probe out of its charger so it starts broadcasting, and bring it closer. |
| Camera init failed | Camera board not seated | Reseat the camera board on the XIAO's connector. |
| Stream works by IP but not `bbqmaster.local` | mDNS not available on the client | Use the IP address from the serial console. |
| Probe POST fails | Wrong server address | Check `SERVER_HOST` and `SERVER_PORT` in `src/secrets.h`. |
| Build fails with "Missing src/secrets.h" | No site config | `cp src/secrets.example.h src/secrets.h` and edit it. |

## Directory Structure

```
.
|-- README.md
|-- LICENSE
|-- TODO.md
|-- platformio.ini            PlatformIO environments
|-- get_network_flags.py      Build helper: include paths for Arduino-ESP32 3.x
|-- fix_wifi_deps.py          Build helper: WiFi -> Network dependency fix
+-- src/
    |-- main.cpp              Setup, BLE scan callback, main loop timers
    |-- config.h              Pins, timing, camera and battery settings
    |-- secrets.example.h     Template for secrets.h (WiFi, server address)
    |-- combustion.cpp/.h     Combustion probe advertisement parser
    |-- camera.cpp/.h         OV2640 init, MJPEG stream and snapshot server
    |-- bme_sensor.cpp/.h     BME280 / BME680 / BME688 auto-detection and reads
    |-- battery.cpp/.h        Battery voltage from the optional divider
    |-- network.cpp/.h        WiFi, mDNS, JSON payloads, HTTP POST
    +-- ota.cpp/.h            Pull-based firmware update
```

## License

[MIT](LICENSE)
