import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class SystemStatus extends StatelessWidget {
  const SystemStatus({super.key});

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: StreamBuilder(
          stream: WsControlApi.stream(),
          builder: (context, snapshot) {
            double freq = double.nan;
            double amp = double.nan;
            double lambda = double.nan;
            double L = double.nan;
            double fbGain = double.nan;
            bool paused = false;

            if (snapshot.hasData) {
              final data = snapshot.data;

              if (data["type"] == "ctrl_params") {
                freq = (data["frequency"] ?? double.nan).toDouble();
                amp = (data["Ajoint"] ?? double.nan).toDouble();
                lambda = (data["lambda"] ?? double.nan).toDouble();
                L = (data["L"] ?? double.nan).toDouble();
                fbGain = (data["feedbackGain"] ?? double.nan).toDouble();
                paused = (data["paused"] ?? false);
              }
            }

            String fmt(double v, {String unit = ""}) {
              return v.isNaN ? "-" : "${v.toStringAsFixed(2)}$unit";
            }

            return Column(
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
            );
          },
        ),
      ),
    );
  }
}
