import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class WiFiScanCard extends StatefulWidget {
  const WiFiScanCard({super.key});

  @override
  State<WiFiScanCard> createState() => _WiFiScanCardState();
}

class _WiFiScanCardState extends State<WiFiScanCard> {
  List<Map<String, dynamic>> aps = [];
  bool scanning = false;
  StreamSubscription? _sub;

  @override
  void initState() {
    super.initState();

    _sub = WsControlApi.stream().listen((msg) {
      if (msg is! Map) return;
      if (msg["type"] != "wifi_scan") return;
      if (!mounted) return;

      setState(() {
        aps = List<Map<String, dynamic>>.from(msg["list"]);
        scanning = false;
      });
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  void startScan() {
    setState(() {
      scanning = true;
      aps.clear();
    });

    WsControlApi.wifiScan(); // 送 cmd
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
            const Text("附近 Wi-Fi", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 8),

            ElevatedButton(
              onPressed: scanning ? null : startScan,
              child: Text(scanning ? "掃描中…" : "掃描 Wi-Fi"),
            ),

            const SizedBox(height: 8),

            SizedBox(
              height: 300,
              child: scanning
                  ? const Center(child: CircularProgressIndicator())
                  : aps.isEmpty
                  ? const Center(child: Text("尚未掃描"))
                  : ListView.builder(
                      itemCount: aps.length,
                      itemBuilder: (_, i) {
                        final ap = aps[i];
                        return ListTile(
                          title: Text(ap["ssid"] ?? ""),
                          subtitle: Text("RSSI: ${ap["rssi"]}"),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}
