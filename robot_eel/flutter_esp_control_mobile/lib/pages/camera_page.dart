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
              ? Column(
                  children: const [
                    AspectRatio(
                      aspectRatio: 4 / 3,
                      child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                    ),
                    SizedBox(height: 16),
                    CameraControlPanel(),
                  ],
                )
              : Row(
                  children: const [
                    Expanded(
                      child: AspectRatio(
                        aspectRatio: 4 / 3,
                        child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                      ),
                    ),
                    SizedBox(width: 24),
                    SizedBox(width: 260, child: CameraControlPanel()),
                  ],
                ),
        ),
      ),
    );
  }
}
