import 'dart:async';
import 'package:flutter/material.dart';
import '../api/esp_api.dart';
import '../ui/ui_card.dart';
import '../ui/ui_layout.dart';

class MotionParam extends StatefulWidget {
  const MotionParam({super.key});

  @override
  State<MotionParam> createState() => _MotionParamState();
}

class _MotionParamState extends State<MotionParam> {
  final freqCtrl = TextEditingController();
  final ampCtrl = TextEditingController();
  final lamCtrl = TextEditingController();
  final lCtrl = TextEditingController();

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

  Widget _setButton(VoidCallback onSet) {
    return SizedBox(
      height: UiLayout.buttonHeight,
      width: double.infinity,
      child: ElevatedButton(
        onPressed: onSet,
        child: const Text("設定"),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "參數設定",
      minHeight: 280,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          paramRow(
            label: "頻率 (Hz)",
            ctrl: freqCtrl,
            onSet: () => setParam("frequency", freqCtrl),
          ),
          paramRow(
            label: "振幅 (°)",
            ctrl: ampCtrl,
            onSet: () => setParam("Ajoint", ampCtrl),
          ),
          paramRow(
            label: "λ",
            ctrl: lamCtrl,
            onSet: () => setParam("lambda", lamCtrl),
          ),
          paramRow(
            label: "L",
            ctrl: lCtrl,
            onSet: () => setParam("L", lCtrl),
          ),
        ],
      ),
    );
  }

  Widget paramRow({
    required String label,
    required TextEditingController ctrl,
    required VoidCallback onSet,
  }) {
    return Padding(
      padding: const EdgeInsets.only(top: 10),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final isNarrow = constraints.maxWidth < 420;

          Widget field() => TextField(
                controller: ctrl,
                keyboardType: TextInputType.number,
                onTap: () => setState(() => _editing = true),
                onChanged: (_) => setState(() => _editing = true),
                onSubmitted: (_) => onSet(),
                decoration: _fieldDeco(),
              );

          if (isNarrow) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(label),
                const SizedBox(height: 6),
                field(),
                const SizedBox(height: 8),
                _setButton(onSet),
              ],
            );
          }

          return Row(
            children: [
              SizedBox(width: 100, child: Text(label)),
              const SizedBox(width: 10),
              Expanded(child: field()),
              const SizedBox(width: 10),
              SizedBox(
                width: 90,
                height: UiLayout.buttonHeight,
                child: ElevatedButton(
                  onPressed: onSet,
                  child: const Text("設定"),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}
