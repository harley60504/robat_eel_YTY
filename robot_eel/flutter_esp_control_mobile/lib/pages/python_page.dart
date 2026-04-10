import 'package:flutter/material.dart';
import '../api/python_api.dart';
import '../bridge/python_bridge.dart';

class PythonPage extends StatefulWidget {
  const PythonPage({super.key});

  @override
  State<PythonPage> createState() => _PythonPageState();
}

class _PythonPageState extends State<PythonPage> {
  final TextEditingController pcIpCtrl =
      TextEditingController(text: "127.0.0.1");

  bool running = false;
  bool measuring = false;
  String logText = "";

  void log(String s) {
    if (!mounted) return;
    setState(() => logText = "$s\n$logText");
  }

  Future<void> onSync() async {
    final pcHost = pcIpCtrl.text.trim();
    log("sync ESP32 host -> Python ...");

    final ok = await PythonBridge.syncEsp32HostToPython(pcHost: pcHost);
    if (!mounted) return;
    log("sync ok = $ok");
  }

  Future<void> onStart() async {
    final pcHost = pcIpCtrl.text.trim();

    log("start python...");
    final ok = await PythonApi.start(pcHost: pcHost);

    if (!mounted) return;
    setState(() => running = ok);
    log("start ok = $ok");
  }

  Future<void> onStop() async {
    final pcHost = pcIpCtrl.text.trim();
    final ok = await PythonApi.stop(pcHost: pcHost);

    if (!mounted) return;
    setState(() => running = false);
    log("stop ok = $ok");
  }

  Future<void> onMeasureToggle() async {
    final pcHost = pcIpCtrl.text.trim();

    if (!measuring) {
      final ok = await PythonApi.measureOn(pcHost: pcHost);
      if (!mounted) return;

      setState(() => measuring = ok);
      log(ok ? "measure ON" : "measure ON failed");
    } else {
      final ok = await PythonApi.measureOff(pcHost: pcHost);
      if (!mounted) return;

      setState(() => measuring = ok ? false : true);
      log(ok ? "measure OFF" : "measure OFF failed");
    }
  }

  @override
  void dispose() {
    pcIpCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              "Python Controller",
              style: TextStyle(fontWeight: FontWeight.w700, fontSize: 16),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: pcIpCtrl,
              decoration: const InputDecoration(
                border: OutlineInputBorder(),
                hintText: "PC IP",
              ),
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 12,
              children: [
                ElevatedButton(
                  onPressed: onSync,
                  child: const Text("Sync"),
                ),
                ElevatedButton(
                  onPressed: running ? null : onStart,
                  child: const Text("Start"),
                ),
                OutlinedButton(
                  onPressed: running ? onStop : null,
                  child: const Text("Stop"),
                ),
                OutlinedButton(
                  onPressed: onMeasureToggle,
                  child: Text(measuring ? "Measure OFF" : "Measure ON"),
                ),
              ],
            ),
            const SizedBox(height: 16),
            const Text("Log"),
            Container(
              width: double.infinity,
              height: 180,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                border: Border.all(color: Colors.black12),
              ),
              child: SingleChildScrollView(
                child: Text(logText.isEmpty ? "(no logs)" : logText),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
