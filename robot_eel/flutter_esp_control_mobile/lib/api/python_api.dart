import 'dart:convert';
import 'package:http/http.dart' as http;

class PythonApi {
  static const int port = 8000;

  static Uri _u(String host, String path) =>
      Uri.parse("http://$host:$port$path");

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
