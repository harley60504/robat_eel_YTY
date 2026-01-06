import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class MotionParam extends StatefulWidget {
  const MotionParam({super.key});

  @override
  State<MotionParam> createState() => _MotionParamState();
}

class _MotionParamState extends State<MotionParam> {
  final freqCtrl = TextEditingController();
  final ampCtrl = TextEditingController();
  final lamCtrl = TextEditingController();
  final lCtrl = TextEditingController();

  bool firstSync = true;

  @override
  void initState() {
    super.initState();

    // 先請 ESP32 回傳目前參數
    WsControlApi.getParams();

    // 監聽 WS
    WsControlApi.stream().listen((msg) {
      if (msg["type"] != "ctrl_params") return;

      // 只在第一次同步更新 input 預設值
      if (firstSync) {
        freqCtrl.text = msg["frequency"].toStringAsFixed(2);
        ampCtrl.text = msg["Ajoint"].toStringAsFixed(2);
        lamCtrl.text = msg["lambda"].toStringAsFixed(2);
        lCtrl.text = msg["L"].toStringAsFixed(2);
        firstSync = false;
      }
    });
  }

  void setParam(String key, TextEditingController ctrl) {
    final v = double.tryParse(ctrl.text);
    if (v == null) return;

    WsControlApi.setParam({key: v});
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
            const Text("參數設定", style: TextStyle(fontSize: 20)),

            paramRow(
              "頻率 (Hz)",
              freqCtrl,
              () => setParam("frequency", freqCtrl),
            ),
            paramRow("振幅 (°)", ampCtrl, () => setParam("Ajoint", ampCtrl)),
            paramRow("λ", lamCtrl, () => setParam("lambda", lamCtrl)),
            paramRow("L", lCtrl, () => setParam("L", lCtrl)),
          ],
        ),
      ),
    );
  }

  Widget paramRow(
    String label,
    TextEditingController ctrl,
    VoidCallback onSet,
  ) {
    return Padding(
      padding: const EdgeInsets.only(top: 8),

      child: Row(
        children: [
          SizedBox(width: 100, child: Text(label)),
          const SizedBox(width: 6),

          Expanded(
            child: TextField(
              controller: ctrl,
              keyboardType: TextInputType.number,
              decoration: const InputDecoration(border: OutlineInputBorder()),
            ),
          ),

          const SizedBox(width: 6),

          ElevatedButton(onPressed: onSet, child: const Text("設定")),
        ],
      ),
    );
  }
}
