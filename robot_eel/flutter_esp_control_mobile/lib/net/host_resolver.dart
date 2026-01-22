import 'dart:async';
import 'package:http/http.dart' as http;

import '../storage/ip_storage.dart';
import 'wifi_info.dart';

class HostResult {
  final String host;
  final String reason; // ap_ssid(robot) / ssid_map(xxx) / last_ip / ap_fixed
  const HostResult(this.host, this.reason);
}

class HostResolver {
  static const String apHost = "192.168.4.1";

  /// ✅ 你的 ESP32 AP 名稱清單（多台支援）
  /// 以後新增第二台，只要加一行即可
  static const List<String> apSsidList = [
    "robot",
    // "robot2",
    // "robot3",
  ];

  /// ✅ 判斷目前手機 Wi-Fi SSID 是不是 ESP32 AP
  static bool isEspApSsid(String ssid) {
    final s = ssid.trim().toLowerCase();
    return apSsidList.any((ap) => s == ap.toLowerCase());
  }

  /// ✅ 用 HTTP ping（加入 retry，避免第一次掉包）
  static Future<bool> pingHost(String host) async {
    for (int i = 0; i < 2; i++) {
      try {
        final url = Uri.parse("http://$host/wifi_current");
        final res = await http.get(url).timeout(const Duration(milliseconds: 1500));
        if (res.statusCode == 200) return true;
      } catch (_) {}

      await Future.delayed(const Duration(milliseconds: 250));
    }
    return false;
  }

  /// ✅ 回傳 host + reason
  static Future<HostResult> autoSelectHostEx() async {
    // ✅ 優先拿 bootSsid（main init），沒有再即時讀
    final ssid = WifiInfo.bootSsid ?? await WifiInfo.getCurrentSsid();
    final lastIp = await IpStorage.loadLastIp();

    print("[HostResolver] phone ssid = $ssid");
    print("[HostResolver] last_ip = $lastIp");

    // ============================================================
    // ✅ 0) 最優先：如果手機連到 ESP32 AP（robot / robot2 / ...）
    // ============================================================
    if (ssid != null && isEspApSsid(ssid)) {
      print("[HostResolver] detected ESP AP ssid=$ssid -> use $apHost");
      return HostResult(apHost, "ap_ssid($ssid)");
    }

    // ============================================================
    // ✅ 1) SSID -> IP（表單）
    // ============================================================
    if (ssid != null) {
      final mappedIp = await IpStorage.loadIpForSsid(ssid);
      print("[HostResolver] ssid->ip = $mappedIp");

      if (mappedIp != null) {
        final ok = await pingHost(mappedIp);
        print("[HostResolver] ping mapped ip $mappedIp = $ok");

        if (ok) {
          return HostResult(mappedIp, "ssid_map($ssid)");
        }

        // ✅ mappedIp == lastIp：再給一次機會（避免第一次掉包）
        if (lastIp != null && lastIp == mappedIp) {
          final ok2 = await pingHost(lastIp);
          print("[HostResolver] ping mapped==last_ip retry $lastIp = $ok2");
          if (ok2) {
            return HostResult(lastIp, "ssid_map($ssid)");
          }
        }
      }
    }

    // ============================================================
    // ✅ 2) last_ip
    // ============================================================
    if (lastIp != null) {
      final ok = await pingHost(lastIp);
      print("[HostResolver] ping last_ip $lastIp = $ok");

      if (ok) {
        return HostResult(lastIp, "last_ip");
      }
    }

    // ============================================================
    // ✅ 3) 最後：固定 AP
    // ============================================================
    print("[HostResolver] use ap_fixed = $apHost");
    return const HostResult(apHost, "ap_fixed");
  }

  static Future<String> autoSelectHost() async {
    final r = await autoSelectHostEx();
    return r.host;
  }

  /// ✅ 當你連上 STA 拿到 IP 後呼叫：更新 last_ip + SSID->IP
  static Future<void> updateCachesByStaIp(String staIp) async {
    await IpStorage.saveLastIp(staIp);

    final ssid = WifiInfo.bootSsid ?? await WifiInfo.getCurrentSsid();
    if (ssid != null) {
      // ✅ 如果手機連的是 ESP32 AP（robot），不要存成 ssid_map
      if (isEspApSsid(ssid)) {
        print("[HostResolver] skip save map (ap ssid=$ssid), last_ip=$staIp");
        return;
      }

      await IpStorage.saveIpForSsid(ssid, staIp);
      print("[HostResolver] save map: $ssid -> $staIp");
    } else {
      print("[HostResolver] save map skipped (ssid=null), last_ip=$staIp");
    }
  }
}
