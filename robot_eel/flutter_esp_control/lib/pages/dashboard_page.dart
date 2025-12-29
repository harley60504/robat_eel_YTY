import 'package:flutter/material.dart';
import '../api/esp_api.dart'; // ★ 要加
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

    /// ★★★ WebSocket Debug Listener ★★★
    WsControlApi.stream().listen((msg) {
      if (enableControlDebugLog) {
        debugPrint("CONTROL WS RX: $msg");
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),
        child: GridView.count(
          padding: const EdgeInsets.all(10),
          crossAxisSpacing: 10,
          mainAxisSpacing: 10,
          crossAxisCount: width > 1000 ? 2 : 1,
          childAspectRatio: 1.35,
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
