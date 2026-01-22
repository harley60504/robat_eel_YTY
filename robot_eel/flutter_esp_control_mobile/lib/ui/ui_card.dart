import 'package:flutter/material.dart';
import 'ui_layout.dart';

class UiCard extends StatelessWidget {
  final String title;
  final Widget child;

  /// ✅ 統一卡片最小高度（用來讓左右看起來不要差太多）
  final double minHeight;

  const UiCard({
    super.key,
    required this.title,
    required this.child,
    this.minHeight = UiLayout.cardMinHeight,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: Card(
        elevation: 3,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: ConstrainedBox(
            constraints: BoxConstraints(minHeight: minHeight),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: const TextStyle(fontSize: 20)),
                const SizedBox(height: 12),
                child,
              ],
            ),
          ),
        ),
      ),
    );
  }
}
