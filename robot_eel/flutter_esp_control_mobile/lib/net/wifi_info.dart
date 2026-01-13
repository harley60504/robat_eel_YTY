import 'package:network_info_plus/network_info_plus.dart';

class WifiInfo {
  static Future<String?> getCurrentSsid() async {
    final info = NetworkInfo();
    String? ssid = await info.getWifiName();

    if (ssid == null) return null;

    ssid = ssid.replaceAll('"', '').trim();
    if (ssid.isEmpty) return null;
    if (ssid.toLowerCase().contains("unknown")) return null;

    return ssid;
  }
}
