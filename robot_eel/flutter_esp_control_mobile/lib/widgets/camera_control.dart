import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class CameraControlPanel extends StatefulWidget {
  const CameraControlPanel({super.key});

  @override
  State<CameraControlPanel> createState() => _CameraControlPanelState();
}

class _CameraControlPanelState extends State<CameraControlPanel> {
  String resolution = "SVGA";
  double quality = 10;

  Timer? debounce;
  StreamSubscription? sub;

  final Map<String, int> frameSizeMap = {
    "UXGA": 11,
    "SXGA": 10,
    "SVGA": 7,
    "VGA": 6,
  };

  @override
  void initState() {
    super.initState();

    // ✅ 低頻廣播校正 UI（有回傳就同步）
    sub = WsControlApi.stream().listen((msg) {
      try {
        if (!mounted) return;
        if (msg is! Map) return;
        if (msg["type"] != "camera_param") return;

        setState(() {
          // framesize
          if (msg.containsKey("framesize")) {
            final rev = {for (final e in frameSizeMap.entries) e.value: e.key};
            resolution = rev[msg["framesize"]] ?? resolution;
          }

          // quality
          if (msg.containsKey("quality")) {
            quality = (msg["quality"] as num).toDouble();
          }
        });
      } catch (e) {
        debugPrint("Camera WS parse error: $e");
      }
    });
  }

  @override
  void dispose() {
    debounce?.cancel();
    sub?.cancel();
    super.dispose();
  }

  void applyResolution(String value) {
    // ✅ UI 立即更新（不依賴回傳）
    setState(() => resolution = value);

    // ✅ 送給 ESP32
    WsControlApi.setCameraParam({"framesize": frameSizeMap[value]!});
  }

  void applyQuality(double v) {
    // ✅ UI 立即更新
    setState(() => quality = v);

    // ✅ debounce 後送出，避免狂送
    debounce?.cancel();
    debounce = Timer(const Duration(milliseconds: 300), () {
      WsControlApi.setCameraParam({"quality": v.toInt()});
    });
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),

        // ✅ 控制面板內容可滑，橫向高度不足不爆版
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text("相機控制", style: TextStyle(fontSize: 20)),
              const SizedBox(height: 18),

              const Text("解析度"),
              DropdownButton<String>(
                value: resolution,
                isExpanded: true,
                items: frameSizeMap.keys
                    .map((k) => DropdownMenuItem(value: k, child: Text(k)))
                    .toList(),
                onChanged: (v) {
                  if (v == null) return;
                  applyResolution(v);
                },
              ),

              const SizedBox(height: 20),

              Text("JPEG Quality: ${quality.toInt()}"),
              Slider(
                value: quality,
                min: 5,
                max: 60,
                divisions: 55,
                label: quality.toInt().toString(),
                onChanged: applyQuality,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
