import 'package:flutter/material.dart';

import 'servo_control_panel.dart';
import '../pages/python_page.dart';
import '../ui/ui_layout.dart';

/// ✅ UART 模式右側面板：
/// 1) ServoControlPanel（手動 UART slider）
/// 2) PythonPage（PC Python 波形控制）
class UartModePanel extends StatelessWidget {
  const UartModePanel({super.key});

  @override
  Widget build(BuildContext context) {
    return Column(
      children: const [
        ServoControlPanel(),
        SizedBox(height: UiLayout.gap),
        PythonPage(),
      ],
    );
  }
}
