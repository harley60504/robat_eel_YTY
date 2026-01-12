import 'package:flutter/material.dart';
import '../widgets/mode_switch.dart';
import '../widgets/motion_param.dart';
import '../widgets/system_status.dart';
import '../widgets/servo_table.dart';

class DashboardPage extends StatelessWidget {
  const DashboardPage({super.key});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isMobile = width < 700;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),
        child: Padding(
          padding: const EdgeInsets.all(12),

          child: isMobile
              ? ListView(
                  children: const [
                    ModeSwitch(),
                    SizedBox(height: 12),
                    MotionParam(),
                    SizedBox(height: 12),
                    SystemStatus(),
                    SizedBox(height: 12),
                    ServoTable(),
                  ],
                )
              : GridView.count(
                  crossAxisCount: width > 900 ? 2 : 1,
                  mainAxisSpacing: 12,
                  crossAxisSpacing: 12,
                  childAspectRatio: 1.25,
                  children: const [
                    ModeSwitch(),
                    MotionParam(),
                    SystemStatus(),
                    ServoTable(),
                  ],
                ),
        ),
      ),
    );
  }
}
