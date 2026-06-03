import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../ui/ui_card.dart';
import '../ui/ui_layout.dart';

class MotionParam extends StatefulWidget {
  final bool compact;

  const MotionParam({super.key, this.compact = false});

  @override
  State<MotionParam> createState() => _MotionParamState();
}

class _MotionParamState extends State<MotionParam> {
  final freqCtrl = TextEditingController();
  final ampCtrl = TextEditingController();
  final lamCtrl = TextEditingController();
  final lCtrl = TextEditingController();

  double freq = double.nan;
  double amp = double.nan;
  double lambda = double.nan;
  double length = double.nan;

  StreamSubscription? _sub;

  bool _didInitText = false;
  bool _editing = false;

  @override
  void initState() {
    super.initState();

    _applyCtrlParams(WsControlApi.lastCtrlParams);

    _sub = WsControlApi.stream().listen((msg) {
      if (!mounted) return;
      if (msg is! Map) return;
      if (msg["type"] != "ctrl_params") return;
      _applyCtrlParams(msg);
    });
  }

  void _applyCtrlParams(Map? msg) {
    if (msg == null) return;

    setState(() {
      freq = _num(msg["frequency"]);
      amp = _num(msg["Ajoint"]);
      lambda = _num(msg["lambda"]);
      length = _num(msg["L"]);
    });

    // ✅ 如果使用者正在輸入，就不要更新文字框
    if (_editing) return;

    // ✅ 第一次同步一定要寫入 UI
    if (!_didInitText) {
      freqCtrl.text = _fmt(msg["frequency"]);
      ampCtrl.text = _fmt(msg["Ajoint"]);
      lamCtrl.text = _fmt(msg["lambda"]);
      lCtrl.text = _fmt(msg["L"]);
      _didInitText = true;
      return;
    }

    // ✅ 後續同步：只有空的才補值（安全）
    if (freqCtrl.text.isEmpty) freqCtrl.text = _fmt(msg["frequency"]);
    if (ampCtrl.text.isEmpty) ampCtrl.text = _fmt(msg["Ajoint"]);
    if (lamCtrl.text.isEmpty) lamCtrl.text = _fmt(msg["lambda"]);
    if (lCtrl.text.isEmpty) lCtrl.text = _fmt(msg["L"]);
  }

  String _fmt(dynamic v) {
    if (v == null) return "";
    final n = (v as num).toDouble();
    return n.toStringAsFixed(2);
  }

  double _num(dynamic v) {
    if (v is! num) return double.nan;
    return v.toDouble();
  }

  String _status(double v, {String unit = ""}) {
    if (v.isNaN) return "-";
    return "${v.toStringAsFixed(2)}$unit";
  }

  void setParam(String key, TextEditingController ctrl) {
    final v = double.tryParse(ctrl.text.trim());
    if (v == null) return;

    WsControlApi.setParam({key: v});
    setState(() => _editing = false);
  }

  @override
  void dispose() {
    _sub?.cancel();
    freqCtrl.dispose();
    ampCtrl.dispose();
    lamCtrl.dispose();
    lCtrl.dispose();
    super.dispose();
  }

  InputDecoration _fieldDeco() => const InputDecoration(
        border: OutlineInputBorder(),
        isDense: true,
        contentPadding: UiLayout.fieldPadding,
      );

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "參數設定",
      minHeight: widget.compact ? 188 : 240,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          paramRow(
            label: "頻率 (Hz)",
            value: _status(freq, unit: " Hz"),
            ctrl: freqCtrl,
            onSet: () => setParam("frequency", freqCtrl),
          ),
          const SizedBox(height: 8),
          paramRow(
            label: "振幅 (°)",
            value: _status(amp, unit: "°"),
            ctrl: ampCtrl,
            onSet: () => setParam("Ajoint", ampCtrl),
          ),
          const SizedBox(height: 8),
          paramRow(
            label: "λ",
            value: _status(lambda),
            ctrl: lamCtrl,
            onSet: () => setParam("lambda", lamCtrl),
          ),
          const SizedBox(height: 8),
          paramRow(
            label: "L",
            value: _status(length),
            ctrl: lCtrl,
            onSet: () => setParam("L", lCtrl),
          ),
        ],
      ),
    );
  }

  Widget paramRow({
    required String label,
    required String value,
    required TextEditingController ctrl,
    required VoidCallback onSet,
  }) {
    return LayoutBuilder(
      builder: (context, constraints) {
        Widget field() => TextField(
              controller: ctrl,
              keyboardType: TextInputType.number,
              onTap: () => setState(() => _editing = true),
              onChanged: (_) => setState(() => _editing = true),
              onSubmitted: (_) => onSet(),
              decoration: _fieldDeco(),
            );

        return Row(
          children: [
            SizedBox(
              width: widget.compact ? 78 : 112,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.centerLeft,
                    child: Text(label),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    value,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white70,
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            Expanded(child: field()),
            const SizedBox(width: 8),
            SizedBox(
              width: widget.compact ? 70 : 94,
              height: UiLayout.buttonHeight,
              child: ElevatedButton(
                style: ElevatedButton.styleFrom(
                  padding: EdgeInsets.zero,
                  textStyle: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                onPressed: onSet,
                child: const Text("設定", softWrap: false),
              ),
            ),
          ],
        );
      },
    );
  }
}
