#include "camera.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <WebServer.h>

WebServer streamServer(CAM_STREAM_PORT);
static camera_fb_t* currentFb = nullptr;

// ─── Power management state ─────────────────────────────

static unsigned long lastCameraRequestMs = 0;
static bool cameraSleeping = false;

// Bump the "last active" timestamp so cameraMaybeSleep() holds off.
static void cameraNoteActivity() {
    lastCameraRequestMs = millis();
}

// Wake the camera if it's currently sleeping. Called by the stream
// and snapshot handlers before they touch esp_camera_fb_get().
// Returns true if the camera is ready, false on wake failure.
static bool cameraWake() {
    if (!cameraSleeping) return true;

    Serial.println("[camera] Waking from sleep...");
    if (!cameraInit()) {
        Serial.println("[camera] Wake failed — cameraInit() returned false");
        return false;
    }
    cameraSleeping = false;

    // The first couple of frames after a cold reinit are often
    // white or corrupt while the sensor's AGC/AWB settle. Discard
    // them so the first client frame looks clean.
    for (int i = 0; i < 2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    Serial.println("[camera] Wake complete");
    return true;
}

bool cameraIsAsleep() { return cameraSleeping; }

void cameraMaybeSleep() {
    if (cameraSleeping) return;
    if (millis() - lastCameraRequestMs < CAMERA_SLEEP_TIMEOUT_MS) return;

    Serial.println("[camera] No viewers — sleeping to save power");
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        Serial.printf("[camera] esp_camera_deinit failed: 0x%x\n", err);
        // Don't flip the sleeping flag — next wake attempt would try
        // to reinit a driver that's still partly initialized.
        return;
    }
    cameraSleeping = true;
}

// ─── Camera Init ─────────────────────────────────────────

bool cameraInit() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    // Use PSRAM for frame buffers (S3 Sense has 8MB)
    if (psramFound()) {
        config.frame_size   = CAM_FRAME_SIZE;
        config.jpeg_quality = CAM_JPEG_QUALITY;
        config.fb_count     = CAM_FB_COUNT;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        Serial.printf("[camera] PSRAM found (%d KB) — using for frame buffers\n",
                      ESP.getPsramSize() / 1024);
    } else {
        config.frame_size   = FRAMESIZE_QVGA;  // 320x240 fallback
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
        Serial.println("[camera] No PSRAM — using QVGA fallback");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[camera] Init failed: 0x%x\n", err);
        return false;
    }

    // Adjust sensor settings for outdoor use
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);    // Auto white balance
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1); // Auto exposure
        s->set_aec2(s, 1);         // AEC DSP
        s->set_gain_ctrl(s, 1);    // Auto gain
    }

    Serial.println("[camera] OV2640 initialized");
    return true;
}

// ─── MJPEG Stream Handler ────────────────────────────────
//
// MJPEG streaming over HTTP multipart/x-mixed-replace. We bypass the
// Arduino WebServer's send() API entirely and write HTTP/1.0 headers
// directly to the underlying WiFiClient because WebServer.send()
// unconditionally uses HTTP/1.1 + chunked transfer encoding for an
// unknown-length response, and combining chunked encoding with
// multipart framing is not a valid HTTP response — Chrome rejects it
// with ERR_INVALID_CHUNKED_ENCODING.
//
// Using HTTP/1.0 with no Content-Length and no Transfer-Encoding
// means "read until the server closes the connection", which is the
// standard MJPEG-over-HTTP pattern every ESP32-cam example uses.

static const char* STREAM_BOUNDARY = "bbqmastercamframe";

