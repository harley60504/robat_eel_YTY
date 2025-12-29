import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../config.dart';

class WsControlApi {
  static WebSocketChannel? _ws;
  static Stream<dynamic>? _broadcast;

  /// 建立連線（只做一次）
  static void ensureConnect() {
    if (_ws != null) return;

    print("[WS] connecting → ${ApiConfig.wsControlUrl}");

    _ws = WebSocketChannel.connect(Uri.parse(ApiConfig.wsControlUrl));

    // 建立可被多處監聽的 Stream
    _broadcast = _ws!.stream.map((msg) {
      print("[WS RX] $msg"); // ★ Debug RX
      return jsonDecode(msg);
    }).asBroadcastStream();

    print("[WS] connected");
  }

  /// 讓 widget 取得資料 Stream
  static Stream<dynamic> stream() {
    ensureConnect();
    return _broadcast!;
  }

  /// 傳送 JSON
  static void send(Map<String, dynamic> body) {
    ensureConnect();
    final text = jsonEncode(body);

    print("[WS TX] $text"); // ★ Debug TX

    _ws!.sink.add(text);
  }

  /// APIs
  static setMode(int mode) => send({"cmd": "set_mode", "value": mode});

  static setServo(int angle) => send({"cmd": "servo_angle", "value": angle});

  static setCameraParam(Map<String, dynamic> param) =>
      send({"cmd": "camera_param", ...param});

  static enableDebug() => send({"cmd": "debug_on"});

  static disableDebug() => send({"cmd": "debug_off"});

  static getParams() => send({"cmd": "get_params"});
}
