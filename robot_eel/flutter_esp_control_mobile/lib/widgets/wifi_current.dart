import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class WiFiCurrentCard extends StatefulWidget {
  const WiFiCurrentCard({super.key});

  @override
  State<WiFiCurrentCard> createState() => _WiFiCurrentCardState();
}

class _WiFiCurrentCardState extends State<WiFiCurrentCard> {
  String ssid = "-";
  String ip = "-";
  int rssi = 0;
  bool connected = false;

  StreamSubscription? _sub;

  @override
  void initState() {
    super.initState();
    _sub = WsControlApi.stream().listen((msg) {
      if (msg is! Map) return;
      if (msg["type"] != "wifi_status") return;
      if (!mounted) return;

      setState(() {
        connected = msg["sta_connected"];
        ssid = msg["sta_ssid"] ?? "-";
        ip = msg["sta_ip"] ?? "-";
        rssi = msg["rssi"] ?? 0;
      });
    });
    WsControlApi.wifiStatus();
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              "目前 Wi-Fi",
              style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
            ),

            const SizedBox(height: 8),

            Text(
              connected ? "已連線：$ssid" : "未連線",
              style: TextStyle(color: connected ? Colors.green : Colors.red),
            ),

            if (connected) ...[
              const SizedBox(height: 4),
              Text("IP：$ip"),
              Text("RSSI：$rssi dBm"),
            ] else ...[
              const SizedBox(height: 4),
              const Text(
                "等待 ESP32 回傳狀態…",
                style: TextStyle(color: Colors.grey),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
