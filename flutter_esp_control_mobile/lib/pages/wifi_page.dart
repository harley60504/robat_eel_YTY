import 'package:flutter/material.dart';
import '../widgets/wifi_current.dart';
import '../widgets/wifi_saved.dart';
import '../widgets/wifi_scan.dart';

class WiFiPage extends StatelessWidget {
  const WiFiPage({super.key});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isMobile = width < 700;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(12),

          child: Column(
            children: [
              isMobile
                  ? Column(
                      children: const [
                        WiFiCurrentCard(),
                        SizedBox(height: 12),
                        WiFiSavedCard(),
                      ],
                    )
                  : Row(
                      children: const [
                        Expanded(child: WiFiCurrentCard()),
                        SizedBox(width: 12),
                        Expanded(child: WiFiSavedCard()),
                      ],
                    ),

              const SizedBox(height: 12),

              const WiFiScanCard(),
            ],
          ),
        ),
      ),
    );
  }
}
