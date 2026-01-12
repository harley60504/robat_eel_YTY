import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class WiFiSavedCard extends StatefulWidget {
  const WiFiSavedCard({super.key});

  @override
  State<WiFiSavedCard> createState() => _WiFiSavedCardState();
}

class _WiFiSavedCardState extends State<WiFiSavedCard> {
  final List<String> saved = [];
  StreamSubscription? _sub;

  @override
  void initState() {
    super.initState();

    _sub = WsControlApi.stream().listen((msg) {
      if (msg is! Map) return;
      if (msg["type"] != "wifi_list") return;
      if (!mounted) return;

      setState(() {
        saved
          ..clear()
          ..addAll((msg["list"] as List).map((e) => e["ssid"].toString()));
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
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text("已儲存 Wi-Fi", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 8),

            if (saved.isEmpty) const Text("尚未儲存"),

            ...saved.map(
              (ssid) => ListTile(
                title: Text(ssid),
                trailing: IconButton(
                  icon: const Icon(Icons.delete),
                  onPressed: () {
                    WsControlApi.wifiDelete(ssid);
                    setState(() => saved.remove(ssid));
                  },
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
