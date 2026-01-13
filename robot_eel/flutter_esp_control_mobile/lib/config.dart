class ApiConfig {
  static String host = "192.168.4.1";
  static String hostReason = "unknown";

  static String get httpBaseUrl => "http://$host";
  static String get wsControlUrl => "ws://$host:82";
  static String get wsStreamUrl => "ws://$host:81";

  static Future<void> setHost(String newHost, {String reason = "manual"}) async {
    host = newHost;
    hostReason = reason;
  }
}
