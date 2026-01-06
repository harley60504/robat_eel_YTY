import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class ModeSwitch extends StatefulWidget {
  const ModeSwitch({super.key});

  @override
  State<ModeSwitch> createState() => _ModeSwitchState();
}

class _ModeSwitchState extends State<ModeSwitch> {
  int mode = -1;

  @override
  void initState() {
    super.initState();

    WsControlApi.stream().listen((msg) {
      if (msg["type"] == "ctrl_params") {
        setState(() => mode = msg["mode"] ?? -1);
      }
    });

    WsControlApi.getParams();
  }

  void setMode(int m) {
    WsControlApi.setParam({"mode": m});
  }

  Widget modeBtn(String name, int m) {
    final isSel = mode == m;
    return ElevatedButton(
      style: ElevatedButton.styleFrom(
        backgroundColor: isSel ? Colors.blue : null,
      ),
      onPressed: () => setMode(m),
      child: Text(name),
    );
  }

  String modeName() {
    switch (mode) {
      case 0:
        return "Sin";
      case 1:
        return "CPG";
      case 2:
        return "Offset";
      default:
        return "-";
    }
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
            const Text("模式切換", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 10),

            Wrap(
              spacing: 10,
              children: [
                modeBtn("Sin", 0),
                modeBtn("CPG", 1),
                modeBtn("Offset", 2),
              ],
            ),

            const SizedBox(height: 10),

            Text("目前模式： ${modeName()}"),
          ],
        ),
      ),
    );
  }
}
