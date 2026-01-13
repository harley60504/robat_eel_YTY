import 'package:flutter/material.dart';
import '../api/esp_http_api.dart';
import '../net/host_resolver.dart';
import '../config.dart';

class WiFiStatusCard extends StatefulWidget {
  const WiFiStatusCard({super.key});

  @override
  State<WiFiStatusCard> createState() => _WiFiStatusCardState();
}

class _WiFiStatusCardState extends State<WiFiStatusCard> {
  bool loading = true;
  String error = "";

  // current
  bool connected = false;
  String ssid = "-";
  String ip = "-";
  int rssi = 0;

  // saved list
  List<String> saved = [];

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
      // ✅ current
      final current = await EspHttpApi.wifiCurrent();
      connected = current['connected'] ?? false;
      ssid = current['ssid'] ?? "-";
      ip = current['ip'] ?? "-";
      rssi = current['rssi'] ?? 0;

      // ✅ saved list
      saved = await EspHttpApi.wifiSaved();

      // ✅ connected + got ip => update cache
      if (connected && ip != "-") {
        await HostResolver.updateCachesByStaIp(ip);
      }
    } catch (e) {
      error = e.toString();
    }

    if (!mounted) return;
    setState(() => loading = false);
  }

  /// ✅ 連線（同時存進 ESP32 Saved List）
  Future<void> connectDialog(String targetSsid) async {
    final controller = TextEditingController();
    bool connecting = false;
    String dialogError = "";

    await showDialog(
      context: context,
      builder: (_) => StatefulBuilder(
        builder: (context, setStateDialog) {
          return AlertDialog(
            title: Text("連線到 $targetSsid"),
            content: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: controller,
                  obscureText: true,
                  decoration: const InputDecoration(labelText: "Wi-Fi 密碼"),
                ),
                if (dialogError.isNotEmpty) ...[
                  const SizedBox(height: 8),
                  Text(dialogError, style: const TextStyle(color: Colors.red)),
                ],
              ],
            ),
            actions: [
              TextButton(
                onPressed: connecting ? null : () => Navigator.pop(context),
                child: const Text("取消"),
              ),
              ElevatedButton(
                onPressed: connecting
                    ? null
                    : () async {
                        setStateDialog(() {
                          connecting = true;
                          dialogError = "";
                        });

                        try {
                          // ✅ ESP32 連線 + 儲存
                          final ok = await EspHttpApi.wifiConnect(
                            targetSsid,
                            controller.text,
                          );

                          if (!context.mounted) return;

                          if (!ok) {
                            setStateDialog(() {
                              connecting = false;
                              dialogError = "連線失敗";
                            });
                            return;
                          }

                          // ✅ 成功：關閉 dialog
                          Navigator.pop(context);

                          // ✅ 刷新狀態（拿到 STA IP）
                          await refresh();

                          // ✅ 如果已拿到 STA IP，切到 STA（變成 last_ip）
                          if (connected && ip != "-") {
                            await HostResolver.updateCachesByStaIp(ip);
                            await ApiConfig.setHost(ip);
                          }

                          if (!mounted) return;
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(content: Text("已連線並儲存 $targetSsid")),
                          );
                        } catch (e) {
                          setStateDialog(() {
                            connecting = false;
                            dialogError = "錯誤：$e";
                          });
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

  /// ✅ 刪除 Saved Wi-Fi
  Future<void> deleteSaved(String targetSsid) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text("刪除已儲存 Wi-Fi"),
        content: Text("確定要刪除：$targetSsid ？"),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text("取消"),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text("刪除"),
          ),
        ],
      ),
    );

    if (ok != true) return;

    try {
      final done = await EspHttpApi.wifiDelete(targetSsid);

      if (!mounted) return;

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(done ? "已刪除 $targetSsid" : "刪除失敗")),
      );

      await refresh();
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text("刪除錯誤：$e")));
    }
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text("Wi-Fi 狀態總覽", style: TextStyle(fontSize: 20)),
              const SizedBox(height: 8),

              if (loading)
                const Text("讀取中…")
              else if (error.isNotEmpty)
                Text("錯誤：$error", style: const TextStyle(color: Colors.red))
              else ...[
                // ===== Current =====
                Text(
                  "ESP32 連線狀態：${connected ? "已連線" : "未連線"}",
                  style: TextStyle(
                    color: connected ? Colors.black : Colors.red,
                  ),
                ),
                SelectableText("SSID：$ssid"),
                SelectableText("IP：$ip"),
                Text("RSSI：$rssi dBm"),

                const Divider(height: 24),

                // ===== Saved =====
                const Text("已儲存 Wi-Fi", style: TextStyle(fontSize: 16)),
                const SizedBox(height: 8),

                if (saved.isEmpty)
                  const Text("尚未儲存")
                else
                  ...saved.map(
                    (s) => ListTile(
                      contentPadding: EdgeInsets.zero,
                      title: Text(s),
                      subtitle: const Text("點右側可連線 / 刪除"),
                      trailing: Wrap(
                        spacing: 6,
                        children: [
                          IconButton(
                            tooltip: "重新連線 / 修改密碼",
                            icon: const Icon(Icons.link),
                            onPressed: () => connectDialog(s),
                          ),
                          IconButton(
                            tooltip: "刪除",
                            icon: const Icon(Icons.delete_outline),
                            onPressed: () => deleteSaved(s),
                          ),
                        ],
                      ),
                    ),
                  ),
              ],

              const SizedBox(height: 8),
              ElevatedButton(onPressed: refresh, child: const Text("重新讀取")),
            ],
          ),
        ),
      ),
    );
  }
}
