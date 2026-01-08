import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class WiFiSavedCard extends StatefulWidget {
  const WiFiSavedCard({super.key});

  static void Function(String ssid)? addIfMissing;

  @override
  State<WiFiSavedCard> createState() => _WiFiSavedCardState();
}

class _WiFiSavedCardState extends State<WiFiSavedCard> {
  List<Map<String, String>> saved = [];

  void addOnce(String ssid) {
    if (ssid.isEmpty) return;

    final exist = saved.any((e) => e["ssid"] == ssid);
    if (!exist) {
      setState(() {
        saved.add({"ssid": ssid, "pass": "******"});
      });
    }
  }

  void saveToESP(String ssid, String pass) {
    WsControlApi.wifiSave(ssid, pass);

    setState(() {
      saved.removeWhere((e) => e["ssid"] == ssid);
      saved.add({"ssid": ssid, "pass": pass});
    });
  }

  @override
  void initState() {
    super.initState();

    WiFiSavedCard.addIfMissing = (ssid) {
      addOnce(ssid);
    };
  }

  void editDialog(String ssid, String pass) {
    final ctrl = TextEditingController(text: pass == "******" ? "" : pass);

    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: Text("編輯 $ssid"),
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
              saveToESP(ssid, ctrl.text);
            },
            child: const Text("儲存"),
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
              "已儲存 Wi-Fi",
              style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
            ),

            const SizedBox(height: 10),

            if (saved.isEmpty) const Text("尚未儲存 Wi-Fi"),

            ...saved.map((e) {
              final ssid = e["ssid"]!;
              final pass = e["pass"]!;

              return ListTile(
                title: Text(ssid),
                subtitle: const Text("已儲存"),
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    IconButton(
                      icon: const Icon(Icons.edit),
                      onPressed: () => editDialog(ssid, pass),
                    ),

                    IconButton(
                      icon: const Icon(Icons.delete),
                      onPressed: () {
                        setState(() => saved.remove(e));
                      },
                    ),
                  ],
                ),
              );
            }),
          ],
        ),
      ),
    );
  }
}
