import 'api/esp_api.dart';

class ApiConfig {
  static String _host = "192.168.4.1";

  static String get host => _host;

  static String get httpBaseUrl => "http://$_host";
  static String get wsControlUrl => "ws://$_host:82";
  static String get wsStreamUrl => "ws://$_host:81";

  static Future<void> setHost(String newHost) async {
    newHost = newHost.trim();
    if (newHost.isEmpty) return;
    if (_host == newHost) return;

    _host = newHost;

    // ✅ host 改變 → 控制 WS 一定要重連
    WsControlApi.disconnect();

    // ✅ 立即嘗試重連（不用等）
    WsControlApi.ensureConnect();
  }
}
