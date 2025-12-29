import 'dart:typed_data';
import 'dart:html';
import 'package:flutter/material.dart';

class CameraStreamWS extends StatefulWidget {
  final String wsUrl;
  const CameraStreamWS({super.key, required this.wsUrl});

  @override
  State<CameraStreamWS> createState() => _CameraStreamWSState();
}

class _CameraStreamWSState extends State<CameraStreamWS> {
  WebSocket? socket;
  Uint8List? frame;

  int frameCount = 0;
  double fps = 0;
  DateTime lastTime = DateTime.now();

  @override
  void initState() {
    super.initState();

    socket = WebSocket(widget.wsUrl);
    socket!.binaryType = 'arraybuffer';

    socket!.onMessage.listen((event) {
      final data = event.data as ByteBuffer;

      setState(() {
        frame = Uint8List.view(data);
      });

      _calcFPS();     // 計算 FPS
    });
  }

  void _calcFPS() {
    frameCount++;

    final now = DateTime.now();
    final diff = now.difference(lastTime).inMilliseconds;

    if (diff >= 1000) {
      fps = frameCount * 1000 / diff;
      frameCount = 0;
      lastTime = now;

      // Debug 印出 FPS
      debugPrint("FPS = ${fps.toStringAsFixed(1)}");
    }
  }

  @override
  void dispose() {
    socket?.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [

        frame == null
            ? const Center(child: Text("Waiting…"))
            : Image.memory(
                frame!,
                gaplessPlayback: true,
                fit: BoxFit.cover,
              ),

        Positioned(
          top: 6,
          left: 6,
          child: Container(
            padding: const EdgeInsets.all(4),
            color: Colors.black54,
            child: Text(
              "FPS: ${fps.toStringAsFixed(1)}",
              style: const TextStyle(color: Colors.white),
            ),
          ),
        ),

      ],
    );
  }
}
