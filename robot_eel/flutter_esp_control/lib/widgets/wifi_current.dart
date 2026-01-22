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

  @override
  void initState() {
    super.initState();
    WsControlApi.stream().listen((msg) {
      if (msg["type"] != "wifi_status") return;
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
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text("目前 Wi-Fi", style: TextStyle(fontSize: 20)),
            Text(connected ? "已連線：$ssid" : "未連線"),
            if (connected) Text("IP：$ip  RSSI：$rssi"),
          ],
        ),
      ),
    );
  }
}
