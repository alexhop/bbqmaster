#ifndef NETWORK_H
#define NETWORK_H

#include "combustion.h"
#include "bme_sensor.h"

// Connect to WiFi with retry logic. Returns true on success.
bool networkConnect();

// POST a Combustion probe reading to /api/smoker/reading.
bool networkPostProbe(const CombustionProbeData& probe);

// POST ambient sensor readings (BME280/680/688) to /api/sensors/batch.
bool networkPostAmbient(const BmeReading& reading);

// POST a heartbeat to /api/network/heartbeat.
bool networkHeartbeat(int probeCount, int sensorsOnline, bool hasCamera);

// Clean WiFi disconnect.
void networkDisconnect();

#endif
