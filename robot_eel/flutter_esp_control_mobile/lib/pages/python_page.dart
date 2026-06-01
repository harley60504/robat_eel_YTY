import 'package:flutter/material.dart';
import '../api/python_api.dart';
import '../bridge/python_bridge.dart';
import '../ui/ui_card.dart';

class PythonPage extends StatefulWidget {
  final bool compact;
  final bool fillHeight;

  const PythonPage({super.key, this.compact = false, this.fillHeight = false});

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

  Future<void> onStart() async {
    final pcHost = pcIpCtrl.text.trim();

    log("sync ESP32 host -> Python ...");
    final syncOk = await PythonBridge.syncEsp32HostToPython(pcHost: pcHost);
    if (!mounted) return;
    log("sync ok = $syncOk");

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
    final content = buildContent(context);

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
        TextField(
          controller: pcIpCtrl,
          decoration: const InputDecoration(
            border: OutlineInputBorder(),
            hintText: "PC IP",
          ),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: ElevatedButton(
                onPressed: running ? null : onStart,
                child: const FittedBox(child: Text("Start")),
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: OutlinedButton(
                onPressed: running ? onStop : null,
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
