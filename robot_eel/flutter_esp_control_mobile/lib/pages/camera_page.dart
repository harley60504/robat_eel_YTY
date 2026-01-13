import 'package:flutter/material.dart';
import '../widgets/camera_stream.dart';
import '../widgets/camera_control.dart';
import '../config.dart';

class CameraPage extends StatelessWidget {
  const CameraPage({super.key});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isMobile = width < 700;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1100),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: isMobile
              ? ListView(
                  children: [
                    AspectRatio(
                      aspectRatio: 4 / 3,
                      child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                    ),
                    const SizedBox(height: 16),
                    const CameraControlPanel(),
                  ],
                )
              : Row(
                  children: [
                    Expanded(
                      child: AspectRatio(
                        aspectRatio: 4 / 3,
                        child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                      ),
                    ),
                    const SizedBox(width: 24),

                    // ✅ 改成彈性寬度，不要死固定 260（窄橫向容易爆）
                    ConstrainedBox(
                      constraints: const BoxConstraints(
                        minWidth: 220,
                        maxWidth: 320,
                      ),
                      child: const CameraControlPanel(),
                    ),
                  ],
                ),
        ),
      ),
    );
  }
}
