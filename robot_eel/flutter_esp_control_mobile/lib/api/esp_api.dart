import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';
import '../config.dart';

const enableWsDebug = false;

class WsControlApi {
  static WebSocketChannel? _ws;

  static final StreamController<dynamic> _controller =
      StreamController<dynamic>.broadcast();

  static Timer? _retryTimer;
  static int _retryMs = 500; // 0.5s 起跳
  static String? _connectedUrl;

  /// ✅ 對外：拿 stream（永遠不會 null）
  static Stream<dynamic> stream() {
    ensureConnect();
    return _controller.stream;
  }

  /// ✅ 確保連線存在
  static void ensureConnect() {
    final url = ApiConfig.wsControlUrl;

    // 已連線且連線目標沒變 → 不動
    if (_ws != null && _connectedUrl == url) return;

    // 否則重連到新 url
    disconnect();
    _connect(url);
  }

  /// ✅ 強制斷線（host 改變時一定要呼叫）
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
            _controller.add(decoded);
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

  /// ✅ WS 掛了就清掉狀態，並排程重連
  static void _handleDisconnectAndRetry() {
    disconnect();

    // 避免重複排程
    _retryTimer?.cancel();

    // ✅ Backoff：0.5s → 1s → 2s → 4s → 8s → max 10s
    final delay = Duration(milliseconds: _retryMs);

    if (enableWsDebug) {
      print("[WS] retry in ${delay.inMilliseconds}ms");
    }

    _retryTimer = Timer(delay, () {
      _retryTimer = null;
      _retryMs = (_retryMs * 2).clamp(500, 10000);

      // ✅ 只要有人再呼叫 stream/send，就會 ensureConnect
      ensureConnect();
    });
  }

  /// ✅ 對外：送資料
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

  // ===== API =====
  static void setParam(Map<String, dynamic> p) =>
      send({"cmd": "set_param", ...p});

  static void setCameraParam(Map<String, dynamic> p) =>
      send({"cmd": "camera_param", ...p});

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
