#ifndef OTA_H
#define OTA_H

// Check for an OTA firmware update from the server.
// Polls /api/firmware/check and flashes if a newer build is available.
void otaCheck();

#endif
