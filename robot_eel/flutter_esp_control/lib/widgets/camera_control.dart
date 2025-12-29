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

  final Map<String, int> frameSizeMap = {
    "UXGA": 11,
    "SXGA": 10,
    "SVGA": 7,
    "VGA": 6,
  };

  @override
  void initState() {
    super.initState();

    WsControlApi.stream().listen((msg) {
      if (msg["type"] == "camera_param") {
        setState(() {
          if (msg.containsKey("framesize")) {
            final rev = {for (final e in frameSizeMap.entries) e.value: e.key};

            resolution = rev[msg["framesize"]] ?? resolution;
          }

          if (msg.containsKey("quality")) {
            quality = (msg["quality"] ?? quality).toDouble();
          }
        });
      }
    });
  }

  void applyResolution(String value) {
    WsControlApi.setCameraParam({"framesize": frameSizeMap[value]!});
  }

  void applyQuality(double v) {
    WsControlApi.setCameraParam({"quality": v.toInt()});
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),

        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text("相機控制", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 18),

            const Text("解析度"),
            DropdownButton(
              value: resolution,
              items: frameSizeMap.keys
                  .map((k) => DropdownMenuItem(value: k, child: Text(k)))
                  .toList(),
              onChanged: (v) => applyResolution(v!),
            ),

            const SizedBox(height: 20),

            const Text("JPEG Quality"),
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
    );
  }
}
