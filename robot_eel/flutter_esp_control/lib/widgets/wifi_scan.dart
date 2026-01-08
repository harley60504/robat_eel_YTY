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

  StreamSubscription? sub;

  @override
  void initState() {
    super.initState();

    sub = WsControlApi.stream().listen((msg) {
      if (msg is! Map) return;
      if (!mounted) return;
      if (msg["type"] != "wifi_scan") return;

      setState(() {
        aps = (msg["list"] as List)
            .map((e) => Map<String, dynamic>.from(e))
            .toList();
        scanning = false;
      });
    });
  }

  @override
  void dispose() {
    sub?.cancel();
    super.dispose();
  }

  void startScan() {
    setState(() {
      scanning = true;
      aps.clear();
    });
    WsControlApi.wifiScan();
  }

  void connect(String ssid) {
    final ctrl = TextEditingController();

    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: Text("連線到 $ssid"),
        content: TextField(
          controller: ctrl,
          obscureText: true,
          decoration: const InputDecoration(labelText: "密碼"),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text("取消"),
          ),
          ElevatedButton(
            onPressed: () {
              Navigator.pop(context);
              WsControlApi.wifiConnect(ssid, ctrl.text);
            },
            child: const Text("連線"),
          ),
        ],
      ),
    );
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
              "附近 Wi-Fi",
              style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
            ),

            const SizedBox(height: 10),

            ElevatedButton(
              onPressed: scanning ? null : startScan,
              child: Text(scanning ? "掃描中…" : "掃描 Wi-Fi"),
            ),

            const SizedBox(height: 10),

            SizedBox(
              height: 300,
              child: aps.isEmpty
                  ? Center(
                      child: Text(
                        scanning ? "掃描中…" : "尚未掃描",
                        style: const TextStyle(color: Colors.grey),
                      ),
                    )
                  : ListView.builder(
                      itemCount: aps.length,
                      itemBuilder: (_, i) {
                        final ap = aps[i];
                        final ssid = ap["ssid"] ?? "";
                        final rssi = ap["rssi"] ?? 0;

                        return ListTile(
                          title: Text(ssid),
                          subtitle: Text("RSSI: $rssi"),
                          onTap: () => connect(ssid),
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
