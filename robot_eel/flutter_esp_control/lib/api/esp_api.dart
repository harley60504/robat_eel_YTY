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

    _ws = WebSocketChannel.connect(Uri.parse(ApiConfig.wsControlUrl));

    _broadcast = _ws!.stream.map((msg) {
      if (enableWsDebug) print("[WS RX] $msg");
      return jsonDecode(msg);
    }).asBroadcastStream();
  }

  static Stream<dynamic> stream() {
    ensureConnect();
    return _broadcast!;
  }

  static void send(Map<String, dynamic> body) {
    ensureConnect();
    final text = jsonEncode(body);

    if (enableWsDebug) print("[WS TX] $text");

    _ws!.sink.add(text);
  }

  /// ===== 參數設定（唯一寫入 API）=====
  static setParam(Map<String, dynamic> p) => send({"cmd": "set_param", ...p});

  /// ===== 相機 =====
  static setCameraParam(Map<String, dynamic> p) =>
      send({"cmd": "camera_param", ...p});

  /// ===== 主動取得狀態 =====
  static getParams() => send({"cmd": "get_params"});
}
