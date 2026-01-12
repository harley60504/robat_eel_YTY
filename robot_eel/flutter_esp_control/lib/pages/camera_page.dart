import 'package:flutter/material.dart';
import '../widgets/camera_stream.dart';
import '../widgets/camera_control.dart';
import '../config.dart';

class CameraPage extends StatelessWidget {
  const CameraPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1100),
        child: Padding(
          padding: const EdgeInsets.all(20),

          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,

            children: [
              Expanded(
                child: AspectRatio(
                  aspectRatio: 4 / 3,
                  child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                ),
              ),

              const SizedBox(width: 24),

              const SizedBox(
                width: 260, // 控制 panel 佔寬固定
                child: CameraControlPanel(),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
