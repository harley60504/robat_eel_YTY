import 'dart:convert';
import 'package:http/http.dart' as http;

class PythonApi {
  static const int port = 8000;

  static Uri _u(String host, String path) =>
      Uri.parse("http://$host:$port$path");

  static Future<bool> ping({required String pcHost}) async {
    try {
      final res = await http
          .get(_u(pcHost, "/"))
          .timeout(const Duration(milliseconds: 700));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  static Future<bool> waitUntilReady({
    required String pcHost,
    Duration timeout = const Duration(seconds: 8),
  }) async {
    final deadline = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(deadline)) {
      if (await ping(pcHost: pcHost)) return true;
      await Future.delayed(const Duration(milliseconds: 350));
    }
    return false;
  }

  // ===============================
  // ESP Host
  // ===============================
  static Future<bool> setEspHost({
    required String pcHost,
    required String espHost,
  }) async {
    try {
      final res = await http.post(
        _u(pcHost, "/set_esp_host"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode({
          "esp_host": espHost,
        }),
      );
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  // ===============================
  // Start / Stop
  // ===============================
  static Future<bool> start({required String pcHost}) async {
    try {
      final res = await http.post(_u(pcHost, "/start"));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  static Future<bool> stop({required String pcHost}) async {
    try {
      final res = await http.post(_u(pcHost, "/stop"));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  // ===============================
  // RTT Measure
  // ===============================
  static Future<bool> measureOn({required String pcHost}) async {
    try {
      final res = await http.post(_u(pcHost, "/measure_on"));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  static Future<bool> measureOff({required String pcHost}) async {
    try {
      final res = await http.post(_u(pcHost, "/measure_off"));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }
}
