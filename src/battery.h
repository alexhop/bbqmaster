#ifndef BATTERY_H
#define BATTERY_H

// Battery voltage reading via an external 2:1 divider on GPIO1.
// See config.h for wiring notes — the XIAO ESP32-S3 Sense doesn't
// route BAT+ to an ADC pin out of the box, so this requires two
// resistors soldered between BAT+, GPIO1, and GND.

struct BatteryReading {
    bool  valid;     // false if the ADC returned 0 mV (divider not wired)
    float voltage;   // Estimated battery voltage in volts
    int   percent;   // 0-100 mapped from a linear LiPo curve
};

BatteryReading batteryRead();

#endif
