import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

import '../api/python_api.dart';
import '../bridge/python_process_launcher.dart';
import '../config.dart';

class PythonRtspPreview extends StatefulWidget {
  const PythonRtspPreview({super.key});

  @override
  State<PythonRtspPreview> createState() => _PythonRtspPreviewState();
}

class _PythonRtspPreviewState extends State<PythonRtspPreview> {
  Timer? timer;
  Uint8List? frame;
  String status = "RTSP preview stopped";
  bool running = false;
  bool busy = false;
  bool recording = false;
  bool recordingBusy = false;
  int frames = 0;
  double fps = 0;
  DateTime lastFps = DateTime.now();

  @override
  void initState() {
    super.initState();
    startPreview();
  }

  @override
  void dispose() {
    timer?.cancel();
    PythonApi.previewStop(pcHost: ApiConfig.pythonHost);
    super.dispose();
  }

  Future<bool> ensurePythonReady() async {
    final pcHost = ApiConfig.pythonHost;
    var ready = await PythonApi.ping(pcHost: pcHost);
    if (ready) return true;

    setState(() => status = "Starting Python backend...");
    final launch = await PythonProcessLauncher.launch();
    if (!launch.ok) {
      setState(() => status = launch.message);
      return false;
    }

    ready = await PythonApi.waitUntilReady(pcHost: pcHost);
    if (!ready) setState(() => status = "Python backend offline");
    return ready;
  }

  Future<void> startPreview() async {
    if (busy) return;
    setState(() {
      busy = true;
      status = "Starting RTSP preview...";
    });

    final ready = await ensurePythonReady();
    if (!ready) {
      if (mounted) setState(() => busy = false);
      return;
    }

    final pcHost = ApiConfig.pythonHost;
    await PythonApi.setRecorderUrl(
      pcHost: pcHost,
      recorderUrl: ApiConfig.recorderUrl,
    );

    final ok = await PythonApi.previewStart(pcHost: pcHost);
    if (!mounted) return;
    setState(() {
      running = ok;
      busy = false;
      status = ok ? "Waiting for RTSP frame..." : "RTSP preview failed";
    });

    if (ok) {
      timer?.cancel();
      timer = Timer.periodic(const Duration(milliseconds: 100), (_) {
        fetchFrame();
      });
    }
  }

  Future<void> stopPreview() async {
    timer?.cancel();
    await PythonApi.previewStop(pcHost: ApiConfig.pythonHost);
    if (!mounted) return;
    setState(() {
      running = false;
      fps = 0;
      status = "RTSP preview stopped";
    });
  }

  Future<void> toggleRecording() async {
    if (recordingBusy) return;
    setState(() => recordingBusy = true);

    final ready = await ensurePythonReady();
    if (!ready) {
      if (mounted) setState(() => recordingBusy = false);
      return;
    }

    final pcHost = ApiConfig.pythonHost;
    await PythonApi.setRecorderUrl(
      pcHost: pcHost,
      recorderUrl: ApiConfig.recorderUrl,
    );

    final ok = recording
        ? await PythonApi.recordingStop(pcHost: pcHost)
        : await PythonApi.recordingStart(pcHost: pcHost);

    if (!mounted) return;
    setState(() {
      if (ok) recording = !recording;
      recordingBusy = false;
      status = ok
          ? (recording ? "RTSP preview recording" : "RTSP preview")
          : (recording ? "Stop recording failed" : "Start recording failed");
    });
  }

  Future<void> fetchFrame() async {
    try {
      final res = await http
          .get(PythonApi.previewFrameUri(pcHost: ApiConfig.pythonHost))
          .timeout(const Duration(milliseconds: 650));
      if (!mounted || res.statusCode != 200 || res.bodyBytes.isEmpty) return;

      setState(() {
        frame = res.bodyBytes;
        status = "RTSP preview";
      });
      calcFps();
    } catch (_) {}
  }

  void calcFps() {
    frames++;
    final now = DateTime.now();
    final diff = now.difference(lastFps).inMilliseconds;
    if (diff >= 1000) {
      setState(() {
        fps = frames * 1000 / diff;
        frames = 0;
        lastFps = now;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: Stack(
        fit: StackFit.expand,
        children: [
          Container(
            color: const Color(0xFF151A20),
            child: Center(
              child: frame == null
                  ? Text(status, style: const TextStyle(color: Colors.white70))
                  : FittedBox(
                      fit: BoxFit.contain,
                      child: Image.memory(frame!, gaplessPlayback: true),
                    ),
            ),
          ),
          Positioned(
            top: 8,
            left: 8,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              decoration: BoxDecoration(
                color: Colors.black54,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(
                "RTSP FPS: ${fps.toStringAsFixed(1)}",
                style: const TextStyle(color: Colors.white),
              ),
            ),
          ),
          Positioned(
            top: 8,
            right: 8,
            child: IconButton.filled(
              tooltip: recording ? "Stop recording" : "Start recording",
              onPressed: recordingBusy ? null : toggleRecording,
              style: IconButton.styleFrom(
                backgroundColor: recording ? Colors.redAccent : null,
                foregroundColor: Colors.white,
              ),
              icon: Icon(recording ? Icons.stop : Icons.fiber_manual_record),
            ),
          ),
        ],
      ),
    );
  }
}
