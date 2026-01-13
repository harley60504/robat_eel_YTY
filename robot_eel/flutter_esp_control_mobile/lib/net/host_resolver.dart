import 'dart:convert';
import 'package:http/http.dart' as http;

import '../net/wifi_info.dart';
import '../storage/ip_storage.dart';
import '../storage/ssid_ip_store.dart';

class HostResolver {
  static const String apHost = "192.168.4.1";

  static Future<bool> pingEsp32(String host) async {
    try {
      final res = await http
          .get(Uri.parse("http://$host/wifi_current"))
          .timeout(const Duration(milliseconds: 800));

      if (res.statusCode != 200) return false;

      final data = jsonDecode(res.body) as Map<String, dynamic>;
      return data.containsKey("connected");
    } catch (_) {
      return false;
    }
  }

  /// ✅ App 啟動時呼叫：自動選出最可能成功的 host
  static Future<String> autoSelectHost() async {
    // 1) SSID -> IP（最優先）
    final ssid = await WifiInfo.getCurrentSsid();
    if (ssid != null) {
      final ip = await SsidIpStore.getIpBySsid(ssid);
      if (ip != null && await pingEsp32(ip)) {
        return ip;
      }
    }

    // 2) last_ip（次優先）
    final lastIp = await IpStorage.loadLastIp();
    if (lastIp != null && await pingEsp32(lastIp)) {
      return lastIp;
    }

    // 3) AP fallback（保命）
    if (await pingEsp32(apHost)) {
      return apHost;
    }

    // 4) 全部失敗
    return apHost; // 你也可以 throw exception
  }

  /// ✅ 連線成功後呼叫：更新 last_ip + SSID->IP
  static Future<void> updateCachesByStaIp(String staIp) async {
    // 存 last_ip
    await IpStorage.saveLastIp(staIp);

    // 存 SSID -> IP
    final ssid = await WifiInfo.getCurrentSsid();
    if (ssid != null) {
      await SsidIpStore.setIpForSsid(ssid, staIp);
    }
  }
}
