#include "battery.h"
#include "config.h"
#include <Arduino.h>

BatteryReading batteryRead() {
    BatteryReading r = {};

    // analogReadMilliVolts() accounts for the internal ADC vref
    // calibration, so we don't have to convert from raw counts —
    // it returns millivolts at the ADC pin directly.
    int mv = analogReadMilliVolts(BATTERY_ADC_PIN);
    if (mv <= 0) {
        // Either the divider isn't wired or the battery is fully
        // depleted. Either way we treat it as "no data".
        r.valid = false;
        return r;
    }

    r.voltage = (mv * BATTERY_DIVIDER_RATIO) / 1000.0f;

    // Linear approximation from BATTERY_EMPTY_V to BATTERY_FULL_V.
    // LiPo discharge curves are not linear (there's a long plateau
    // at ~3.8 V), but a linear mapping is honest enough for a
    // dashboard gauge — you'd want a lookup table for a proper
    // fuel gauge.
    if (r.voltage >= BATTERY_FULL_V) {
        r.percent = 100;
    } else if (r.voltage <= BATTERY_EMPTY_V) {
        r.percent = 0;
    } else {
        r.percent = (int)((r.voltage - BATTERY_EMPTY_V) /
                          (BATTERY_FULL_V - BATTERY_EMPTY_V) * 100.0f);
    }

    r.valid = true;
    return r;
}
