import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../ui/ui_card.dart';
import '../ui/ui_layout.dart';

class ServoControlPanel extends StatefulWidget {
  final bool compact;
  final bool embedded;

  const ServoControlPanel({
    super.key,
    this.compact = false,
    this.embedded = false,
  });

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
    final content = buildContent();
    if (widget.embedded) return content;

    return UiCard(
      title: "Angle \u63a7\u5236",
      minHeight: widget.compact ? 420 : 520,
      child: content,
    );
  }

  Widget buildContent() {
    return Scrollbar(
      controller: _scrollController,
      thumbVisibility: true,
      child: SingleChildScrollView(
        controller: _scrollController,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text("\u81ea\u52d5\u9001\u51fa"),
              subtitle: const Text(
                  "\u6ed1\u687f\u653e\u958b\u6642\u9001\u51fa set_angle"),
              value: autoSend,
              onChanged: (v) => setState(() => autoSend = v),
            ),
            const SizedBox(height: 8),
            ...List.generate(servoCount, (i) {
              return Padding(
                padding: EdgeInsets.only(bottom: widget.compact ? 8 : 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      "${i + 1}  ${angles[i].toStringAsFixed(2)} deg",
                      style: const TextStyle(fontWeight: FontWeight.w600),
                    ),
                    Row(
                      children: [
                        Expanded(
                          child: Slider(
                            min: minDeg,
                            max: maxDeg,
                            value: angles[i].clamp(minDeg, maxDeg),
                            onChanged: (v) => setAngleOnly(i, v),
                            onChangeEnd: (_) {
                              if (autoSend) sendAngles();
                            },
                          ),
                        ),
                        SizedBox(
                          width: widget.compact ? 52 : 64,
                          child: Text(
                            angles[i].toStringAsFixed(1),
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
                    child: const Text("\u9001\u51fa angle"),
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
                    child: const Text("\u91cd\u8a2d 120 deg"),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
