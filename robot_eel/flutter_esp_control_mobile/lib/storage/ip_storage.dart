import 'package:shared_preferences/shared_preferences.dart';

class IpStorage {
  static const String _keyLastIp = "last_ip";

  static Future<void> saveLastIp(String ip) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyLastIp, ip);
  }

  static Future<String?> loadLastIp() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_keyLastIp);
  }

  static Future<void> clearLastIp() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_keyLastIp);
  }
}