static void handleStream() {
    WiFiClient client = streamServer.client();
    if (!client) return;

    cameraNoteActivity();
    if (!cameraWake()) {
        client.print(
            "HTTP/1.0 503 Service Unavailable\r\n"
            "Content-Type: text/plain\r\n\r\n"
            "Camera wake failed\r\n");
        client.stop();
        return;
    }

    // Manual HTTP/1.0 response headers. No Content-Length, no chunked
    // encoding — framing is entirely inside the multipart body.
    client.print(
        "HTTP/1.0 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary=");
    client.print(STREAM_BOUNDARY);
    client.print("\r\n\r\n");

    char partBuf[128];

    while (client.connected()) {
        // Keep the "active viewer" flag fresh while we're serving.
        // Otherwise a client that streams for more than
        // CAMERA_SLEEP_TIMEOUT_MS would cause cameraMaybeSleep() to
        // yank the driver out from under us on the next main-loop
        // iteration.
        cameraNoteActivity();

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("[camera] Frame capture failed");
            break;
        }

        // Each part starts with --boundary, then its own headers, then
        // a blank line, then the JPEG bytes, then CRLF before the next
        // boundary. This matches RFC 2046 section 5.1.1.
        snprintf(partBuf, sizeof(partBuf),
            "--%s\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n",
            STREAM_BOUNDARY, fb->len);

        client.write(partBuf, strlen(partBuf));
        client.write(fb->buf, fb->len);
        client.write("\r\n", 2);

        esp_camera_fb_return(fb);

        if (!client.connected()) break;

        // ~15 fps max
        delay(66);
    }

    client.stop();
}

// ─── Snapshot Handler ────────────────────────────────────

static void handleSnapshot() {
    cameraNoteActivity();
    if (!cameraWake()) {
        streamServer.send(503, "text/plain", "Camera wake failed");
        return;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        streamServer.send(500, "text/plain", "Camera capture failed");
        return;
    }

    streamServer.sendHeader("Content-Disposition", "inline; filename=snapshot.jpg");
    streamServer.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// ─── Start Server ────────────────────────────────────────

// Minimal HTML index served at /. Embeds the live stream so typing
// `http://bbqmaster.local` in a browser just shows the video.
// Keeping it small (no external CSS/JS) so it fits easily in RAM
// and serves fast even on WiFi hiccups.
static const char INDEX_HTML[] PROGMEM =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>BBQ Master</title>\n"
    "<style>\n"
    "  html,body{margin:0;padding:0;background:#000;color:#eee;"
    "font-family:system-ui,-apple-system,sans-serif;height:100%;"
    "display:flex;flex-direction:column;align-items:center;justify-content:center}\n"
    "  img{max-width:100%;max-height:100vh;object-fit:contain;display:block}\n"
    "  .links{position:fixed;bottom:8px;right:12px;font-size:11px;opacity:.6}\n"
    "  .links a{color:#aaa;margin-left:10px;text-decoration:none}\n"
    "</style>\n"
    "</head><body>\n"
    "<img src=\"/stream\" alt=\"Live camera\">\n"
    "<div class=\"links\">" DEVICE_ID " · "
    "<a href=\"/snapshot\">snapshot</a> · "
    "<a href=\"/stream\">stream</a></div>\n"
    "</body></html>\n";

static void handleRoot() {
    streamServer.send_P(200, "text/html", INDEX_HTML);
}

void cameraStartServer() {
    streamServer.on("/", HTTP_GET, handleRoot);
    streamServer.on("/stream", HTTP_GET, handleStream);
    streamServer.on("/snapshot", HTTP_GET, handleSnapshot);
    streamServer.begin();
    Serial.printf("[camera] HTTP server on port %d\n", CAM_STREAM_PORT);
}

// ─── Single Frame Capture ────────────────────────────────

CameraFrame cameraCaptureJpeg() {
    CameraFrame frame = {nullptr, 0};

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return frame;

    currentFb = fb;
    frame.data = fb->buf;
    frame.len = fb->len;
    return frame;
}

void cameraReleaseFrame() {
    if (currentFb) {
        esp_camera_fb_return(currentFb);
        currentFb = nullptr;
    }
}

void cameraHandleClients() {
    streamServer.handleClient();
}
