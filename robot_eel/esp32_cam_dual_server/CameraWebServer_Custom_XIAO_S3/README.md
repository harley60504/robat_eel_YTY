# XIAO ESP32S3 Dual-Server (Official style) + Your Frontend

- :80 → `/` (your UI), `/status`, `/set`, `/capture`
- :81 → `/stream` (MJPEG)
- STA with static IP (edit SSID/PASS/IP at the top of `.ino`)

## Files
- `CameraWebServer_Custom_XIAO_S3.ino` – full sketch (handlers + Wi-Fi + camera + servers)
- `index_myui.h` – your HTML frontend (uses `/status` + `/set`)
- **Not included**: `camera_pins.h` – use the one from your board package (Seeed XIAO ESP32S3 Sense).

## Build notes
- Arduino-ESP32 **3.x**
- Board: **Seeed XIAO ESP32S3 (Sense)**; enable PSRAM if available.
- If `:81` is blocked in your environment, uncomment the `:80` fallback `/stream` (look for "optional: :80 fallback stream" in the .ino) and the page will auto-fallback.

## Low-latency tips
- `FRAMESIZE_QVGA`, `jpeg_quality=16~22`, `fb_count=1`, `grab_mode=CAMERA_GRAB_LATEST`
- Keep one viewer at a time (`503 busy` is intentional).

