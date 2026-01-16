import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../config.dart';

const enableWsDebug = false;

class WsControlApi {
  static WebSocketChannel? _ws;

  static final StreamController<dynamic> _controller =
      StreamController<dynamic>.broadcast();

  static Timer? _retryTimer;
  static int _retryMs = 500;
  static String? _connectedUrl;

  // ✅ 快取：最後一筆 ctrl_params（不用等廣播就能顯示）
  static Map<String, dynamic>? lastCtrlParams;

  // ✅ notifier：UI 直接監聽這個（更快、更乾淨）
  static final ValueNotifier<Map<String, dynamic>?> ctrlParamsNotifier =
      ValueNotifier<Map<String, dynamic>?>(null);

  // ✅ 對外：拿 stream（servo_status / wifi_status 也會走這裡）
  static Stream<dynamic> stream() {
    ensureConnect();
    return _controller.stream;
  }

  /// ✅ 確保 WS 連線存在
  static void ensureConnect() {
    final url = ApiConfig.wsControlUrl;

    if (_ws != null && _connectedUrl == url) return;

    disconnect();
    _connect(url);
  }

  /// ✅ 強制斷線
  static void disconnect() {
    if (enableWsDebug) {
      print("[WS] disconnect");
    }

    _retryTimer?.cancel();
    _retryTimer = null;

    try {
      _ws?.sink.close();
    } catch (_) {}

    _ws = null;
    _connectedUrl = null;
  }

  /// ✅ 內部連線（含自動重試）
  static void _connect(String url) {
    if (enableWsDebug) {
      print("[WS] connecting → $url");
    }

    try {
      _ws = WebSocketChannel.connect(Uri.parse(url));
      _connectedUrl = url;

      // ✅ 成功連上就重置 backoff
      _retryMs = 500;

      _ws!.stream.listen(
        (msg) {
          try {
            if (enableWsDebug) print("[WS RX] $msg");
            final decoded = jsonDecode(msg);

            // 1) stream
            _controller.add(decoded);

            // 2) ctrl_params cache + notifier
            if (decoded is Map && decoded["type"] == "ctrl_params") {
              lastCtrlParams = Map<String, dynamic>.from(decoded);
              ctrlParamsNotifier.value = lastCtrlParams;
            }
          } catch (e) {
            if (enableWsDebug) print("[WS] json decode error: $e");
          }
        },
        onDone: () {
          if (enableWsDebug) print("[WS] closed");
          _handleDisconnectAndRetry();
        },
        onError: (e) {
          if (enableWsDebug) print("[WS] error: $e");
          _handleDisconnectAndRetry();
        },
        cancelOnError: true,
      );
    } catch (e) {
      if (enableWsDebug) print("[WS] connect failed: $e");
      _handleDisconnectAndRetry();
    }
  }

  static void _handleDisconnectAndRetry() {
    disconnect();
    _retryTimer?.cancel();

    final delay = Duration(milliseconds: _retryMs);

    if (enableWsDebug) {
      print("[WS] retry in ${delay.inMilliseconds}ms");
    }

    _retryTimer = Timer(delay, () {
      _retryTimer = null;
      _retryMs = (_retryMs * 2).clamp(500, 10000);

      ensureConnect();
    });
  }

  /// ✅ 對外：送資料（send 只吃 Map）
  static void send(Map<String, dynamic> body) {
    ensureConnect();
    if (_ws == null) return;

    final text = jsonEncode(body);
    if (enableWsDebug) print("[WS TX] $text");

    try {
      _ws!.sink.add(text);
    } catch (e) {
      if (enableWsDebug) print("[WS] send failed: $e");
      _handleDisconnectAndRetry();
    }
  }

  // ==============================
  // API
  // ==============================

  static void setParam(Map<String, dynamic> p) =>
      send({"cmd": "set_param", ...p});

  static void setCameraParam(Map<String, dynamic> p) =>
      send({"cmd": "camera_param", ...p});

  static void setAngle(List<double> angles) =>
      send({"cmd": "set_angle", "angles": angles});

  static void wifiStatus() => send({"cmd": "wifi_status"});
  static void wifiScan() => send({"cmd": "wifi_scan"});
  static void wifiList() => send({"cmd": "wifi_list"});

  static void wifiConnect(String ssid, String pass) =>
      send({"cmd": "wifi_connect", "ssid": ssid, "pass": pass});

  static void wifiSave(String ssid, String pass) =>
      send({"cmd": "wifi_save", "ssid": ssid, "pass": pass});

  static void wifiDelete(String ssid) =>
      send({"cmd": "wifi_delete", "ssid": ssid});
}
