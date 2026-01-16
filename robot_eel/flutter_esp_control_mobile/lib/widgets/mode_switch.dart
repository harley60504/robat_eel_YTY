import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../ui/ui_card.dart';
import '../ui/ui_layout.dart';

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

    final cached = WsControlApi.lastCtrlParams;
    if (cached != null) {
      mode = cached["mode"] ?? -1;
    }

    WsControlApi.ctrlParamsNotifier.addListener(_onCtrlParamsChanged);
  }

  void _onCtrlParamsChanged() {
    final msg = WsControlApi.ctrlParamsNotifier.value;
    if (!mounted || msg == null) return;

    final newMode = msg["mode"] ?? -1;
    if (newMode != mode) {
      setState(() => mode = newMode);
    }
  }

  @override
  void dispose() {
    WsControlApi.ctrlParamsNotifier.removeListener(_onCtrlParamsChanged);
    super.dispose();
  }

  void setMode(int m) {
    WsControlApi.setParam({"mode": m});
  }

  Widget modeBtn(String name, int m) {
    final isSel = mode == m;

    return SizedBox(
      height: UiLayout.buttonHeight,
      child: ElevatedButton(
        style: ElevatedButton.styleFrom(
          backgroundColor: isSel ? Colors.blue : null,
          padding: const EdgeInsets.symmetric(horizontal: 18),
        ),
        onPressed: () => setMode(m),
        child: Text(name),
      ),
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
      case 3:
        return "UART";
      default:
        return "-";
    }
  }

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "模式切換",
      minHeight: 160, // ✅ 比其他卡片稍高一點
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Wrap(
            spacing: 10,
            runSpacing: 8,
            children: [
              modeBtn("Sin", 0),
              modeBtn("CPG", 1),
              modeBtn("Offset", 2),
              modeBtn("UART", 3),
            ],
          ),
          const SizedBox(height: 12),
          Text("目前模式： ${modeName()}"),
        ],
      ),
    );
  }
}
