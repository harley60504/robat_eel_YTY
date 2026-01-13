import 'package:flutter/material.dart';

class SystemControl extends StatelessWidget {
  const SystemControl({super.key});

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            ElevatedButton(onPressed: (){}, child: const Text("暫停/繼續")),
            const SizedBox(width: 12),
            ElevatedButton(onPressed: (){}, child: const Text("下載 CSV")),
          ],
        ),
      ),
    );
  }
}
