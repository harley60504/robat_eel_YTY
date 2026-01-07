import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../config.dart';

const enableWsDebug = true;

class WsControlApi {
  static WebSocketChannel? _ws;
  static Stream<dynamic>? _broadcast;

  static void ensureConnect() {
    if (_ws != null) return;

    print("[WS] connecting → ${ApiConfig.wsControlUrl}");

    _ws = WebSocketChannel.connect(Uri.parse(ApiConfig.wsControlUrl));

    _broadcast = _ws!.stream.map((msg) {
      print("[WS RX] $msg");
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
    print("[WS TX] $text");
    _ws!.sink.add(text);
  }

  static setParam(Map<String, dynamic> p) => send({"cmd": "set_param", ...p});

  static setCameraParam(Map<String, dynamic> p) =>
      send({"cmd": "camera_param", ...p});

  static getParams() => send({"cmd": "get_params"});

  static getCameraParam() => send({"cmd": "get_camera_param"});
}
