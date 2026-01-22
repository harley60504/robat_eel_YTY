import 'package:flutter/material.dart';
import '../widgets/wifi_current.dart';
import '../widgets/wifi_saved.dart';
import '../widgets/wifi_scan.dart';

class WiFiPage extends StatelessWidget {
  const WiFiPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),

        child: SingleChildScrollView(
          padding: const EdgeInsets.all(10),

          child: Column(
            children: [
              Row(
                children: [
                  Expanded(child: WiFiCurrentCard()),
                  const SizedBox(width: 10),
                  Expanded(child: WiFiSavedCard()),
                ],
              ),

              const SizedBox(height: 10),

              const WiFiScanCard(),
            ],
          ),
        ),
      ),
    );
  }
}
