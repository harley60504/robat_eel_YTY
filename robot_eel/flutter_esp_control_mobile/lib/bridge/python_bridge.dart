import '../net/host_resolver.dart';
import '../api/python_api.dart';

class PythonBridge {
  /// 一鍵：Flutter 算 ESP32 host → 傳給 Python
  static Future<bool> syncEsp32HostToPython({
    required String pcHost,
    String espWsPath = "/ws",
  }) async {
    final espHost = await HostResolver.autoSelectHost();
    final ok = await PythonApi.setEspHost(
      pcHost: pcHost,
      espHost: espHost,
      espWsPath: espWsPath,
    );
    return ok;
  }
}
