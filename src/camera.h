#ifndef CAMERA_H
#define CAMERA_H

#include <stddef.h>
#include <stdint.h>

// Initialize the OV2640 camera on the XIAO ESP32-S3 Sense board.
// Returns true on success.
bool cameraInit();

// Start the MJPEG HTTP stream server on CAM_STREAM_PORT.
// Clients connect to http://<device-ip>/stream for live video
// and http://<device-ip>/snapshot for a single JPEG frame.
void cameraStartServer();

// Must be called from loop() to handle incoming stream/snapshot requests.
void cameraHandleClients();

// Capture a single JPEG frame and return it.
// Caller must call cameraReleaseFrame() when done with the buffer.
// Returns null on failure.
struct CameraFrame {
    uint8_t* data;
    size_t   len;
};

CameraFrame cameraCaptureJpeg();
void cameraReleaseFrame();

// Power management: the camera driver is deinitialized after
// CAMERA_SLEEP_TIMEOUT_MS with no /stream or /snapshot requests.
// Call cameraMaybeSleep() from the main loop to let it check.
// Returns true if the camera is currently asleep.
bool cameraIsAsleep();
void cameraMaybeSleep();

#endif
