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
      TextEditingController(text: "192.168.50.233");

  bool running = false;
  String logText = "";

  double base = 120;
  double amp = 30;
  double freq = 0.6;
  double phaseStep = 0.7;
  int intervalMs = 50;

  void log(String s) {
    setState(() => logText = "$s\n$logText");
  }

  // ===========================
  Future<void> onSync() async {
    final pcHost = pcIpCtrl.text.trim();
    log("sync ESP32 host -> Python ...");

    final ok = await PythonBridge.syncEsp32HostToPython(pcHost: pcHost);
    log("sync ok = $ok");

    if (!ok) {
      log("❌ sync 失敗：請確認 PC IP 正確 & Python 有啟動");
    }
  }

  // ===========================
  Future<void> onStart() async {
    final pcHost = pcIpCtrl.text.trim();

    log("1) sync ESP32 host -> Python ...");
    final syncOk = await PythonBridge.syncEsp32HostToPython(pcHost: pcHost);
    log("   sync ok = $syncOk");
    if (!syncOk) return;

    log("2) start python loop ...");
    final ok = await PythonApi.start(
      pcHost: pcHost,
      base: base,
      amp: amp,
      freq: freq,
      phaseStep: phaseStep,
      intervalMs: intervalMs,
    );

    log("   start ok = $ok");
    setState(() => running = ok);
  }

  // ===========================
  Future<void> onStop() async {
    final pcHost = pcIpCtrl.text.trim();
    final ok = await PythonApi.stop(pcHost: pcHost);
    log("stop ok = $ok");

    setState(() => running = false);
  }

  // ===========================
  Future<void> onApplyParams() async {
    final pcHost = pcIpCtrl.text.trim();

    final ok = await PythonApi.setParams(
      pcHost: pcHost,
      base: base,
      amp: amp,
      freq: freq,
      phaseStep: phaseStep,
      intervalMs: intervalMs,
    );

    log("set_params ok=$ok");
  }

  // ===========================
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
              "Python Controller（PC）",
              style: TextStyle(fontWeight: FontWeight.w700, fontSize: 16),
            ),
            const SizedBox(height: 8),
            const Text(
              "PC IP（FastAPI port=8000）",
              style: TextStyle(fontWeight: FontWeight.w600),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: pcIpCtrl,
              decoration: const InputDecoration(
                border: OutlineInputBorder(),
                hintText: "例如 192.168.50.233",
              ),
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 12,
              runSpacing: 8,
              children: [
                ElevatedButton(
                  onPressed: onSync,
                  child: const Text("同步 ESP32 Host → Python"),
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
                  onPressed: running ? onApplyParams : null,
                  child: const Text("套用參數"),
                ),
              ],
            ),
            const SizedBox(height: 16),
            _slider("base", base, 0, 240, (v) => setState(() => base = v)),
            _slider("amp", amp, 0, 120, (v) => setState(() => amp = v)),
            _slider("freq", freq, 0.1, 3.0, (v) => setState(() => freq = v)),
            _slider(
              "phaseStep",
              phaseStep,
              0.0,
              3.14,
              (v) => setState(() => phaseStep = v),
            ),
            const SizedBox(height: 12),
            Text("interval_ms = $intervalMs"),
            Slider(
              min: 10,
              max: 200,
              divisions: 19,
              value: intervalMs.toDouble(),
              onChanged: (v) => setState(() => intervalMs = v.toInt()),
            ),
            const SizedBox(height: 12),
            const Text("Log", style: TextStyle(fontWeight: FontWeight.w700)),
            const SizedBox(height: 8),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                border: Border.all(color: Colors.black12),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(logText.isEmpty ? "(no logs)" : logText),
            ),
          ],
        ),
      ),
    );
  }

  Widget _slider(
    String name,
    double value,
    double min,
    double max,
    ValueChanged<double> onChanged,
  ) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text("$name = ${value.toStringAsFixed(2)}"),
        Slider(
          min: min,
          max: max,
          value: value.clamp(min, max),
          onChanged: onChanged,
        ),
      ],
    );
  }
}
