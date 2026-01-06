import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../widgets/mode_switch.dart';
import '../widgets/motion_param.dart';
import '../widgets/system_status.dart';
import '../widgets/servo_table.dart';

const bool enableControlDebugLog = false;

class DashboardPage extends StatefulWidget {
  const DashboardPage({super.key});

  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  @override
  void initState() {
    super.initState();

    WsControlApi.stream().listen((msg) {
      if (enableControlDebugLog) {
        debugPrint("CONTROL WS RX: $msg");
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;

    final crossAxisCount = width > 900 ? 2 : 1;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),

        child: GridView.count(
          padding: const EdgeInsets.all(12),

          crossAxisCount: crossAxisCount,
          mainAxisSpacing: 12,
          crossAxisSpacing: 12,

          /// ★ 讓高度自適應內容
          childAspectRatio: 1.2,

          children: const [
            ModeSwitch(),
            MotionParam(),
            SystemStatus(),
            ServoTable(),
          ],
        ),
      ),
    );
  }
}
