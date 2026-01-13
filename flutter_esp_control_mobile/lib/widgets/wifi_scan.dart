import 'package:flutter/material.dart';
import '../api/esp_http_api.dart';
import 'wifi_bars.dart';

class WiFiScanCard extends StatefulWidget {
  const WiFiScanCard({super.key});

  @override
  State<WiFiScanCard> createState() => _WiFiScanCardState();
}

class _WiFiScanCardState extends State<WiFiScanCard> {
  bool scanning = false;
  List<Map<String, dynamic>> aps = [];
  String error = "";

  Future<void> scan() async {
    setState(() {
      scanning = true;
      error = "";
      aps.clear();
    });

    try {
      aps = await EspHttpApi.wifiScan();
    } catch (e) {
      error = e.toString();
    }

    setState(() => scanning = false);
  }

  Future<void> connectDialog(String ssid) async {
    final controller = TextEditingController();
    bool connecting = false;

    await showDialog(
      context: context,
      builder: (_) => StatefulBuilder(
        builder: (context, setStateDialog) {
          return AlertDialog(
            title: Text("連線到 $ssid"),
            content: TextField(
              controller: controller,
              obscureText: true,
              decoration: const InputDecoration(
                labelText: "Wi-Fi 密碼",
              ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text("取消"),
              ),
              ElevatedButton(
                onPressed: connecting
                    ? null
                    : () async {
                        setStateDialog(() => connecting = true);

                        try {
                          final ok = await EspHttpApi.wifiConnect(
                            ssid,
                            controller.text,
                          );

                          if (!context.mounted) return;

                          Navigator.pop(context);

                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text(ok
                                  ? "已連線並儲存 $ssid"
                                  : "連線失敗"),
                            ),
                          );
                        } catch (e) {
                          setStateDialog(() => connecting = false);
                        }
                      },
                child: Text(connecting ? "連線中…" : "連線"),
              ),
            ],
          );
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text("附近 Wi-Fi", style: TextStyle(fontSize: 20)),

            const SizedBox(height: 8),

            ElevatedButton(
              onPressed: scanning ? null : scan,
              child: Text(scanning ? "掃描中…" : "掃描"),
            ),

            const SizedBox(height: 8),

            if (error.isNotEmpty)
              Text("錯誤：$error",
                  style: const TextStyle(color: Colors.red))
            else if (aps.isEmpty)
              const Text("尚未掃描")
            else
              ...aps.map(
                (ap) => ListTile(
                  title: Text(ap['ssid'] ?? ''),
                  subtitle: Text("RSSI: ${ap['rssi']}"),
                  trailing: wifiBars(ap['rssi']),
                  onTap: () => connectDialog(ap['ssid']),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
