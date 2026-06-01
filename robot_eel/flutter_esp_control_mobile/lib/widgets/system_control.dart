import 'package:flutter/material.dart';
import '../ui/ui_card.dart';

class SystemControl extends StatelessWidget {
  const SystemControl({super.key});

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "系統控制",
      child: Padding(
        padding: EdgeInsets.zero,
        child: Row(
          children: [
            ElevatedButton(onPressed: () {}, child: const Text("暫停/繼續")),
            const SizedBox(width: 12),
            ElevatedButton(onPressed: () {}, child: const Text("下載 CSV")),
          ],
        ),
      ),
    );
  }
}
