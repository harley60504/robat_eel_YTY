# XIAO ESP32S3 Dual-Server — gzip frontend, mDNS removed

- :80 → `/` (gzip HTML), `/status`, `/set`, `/capture`（:81 失敗時會掛 `/stream` 供回退）
- :81 → `/stream`（資源不足時啟不來，會自動回退到 :80 `/stream`）
- mDNS 已移除，請用 IP 開頁。

