import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class SystemStatus extends StatefulWidget {
  const SystemStatus({super.key});

  @override
  State<SystemStatus> createState() => _SystemStatusState();
}

class _SystemStatusState extends State<SystemStatus> {
  double freq = double.nan;
  double amp = double.nan;
  double lambda = double.nan;
  double L = double.nan;
  double fbGain = double.nan;
  bool paused = false;

  @override
  void initState() {
    super.initState();

    WsControlApi.stream().listen((msg) {
      if (msg["type"] != "ctrl_params") return;
      if (!mounted) return;

      setState(() {
        freq = (msg["frequency"] ?? double.nan).toDouble();
        amp = (msg["Ajoint"] ?? double.nan).toDouble();
        lambda = (msg["lambda"] ?? double.nan).toDouble();
        L = (msg["L"] ?? double.nan).toDouble();
        fbGain = (msg["feedbackGain"] ?? double.nan).toDouble();
        paused = (msg["paused"] ?? false);
      });
    });
  }

  String fmt(double v, {String unit = ""}) {
    return v.isNaN ? "-" : "${v.toStringAsFixed(2)}$unit";
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
            const Text("系統狀態", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 10),

            Text("頻率：${fmt(freq, unit: " Hz")}"),
            Text("振幅：${fmt(amp, unit: " °")}"),
            Text("λ：${fmt(lambda)}"),
            Text("L：${fmt(L)}"),
            Text("回授權重：${fmt(fbGain)}"),
            Text("狀態：${paused ? "暫停" : "運行中"}"),
          ],
        ),
      ),
    );
  }
}
