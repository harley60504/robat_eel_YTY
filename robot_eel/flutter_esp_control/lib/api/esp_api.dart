import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../config.dart';

const enableWsDebug = true;

class WsControlApi {
  static WebSocketChannel? _ws;
  static Stream<dynamic>? _broadcast;

  static void ensureConnect() {
    if (_ws != null) return;

    if (enableWsDebug) {
      print("[WS] connecting → ${ApiConfig.wsControlUrl}");
    }

    try {
      _ws = WebSocketChannel.connect(Uri.parse(ApiConfig.wsControlUrl));

      _broadcast = _ws!.stream.map((msg) {
        if (enableWsDebug) print("[WS RX] $msg");
        return jsonDecode(msg);
      }).asBroadcastStream();
    } catch (e) {
      print("[WS] connect failed: $e");
      _ws = null;
    }
  }

  static Stream<dynamic> stream() {
    ensureConnect();
    return _broadcast ?? const Stream.empty();
  }

  static void send(Map<String, dynamic> body) {
    ensureConnect();
    if (_ws == null) return;

    final text = jsonEncode(body);
    if (enableWsDebug) print("[WS TX] $text");
    _ws!.sink.add(text);
  }

  // ===== API =====
  static void setParam(Map<String, dynamic> p) =>
      send({"cmd": "set_param", ...p});

  static void setCameraParam(Map<String, dynamic> p) =>
      send({"cmd": "camera_param", ...p});

  static void wifiStatus() => send({"cmd": "wifi_status"});
  static void wifiScan() => send({"cmd": "wifi_scan"});

  static void wifiConnect(String ssid, String pass) =>
      send({"cmd": "wifi_connect", "ssid": ssid, "pass": pass});

  static void wifiSave(String ssid, String pass) =>
      send({"cmd": "wifi_save", "ssid": ssid, "pass": pass});

  static void wifiDelete(String ssid) =>
      send({"cmd": "wifi_delete", "ssid": ssid});
}
