import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../ui/ui_card.dart';
import '../ui/ui_layout.dart';

class ServoControlPanel extends StatefulWidget {
  const ServoControlPanel({super.key});

  @override
  State<ServoControlPanel> createState() => _ServoControlPanelState();
}

class _ServoControlPanelState extends State<ServoControlPanel> {
  static const int servoCount = 6;
  static const double minDeg = 0;
  static const double maxDeg = 240;

  final List<double> angles = List.filled(servoCount, 120.0);
  bool autoSend = false;

  late final ScrollController _scrollController;

  @override
  void initState() {
    super.initState();
    _scrollController = ScrollController();
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  void sendAngles() {
    final mode = WsControlApi.lastCtrlParams?['mode'] ?? -1;
    if (mode != 3) return;
    WsControlApi.setAngle(angles);
  }

  void setAngleOnly(int index, double value) {
    setState(() => angles[index] = value);
  }

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "UART Servo 控制",
      minHeight: 520,
      child: LayoutBuilder(
        builder: (context, constraints) {
          return ConstrainedBox(
            constraints: BoxConstraints(maxHeight: constraints.maxHeight),
            child: Scrollbar(
              controller: _scrollController,
              thumbVisibility: true,
              child: SingleChildScrollView(
                controller: _scrollController,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    SwitchListTile(
                      contentPadding: EdgeInsets.zero,
                      title: const Text("即時送出"),
                      subtitle: const Text("拖拉結束後自動送 set_angle"),
                      value: autoSend,
                      onChanged: (v) => setState(() => autoSend = v),
                    ),
                    const SizedBox(height: 8),
                    ...List.generate(servoCount, (i) {
                      return Padding(
                        padding: const EdgeInsets.only(bottom: 12),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              "CH${i + 1}  ${angles[i].toStringAsFixed(1)}°",
                              style: const TextStyle(
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            Row(
                              children: [
                                Expanded(
                                  child: Slider(
                                    min: minDeg,
                                    max: maxDeg,
                                    value: angles[i].clamp(minDeg, maxDeg),
                                    onChanged: (v) => setAngleOnly(i, v),
                                    onChangeEnd: (v) {
                                      if (autoSend) sendAngles();
                                    },
                                  ),
                                ),
                                SizedBox(
                                  width: 58,
                                  child: Text(
                                    angles[i].toStringAsFixed(0),
                                    textAlign: TextAlign.right,
                                  ),
                                ),
                              ],
                            ),
                          ],
                        ),
                      );
                    }),
                    const SizedBox(height: 4),
                    Wrap(
                      spacing: 12,
                      runSpacing: 8,
                      children: [
                        SizedBox(
                          height: UiLayout.buttonHeight,
                          child: ElevatedButton(
                            onPressed: sendAngles,
                            child: const Text("送出 set_angle"),
                          ),
                        ),
                        SizedBox(
                          height: UiLayout.buttonHeight,
                          child: OutlinedButton(
                            onPressed: () {
                              setState(() {
                                for (int i = 0; i < servoCount; i++) {
                                  angles[i] = 120.0;
                                }
                              });
                              if (autoSend) sendAngles();
                            },
                            child: const Text("回到 120°"),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}
