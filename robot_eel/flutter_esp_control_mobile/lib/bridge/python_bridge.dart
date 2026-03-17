import '../net/host_resolver.dart';
import '../api/python_api.dart';

class PythonBridge {
  static Future<bool> syncEsp32HostToPython({
    required String pcHost,
  }) async {
    final espHost = await HostResolver.autoSelectHost();
    return await PythonApi.setEspHost(
      pcHost: pcHost,
      espHost: espHost,
    );
  }
}
