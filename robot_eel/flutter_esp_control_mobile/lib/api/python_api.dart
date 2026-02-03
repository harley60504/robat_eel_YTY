import 'dart:convert';
import 'package:http/http.dart' as http;

class PythonApi {
  static const int port = 8000;

  static Uri _u(String host, String path) =>
      Uri.parse("http://$host:$port$path");

  // =========================================================
  // ESP Host
  // =========================================================
  static Future<bool> setEspHost({
    required String pcHost,
    required String espHost,
    String espWsPath = "/ws",
  }) async {
    try {
      final res = await http.post(
        _u(pcHost, "/set_esp_host"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode({
          "esp_host": espHost,
          "esp_ws_path": espWsPath,
        }),
      );
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  // =========================================================
  // Start / Stop
  // =========================================================
  static Future<bool> start({
    required String pcHost,
    double base = 120,
    double amp = 30,
    double freq = 0.6,
    double phaseStep = 0.7,
    int intervalMs = 50,
  }) async {
    try {
      final res = await http.post(
        _u(pcHost, "/start"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode({
          "base": base,
          "amp": amp,
          "freq": freq,
          "phase_step": phaseStep,
          "interval_ms": intervalMs,
        }),
      );
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

  // =========================================================
  // setParams（✅ 不含 seq/ts）
  // =========================================================
  static Future<bool> setParams({
    required String pcHost,
    double? base,
    double? amp,
    double? freq,
    double? phaseStep,
    int? intervalMs,
  }) async {
    try {
      final body = <String, dynamic>{};

      if (base != null) body["base"] = base;
      if (amp != null) body["amp"] = amp;
      if (freq != null) body["freq"] = freq;
      if (phaseStep != null) body["phase_step"] = phaseStep;
      if (intervalMs != null) body["interval_ms"] = intervalMs;

      final res = await http.post(
        _u(pcHost, "/set_params"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode(body),
      );
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }
}
