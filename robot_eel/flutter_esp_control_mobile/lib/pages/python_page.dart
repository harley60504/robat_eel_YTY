import 'package:flutter/material.dart';
import '../api/python_api.dart';
import '../bridge/python_bridge.dart';
import '../bridge/python_process_launcher.dart';
import '../config.dart';
import '../ui/ui_card.dart';

class PythonPage extends StatefulWidget {
  final bool compact;
  final bool fillHeight;
  final bool embedded;

  const PythonPage({
    super.key,
    this.compact = false,
    this.fillHeight = false,
    this.embedded = false,
  });

  @override
  State<PythonPage> createState() => _PythonPageState();
}

class _PythonPageState extends State<PythonPage> {
  bool running = false;
  bool busy = false;
  bool measuring = false;
  String selectedGait = "straight_rl";
  String outputMode = "cpg";
  List<Map<String, dynamic>> gaits = const [
    {"key": "straight_rl", "label": "Straight RL"},
    {"key": "left_turn_rl", "label": "Left Turn RL"},
    {"key": "left_spin_rl", "label": "Left Strong RL"},
    {"key": "right_turn_rl", "label": "Right Turn RL"},
    {"key": "right_spin_rl", "label": "Right Strong RL"},
  ];
  String logText = "";

  @override
  void initState() {
    super.initState();
    loadGaits();
  }

  void log(String s) {
    if (!mounted) return;
    setState(() => logText = "$s\n$logText");
  }

  Future<void> loadGaits() async {
    final loaded = await PythonApi.gaits(pcHost: ApiConfig.pythonHost);
    if (!mounted || loaded.isEmpty) return;
    setState(() {
      gaits = loaded;
      selectedGait = loaded.first["key"]?.toString() ?? selectedGait;
    });
  }

  Future<void> onStart() async {
    if (busy) return;
    final pcHost = ApiConfig.pythonHost;
    setState(() => busy = true);

    try {
      log("check python API ...");
      var ready = await PythonApi.ping(pcHost: pcHost);

      if (!ready) {
        log("python API offline, try local launch ...");
        final launch = await PythonProcessLauncher.launch();
        log(launch.message);

        if (launch.ok) {
          ready = await PythonApi.waitUntilReady(pcHost: pcHost);
          log("python API ready = $ready");
        }
      }

      if (!ready) {
        log("python API not ready, cannot start");
        return;
      }

      log("sync ESP32 host -> Python ...");
      final syncOk = await PythonBridge.syncEsp32HostToPython(pcHost: pcHost);
      if (!mounted) return;
      log("sync ok = $syncOk");

      log("set gait/output ...");
      final results = await Future.wait([
        PythonApi.setGait(pcHost: pcHost, gait: selectedGait),
        PythonApi.setOutputMode(pcHost: pcHost, outputMode: outputMode),
      ]);
      log("gait ok = ${results[0]}, output ok = ${results[1]}");

      log("start python...");
      final ok = await PythonApi.start(pcHost: pcHost);

      if (!mounted) return;
      setState(() => running = ok);
      log("start ok = $ok");
    } finally {
      if (mounted) setState(() => busy = false);
    }
  }

  Future<void> onStop() async {
    if (busy) return;
    final pcHost = ApiConfig.pythonHost;
    setState(() {
      busy = true;
      running = false;
    });

    try {
      final ok = await PythonApi.stop(pcHost: pcHost);
      log("stop ok = $ok");
    } finally {
      if (mounted) setState(() => busy = false);
    }
  }

  Future<void> onMeasureToggle() async {
    final pcHost = ApiConfig.pythonHost;

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
  Widget build(BuildContext context) {
    final content = buildContent(context);

    if (widget.embedded) return content;

    return UiCard(
      title: "Python",
      minHeight: widget.fillHeight ? 0 : (widget.compact ? 260 : 340),
      fill: widget.fillHeight,
      child: content,
    );
  }

  Widget buildContent(BuildContext context) {
    final logBox = Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        border: Border.all(color: Theme.of(context).dividerColor),
        borderRadius: BorderRadius.circular(8),
      ),
      child: SingleChildScrollView(
        child: Text(logText.isEmpty ? "(no logs)" : logText),
      ),
    );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: widget.fillHeight ? MainAxisSize.max : MainAxisSize.min,
      children: [
        Text(
          "Python API: ${ApiConfig.pythonHost}:${ApiConfig.pythonPort}",
          style: const TextStyle(color: Colors.white70),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: DropdownButtonFormField<String>(
                initialValue: selectedGait,
                decoration: const InputDecoration(labelText: "Gait"),
                items: gaits.map((item) {
                  final key = item["key"].toString();
                  final label = item["label"]?.toString() ?? key;
                  return DropdownMenuItem(value: key, child: Text(label));
                }).toList(),
                onChanged: running || busy
                    ? null
                    : (value) {
                        if (value == null) return;
                        setState(() => selectedGait = value);
                      },
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: DropdownButtonFormField<String>(
                initialValue: outputMode,
                decoration: const InputDecoration(labelText: "Output"),
                items: const [
                  DropdownMenuItem(value: "angle", child: Text("Mode 3 Angle")),
                  DropdownMenuItem(value: "cpg", child: Text("Mode 1 CPG")),
                ],
                onChanged: running || busy
                    ? null
                    : (value) {
                        if (value == null) return;
                        setState(() => outputMode = value);
                      },
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: ElevatedButton(
                onPressed: running || busy ? null : onStart,
                child: FittedBox(child: Text(busy ? "Wait" : "Start")),
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: OutlinedButton(
                onPressed: running && !busy ? onStop : null,
                child: const FittedBox(child: Text("Stop")),
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: OutlinedButton(
                onPressed: onMeasureToggle,
                child: FittedBox(
                  child: Text(measuring ? "Meas OFF" : "Meas ON"),
                ),
              ),
            ),
          ],
        ),
        SizedBox(height: widget.compact ? 10 : 16),
        const Text("Log"),
        const SizedBox(height: 6),
        if (widget.fillHeight)
          Expanded(child: logBox)
        else
          SizedBox(height: widget.compact ? 120 : 180, child: logBox),
      ],
    );
  }
}
