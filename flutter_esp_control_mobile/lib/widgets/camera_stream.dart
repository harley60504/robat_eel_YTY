import 'dart:typed_data';
import 'dart:io';
import 'dart:async';
import 'package:flutter/material.dart';

class CameraStreamWS extends StatefulWidget {
  final String wsUrl;
  const CameraStreamWS({super.key, required this.wsUrl});

  @override
  State<CameraStreamWS> createState() => _CameraStreamWSState();
}

class _CameraStreamWSState extends State<CameraStreamWS> {
  WebSocket? socket;
  StreamSubscription? _sub;

  Uint8List? frame;

  int frameCount = 0;
  double fps = 0;
  DateTime lastTime = DateTime.now();

  @override
  void initState() {
    super.initState();
    _connect();
  }

  void _connect() async {
    try {
      socket = await WebSocket.connect(widget.wsUrl);

      _sub = socket!.listen(
        (data) {
          if (!mounted) return;

          setState(() {
            frame = Uint8List.fromList(data as List<int>);
          });

          _calcFPS();
        },
        onDone: () => debugPrint("Camera WS closed"),
        onError: (e) => debugPrint("Camera WS error: $e"),
      );
    } catch (e) {
      debugPrint("Camera WS connect failed: $e");
    }
  }

  void _calcFPS() {
    frameCount++;
    final now = DateTime.now();
    final diff = now.difference(lastTime).inMilliseconds;

    if (diff >= 1000) {
      fps = frameCount * 1000 / diff;
      frameCount = 0;
      lastTime = now;
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    socket?.close();
    socket = null;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        frame == null
            ? const Center(child: Text("Waiting for camera…"))
            : Image.memory(frame!, gaplessPlayback: true, fit: BoxFit.cover),

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
