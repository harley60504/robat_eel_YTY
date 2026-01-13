import 'package:flutter/material.dart';
import '../api/esp_http_api.dart';

class WiFiSavedCard extends StatefulWidget {
  const WiFiSavedCard({super.key});

  @override
  State<WiFiSavedCard> createState() => _WiFiSavedCardState();
}

class _WiFiSavedCardState extends State<WiFiSavedCard> {
  bool loading = true;
  List<String> saved = [];
  String error = "";

  @override
  void initState() {
    super.initState();
    refresh();
  }

  Future<void> refresh() async {
    setState(() {
      loading = true;
      error = "";
    });

    try {
      saved = await EspHttpApi.wifiSaved();
    } catch (e) {
      error = e.toString();
    }

    setState(() => loading = false);
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

            if (loading)
              const Text("讀取中…")
            else if (error.isNotEmpty)
              Text("錯誤：$error", style: const TextStyle(color: Colors.red))
            else if (saved.isEmpty)
              const Text("尚未儲存")
            else
              ...saved.map((s) => ListTile(title: Text(s))),

            const SizedBox(height: 8),
            ElevatedButton(
              onPressed: refresh,
              child: const Text("重新讀取"),
            ),
          ],
        ),
      ),
    );
  }
}
